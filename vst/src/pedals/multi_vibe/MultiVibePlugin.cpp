/*
 * MultiVibe - Boss VB-2 style BBD vibrato for the game's Pedal_MultiVibe.
 *
 * Local references: pedals/multi vibe.jpg, Boss-VB-2-Vibrato-Schematic-1.jpg
 * and boss vb-2 clone.jpeg. The schematic is a Boss VB-2 with
 * NJM4558 input/output stages, a BA662A/BA654 gain-control section and the
 * MN3207/MN3102 BBD clock pair. The real front panel is Rate, Depth and Rise
 * Time; the game's Speed, Mix and Waveform are mapped to those controls.
 */
#include "DistrhoPlugin.hpp"
#include "MultiVibeParams.h"
#include "../_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 6.28318530718f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
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

} // namespace

class MultiVibeCore
{
    float sampleRate = 48000.0f;
    float rate = kMultiVibeDef[kSpeed];
    float depth = kMultiVibeDef[kMix];
    float riseTime = kMultiVibeDef[kWaveform];
    float mode = kMultiVibeDef[kMode];

    rbmod::DelayBuffer delay;
    float lfoPhase = 0.0f;
    float lfoLag = 0.0f;
    float riseEnv = 0.0f;
    float smRate = kMultiVibeDef[kSpeed];
    float smDepth = kMultiVibeDef[kMix];
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float preY = 0.0f;
    float bbdY = 0.0f;
    float airY = 0.0f;
    float dc = 0.0f;
    float clockNoise = 0.0f;

    float hpA = 0.0f;
    float preA = 0.0f;
    float bbdA = 0.0f;
    float airA = 0.0f;
    float lfoA = 0.0f;
    float riseA = 0.0f;
    float noiseA = 0.0f;
    float paramA = 0.0f;
    rbmod::NoiseSource noise;

    float rateHz() const
    {
        // Measured from the nine VB-2 reference renders. The reverse-log 250 kC
        // RATE pot places noon much closer to the fast end than a linear map.
        const float curve = std::pow(clamp01(smRate), 1.27f);
        return 2.34f * std::pow(14.17f / 2.34f, curve);
    }

    void updateCoeffs()
    {
        const float dt = 1.0f / sampleRate;
        const float hpHz = 28.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);

        // The Q1/IC1 and Q2-Q4 networks are fixed filters. DEPTH only changes
        // the MN3102 clock excursion; it must not darken the audio path.
        preA = onePoleCoeffHz(9800.0f, sampleRate);
        bbdA = onePoleCoeffHz(6900.0f, sampleRate);
        airA = onePoleCoeffHz(6100.0f, sampleRate);
        lfoA = onePoleCoeffHz(58.0f, sampleRate);
        const float rise = std::pow(clamp01(riseTime), 1.7f); // VR3 250 kA
        riseA = coeffMs(18.0f + 1700.0f * rise, sampleRate);
        noiseA = onePoleCoeffHz(6400.0f, sampleRate);
        paramA = coeffMs(12.0f, sampleRate);
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
        smRate = rate;
        smDepth = depth;
        hpX1 = hpY1 = preY = bbdY = airY = dc = clockNoise = 0.0f;
        noise.seed(0x56423231u);
        updateCoeffs();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resizeForMs(sampleRate, 12.0f);
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
        // Physical selector order is UNLATCH / BYPASS / LATCH. The UI maps the
        // top position to 1, centre to 0.5 and bottom to 0.
        const bool bypass  = mode >= 0.34f && mode <= 0.67f;
        const bool unlatch = mode > 0.67f;

        smRate += paramA * (rate - smRate);
        smDepth += paramA * (depth - smDepth);

        lfoPhase += rateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        // BLOOM envelope = the latch/unlatch behaviour. Latch snaps to full,
        // Unlatch ramps in over Rise Time (riseA), Bypass fades to dry.
        const float target = bypass ? 0.0f : 1.0f;
        const float envCoeff = unlatch ? riseA : 0.02f;
        riseEnv += envCoeff * (target - riseEnv);
        const float bloom = riseEnv;

        // VR2 is 50 kB. The slight exponent is measured from the reference
        // delay spans: about 1.36 ms p-p at noon and 2.9-3.4 ms at maximum.
        const float intensity = std::pow(clamp01(smDepth), 1.15f);
        const float rateDepthLoss = 1.0f
                                  - 0.23f * smRate * smRate
                                  - 0.34f * smRate * smRate * smRate;
        const float rawLfo = lfoShape(lfoPhase);
        lfoLag += lfoA * (rawLfo - lfoLag);

        float x = highPass(in);
        x = lowPass(x, preY, preA);

        // BA662A is the rise/bypass VCA here, not a signal-dependent compander.
        x = std::tanh(x * 1.02f) * 0.98f;

        const float baseMs = 4.32f;
        const float widthMs = 1.72f * intensity * bloom * rateDepthLoss;
        const float delayMs = std::fmax(2.45f, std::fmin(6.20f, baseMs + widthMs * lfoLag));

        float wet = delay.readCubic(delayMs * 0.001f * sampleRate);
        delay.write(x);

        wet = lowPass(wet, bbdY, bbdA);
        const float darker = lowPass(wet, airY, airA);
        wet = darker + (wet - darker) * 0.62f;

        clockNoise += noiseA * (noise.next() - clockNoise);
        wet += clockNoise * (0.00010f + 0.00024f * intensity * bloom);

        dc += 0.00035f * (wet - dc);
        wet -= dc;

        const float throb = 1.0f - (0.004f + 0.012f * intensity * bloom) * (0.5f + 0.5f * lfoLag);
        wet *= throb;

        // VB-2 vibrato = 100% WET in Latch (pitch mod, no dry). bloom = wet mix:
        // Bypass -> dry, Unlatch crossfades dry->wet as it swells in.
        if (bypass)
            return in;
        const float y = in * (1.0f - bloom) + wet * bloom;
        return y * 0.87f;
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
    uint32_t getVersion() const override { return d_version(1, 3, 0); }
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
