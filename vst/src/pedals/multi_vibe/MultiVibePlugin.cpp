/*
 * MultiVibe - Boss VB-2 style BBD vibrato for the game's Pedal_MultiVibe.
 *
 * Local reference: pedals/multi vibe.jpg. The schematic is a Boss VB-2 with
 * NJM4558 input/output stages, a BA662A/BA654 gain-control section and the
 * MN3207/MN3102 BBD clock pair. The real front panel is Rate, Depth and Rise
 * Time; the game's Speed, Mix and Waveform are mapped to those controls.
 */
#include "DistrhoPlugin.hpp"
#include "MultiVibeParams.h"
#include "../_shared/ChorusComponents.h"
#include <cmath>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 6.28318530718f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = std::fmax(10.0f, std::fmin(hz, sr * 0.45f));
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

static inline float coeffMs(float ms, float sr)
{
    ms = std::fmax(0.1f, ms);
    return 1.0f - std::exp(-1.0f / (ms * 0.001f * sr));
}

class DelayBuffer
{
    std::vector<float> data;
    int writeIndex = 0;

public:
    void resize(int samples)
    {
        if (samples < 8)
            samples = 8;
        data.assign((size_t)samples, 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = 0.0f;
        writeIndex = 0;
    }

    float read(float delaySamples) const
    {
        const int size = (int)data.size();
        if (size <= 4)
            return 0.0f;

        delaySamples = std::fmax(1.0f, std::fmin(delaySamples, (float)(size - 3)));
        float pos = (float)writeIndex - delaySamples;
        while (pos < 0.0f)
            pos += (float)size;
        while (pos >= (float)size)
            pos -= (float)size;

        const int i0 = (int)std::floor(pos);
        const int i1 = (i0 + 1) % size;
        const float frac = pos - (float)i0;
        return data[(size_t)i0] + (data[(size_t)i1] - data[(size_t)i0]) * frac;
    }

    void write(float x)
    {
        if (data.empty())
            return;
        data[(size_t)writeIndex] = x;
        ++writeIndex;
        if (writeIndex >= (int)data.size())
            writeIndex = 0;
    }
};

} // namespace

class MultiVibeCore
{
    float sampleRate = 48000.0f;
    float rate = kMultiVibeDef[kSpeed];
    float depth = kMultiVibeDef[kMix];
    float riseTime = kMultiVibeDef[kWaveform];
    float mode = kMultiVibeDef[kMode];

    DelayBuffer delay;
    float lfoPhase = 0.0f;
    float lfoLag = 0.0f;
    float riseEnv = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float preY = 0.0f;
    float bbdY = 0.0f;
    float airY = 0.0f;
    float dc = 0.0f;
    float compEnv = 0.0f;
    float clockNoise = 0.0f;

    float hpA = 0.0f;
    float preA = 0.0f;
    float bbdA = 0.0f;
    float airA = 0.0f;
    float lfoA = 0.0f;
    float compA = 0.0f;
    float riseA = 0.0f;
    float noiseA = 0.0f;
    rbmod::NoiseSource noise;

    float rateHz() const
    {
        const float r = clamp01(rate);
        return 0.12f + 6.65f * std::pow(r, 2.02f);
    }

    void updateCoeffs()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 28.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        const float d = smoothstep(depth);
        const float rise = smoothstep(riseTime);
        preA = onePoleCoeffHz(7800.0f - 900.0f * d, sampleRate);
        bbdA = onePoleCoeffHz(4650.0f - 1150.0f * d + 220.0f * rise, sampleRate);
        airA = onePoleCoeffHz(2300.0f + 850.0f * (1.0f - d), sampleRate);
        lfoA = onePoleCoeffHz(12.0f + 22.0f * rate, sampleRate);
        compA = onePoleCoeffHz(42.0f, sampleRate);
        riseA = coeffMs(18.0f + 1550.0f * rise * rise, sampleRate);
        noiseA = onePoleCoeffHz(6400.0f, sampleRate);
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

    static float lowPass(float x, float& z, float a)
    {
        z += a * (x - z);
        return z;
    }

    float lfoShape(float phase) const
    {
        // VB-2 vibrato LFO ~ sinusoidal. Rise Time no longer skews it — it now
        // sets the UNLATCH bloom time (its real function).
        phase -= std::floor(phase);
        return std::sin(kTwoPi * phase);
    }

public:
    void reset()
    {
        delay.reset();
        lfoPhase = 0.0f;
        lfoLag = 0.0f;
        riseEnv = 0.0f;
        hpX1 = hpY1 = preY = bbdY = airY = dc = compEnv = clockNoise = 0.0f;
        noise.seed(0x56423231u);
        updateCoeffs();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resize((int)(sampleRate * 0.060f));
        reset();
    }

    void setSpeed(float v)
    {
        rate = clamp01(v);
        updateCoeffs();
    }

    void setMix(float v)
    {
        depth = clamp01(v);
        updateCoeffs();
    }

    void setWaveform(float v)
    {
        riseTime = clamp01(v);
        updateCoeffs();
    }

    void setMode(float v)
    {
        mode = clamp01(v);
    }

    float process(float in)
    {
        // Real VB-2 MODE toggle: <0.34 = BYPASS (dry), 0.34-0.67 = LATCH
        // (continuous vibrato), >0.67 = UNLATCH (swells the vibrato in over
        // RISE TIME). Default 0.5 = Latch.
        const bool bypass  = mode < 0.34f;
        const bool unlatch = mode > 0.67f;

        lfoPhase += rateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        // BLOOM envelope = the latch/unlatch behaviour. Latch snaps to full,
        // Unlatch ramps in over Rise Time (riseA), Bypass fades to dry.
        const float target = bypass ? 0.0f : 1.0f;
        const float envCoeff = unlatch ? riseA : 0.02f;
        riseEnv += envCoeff * (target - riseEnv);
        const float bloom = riseEnv;

        const float intensity = smoothstep(depth);   // DEPTH = pitch excursion only
        const float rawLfo = lfoShape(lfoPhase);
        lfoLag += lfoA * (rawLfo - lfoLag);

        float x = highPass(in);
        x = lowPass(x, preY, preA);

        compEnv += compA * (std::fabs(x) - compEnv);
        const float ba662Gain = 1.0f / (1.0f + (0.38f + 0.78f * intensity) * compEnv);
        x = std::tanh(x * ba662Gain * 1.08f) * 0.96f;

        const float baseMs = 5.85f;
        const float widthMs = 0.28f + 5.55f * intensity * bloom;   // excursion swells in on unlatch
        const float delayMs = std::fmax(1.35f, std::fmin(18.0f, baseMs + widthMs * lfoLag));

        float wet = delay.read(delayMs * 0.001f * sampleRate);
        delay.write(x);

        wet = lowPass(wet, bbdY, bbdA);
        const float darker = lowPass(wet, airY, airA);
        wet = darker + (wet - darker) * 0.18f;

        clockNoise += noiseA * (noise.next() - clockNoise);
        wet += clockNoise * (0.00010f + 0.00024f * intensity * bloom);

        dc += 0.00035f * (wet - dc);
        wet -= dc;

        const float throb = 1.0f - (0.012f + 0.042f * intensity * bloom) * (0.5f + 0.5f * lfoLag);
        wet *= throb;

        // VB-2 vibrato = 100% WET in Latch (pitch mod, no dry). bloom = wet mix:
        // Bypass -> dry, Unlatch crossfades dry->wet as it swells in.
        const float y = in * (1.0f - bloom) + wet * bloom;
        return y * 0.985f;
    }
};

class MultiVibePlugin : public Plugin
{
    MultiVibeCore left;
    MultiVibeCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setSpeed(params[kSpeed]);
        right.setSpeed(params[kSpeed]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setWaveform(params[kWaveform]);
        right.setWaveform(params[kWaveform]);
        left.setMode(params[kMode]);
        right.setMode(params[kMode]);
    }

public:
    MultiVibePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kMultiVibeDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MultiVibe"; }
    const char* getDescription() const override { return "Boss VB-2 style BBD vibrato"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'l', 'V', 'b'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMultiVibeNames[index];
        parameter.symbol = kMultiVibeSymbols[index];
        parameter.ranges.min = kMultiVibeMin[index];
        parameter.ranges.max = kMultiVibeMax[index];
        parameter.ranges.def = kMultiVibeDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = clamp01(value);
        applyAll();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            const rbmod::StereoInputPair feed = rbmod::stereoPedalFeeds(inL[i], inR[i]);
            outL[i] = left.process(feed.left);
            outR[i] = right.process(feed.right);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiVibePlugin)
};

Plugin* createPlugin()
{
    return new MultiVibePlugin();
}

END_NAMESPACE_DISTRHO
