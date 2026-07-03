/*
 * StereoTubeTrem - Peavey Valverb-style tube/opto tremolo for Rack_StereoTubeTrem.
 *
 * Reference: racks/VALVERB.gif.  The Valverb schematic has a tube audio path
 * around 12AX7/12AT7 stages plus a transistor LED driver feeding a dual LDR
 * network for pan/tremolo. the game exposes Speed, Mix and Waveform; we keep
 * those slots and map them to the closest real behavior:
 *   Speed    -> Valverb trem oscillator speed (VR106 50K region)
 *   Mix      -> optical intensity/depth
 *   Waveform -> LFO shape/diode clipping amount (no literal Valverb knob)
 */
#include "DistrhoPlugin.hpp"
#include "StereoTubeTremParams.h"
#include "../../_shared/tube_stage.hpp"
#include "../../pedals/_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return rbmod::clamp01(v);
}

static inline float smoothstep(float v)
{
    return rbmod::smoothstep(v);
}

static inline float antiLog(float v)
{
    return std::pow(clamp01(v), 0.58f);
}

static inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float onePoleCoeffMs(float ms, float sr)
{
    const float safeSr = sr > 1000.0f ? sr : 48000.0f;
    return 1.0f - std::exp(-1.0f / std::fmax(1.0f, ms * 0.001f * safeSr));
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    return rbmod::onePoleCoeffHz(hz, sr > 1000.0f ? sr : 48000.0f);
}

static inline float speedHz(float speed)
{
    // Tube rack trem ranges tend to live below pedal chopper rates. Keep the
    // top fast enough for the game presets but leave more travel below 4 Hz.
    return 0.28f + 8.6f * std::pow(clamp01(speed), 1.45f);
}

class HighPass
{
    float x1 = 0.0f;
    float y1 = 0.0f;
    float a = 0.0f;

public:
    void set(float sr, float hz)
    {
        const float safeSr = sr > 1000.0f ? sr : 48000.0f;
        const float dt = 1.0f / safeSr;
        const float rc = 1.0f / (2.0f * rbmod::kPi * rbmod::clamp(hz, 3.0f, safeSr * 0.40f));
        a = rc / (rc + dt);
    }

    void reset()
    {
        x1 = 0.0f;
        y1 = 0.0f;
    }

    float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = y;
        return y;
    }
};

class LowPass
{
    float y = 0.0f;
    float a = 0.0f;

public:
    void set(float sr, float hz)
    {
        a = onePoleCoeffHz(hz, sr);
    }

    void reset()
    {
        y = 0.0f;
    }

    float process(float x)
    {
        y += a * (x - y);
        return y;
    }
};

class ValverbOpto
{
    float sampleRate = 48000.0f;
    float speed = kStereoTubeTremDef[kSpeed];
    float depth = kStereoTubeTremDef[kMix];
    float waveform = kStereoTubeTremDef[kWaveform];
    float phase = 0.0f;
    float phaseOffset = 0.0f;
    float led = 0.0f;
    float ldr = 0.0f;
    float gain = 1.0f;
    float ledA = 0.0f;
    float ldrRiseA = 0.0f;
    float ldrFallA = 0.0f;
    float gainA = 0.0f;

    void updateCoeffs()
    {
        const float s = clamp01(speed);
        const float w = smoothstep(waveform);
        ledA = onePoleCoeffMs(2.0f + 8.0f * (1.0f - s), sampleRate);
        ldrRiseA = onePoleCoeffMs(9.0f + 20.0f * (1.0f - s), sampleRate);
        ldrFallA = onePoleCoeffMs(62.0f + 72.0f * (1.0f - s) + 25.0f * (1.0f - w), sampleRate);
        gainA = onePoleCoeffMs(2.2f, sampleRate);
    }

public:
    void setPhaseOffset(float offset)
    {
        phaseOffset = offset - std::floor(offset);
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void reset()
    {
        phase = phaseOffset;
        led = 0.0f;
        ldr = 0.0f;
        gain = 1.0f;
        updateCoeffs();
    }

    void setControls(float speedNorm, float depthNorm, float waveNorm)
    {
        speed = clamp01(speedNorm);
        depth = clamp01(depthNorm);
        waveform = clamp01(waveNorm);
        updateCoeffs();
    }

    float processGain()
    {
        phase += speedHz(speed) / sampleRate;
        if (phase >= 1.0f)
            phase -= std::floor(phase);

        const float p = phase - std::floor(phase);
        const float sine = 0.5f + 0.5f * std::sin(rbmod::kTwoPi * p);
        const float tri = 1.0f - std::fabs(2.0f * (p - std::floor(p + 0.5f)));
        const float clipped = sine > 0.53f ? 1.0f : smoothstep(sine * 1.88f);
        const float w = smoothstep(waveform);

        // Valverb LFO is transistor/diode driven into an LED, so it is not a
        // pure sine. Waveform increases the diode-like top/bottom flattening.
        float drive = (1.0f - w) * (0.72f * sine + 0.28f * tri)
                    + w * (0.36f * tri + 0.64f * clipped);
        drive = smoothstep(rbmod::clamp(drive * (1.04f + 0.34f * w) - 0.04f * w, 0.0f, 1.0f));

        led += ledA * (drive - led);
        const float optical = std::pow(clamp01(led), 0.64f);
        ldr += (optical > ldr ? ldrRiseA : ldrFallA) * (optical - ldr);

        const float d = antiLog(depth);
        const float target = 1.0f - (0.92f * d) * std::pow(clamp01(ldr), 0.82f);
        gain += gainA * (rbmod::clamp(target, 0.055f, 1.0f) - gain);
        return gain;
    }
};

class ValverbChannel
{
    float sampleRate = 48000.0f;
    float depth = kStereoTubeTremDef[kMix];
    float waveform = kStereoTubeTremDef[kWaveform];

    rbtube::TubeStage inputTube;
    rbtube::TubeStageAT7 driverTube;
    rbtube::Miller12AX7 inputMiller;
    rbtube::Miller12AT7 driverMiller;
    HighPass inputHp;
    HighPass interstageHp;
    LowPass outputLp;
    ValverbOpto opto;

    void updateFilters()
    {
        const float d = antiLog(depth);
        const float w = smoothstep(waveform);
        inputHp.set(sampleRate, 9.0f);          // .047uF / 1M grid path
        interstageHp.set(sampleRate, 12.0f);    // tube coupling caps stay full-range
        outputLp.set(sampleRate, 11800.0f - 1900.0f * d - 700.0f * w);
    }

public:
    void setPhaseOffset(float offset)
    {
        opto.setPhaseOffset(offset);
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;

        // Schematic-derived operating regions:
        // V1A 12AX7: 100K plate, 2.2K cathode, 22uF bypass, .047uF coupling.
        // 12AT7 recovery/driver: lower gain and stiffer headroom at the output.
        inputTube.set(sampleRate, 1, 250.0f, 58.0f, 3.4f, 2200.0f);
        driverTube.set(sampleRate, 1, 250.0f, 86.0f, 36.0f, 1500.0f);
        inputMiller.set(sampleRate, 68000.0f, 42.0f, 6.0f);
        driverMiller.set(sampleRate, 180000.0f, 24.0f, 5.0f);
        opto.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        inputTube.reset();
        driverTube.reset();
        inputMiller.reset();
        driverMiller.reset();
        inputHp.reset();
        interstageHp.reset();
        outputLp.reset();
        opto.reset();
        updateFilters();
    }

    void setControls(float speedNorm, float depthNorm, float waveNorm)
    {
        depth = clamp01(depthNorm);
        waveform = clamp01(waveNorm);
        opto.setControls(speedNorm, depth, waveform);
        updateFilters();
    }

    float process(float in)
    {
        const float d = antiLog(depth);
        float x = inputHp.process(in);
        x = inputMiller.process(x);

        // Keep the rack clean-to-warm, not amp-like distortion. The tube stage
        // adds the asymmetry and mild compression of the Valverb line path.
        x = inputTube.process(x * dbToGain(-2.2f));
        x = interstageHp.process(x);

        const float optoGain = opto.processGain();
        const float makeup = 1.0f + 0.16f * d;
        x *= optoGain * makeup;

        x = driverMiller.process(x);
        x = driverTube.process(x * dbToGain(-0.9f));
        x = outputLp.process(x);

        const float trim = 2.65f + 2.75f * d;
        return rbmod::softClip(x * trim);
    }
};

} // namespace

class StereoTubeTremPlugin : public Plugin
{
    ValverbChannel left;
    ValverbChannel right;
    float params[kParamCount];

    void applyAll()
    {
        left.setControls(params[kSpeed], params[kMix], params[kWaveform]);
        right.setControls(params[kSpeed], params[kMix], params[kWaveform]);
    }

public:
    StereoTubeTremPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kStereoTubeTremDef[i];

        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.05f); // real Valverb: single LED -> both LDRs in-phase (mono trem); tiny offset = subtle width, no mono cancel
        left.setSampleRate(48000.0f);
        right.setSampleRate(48000.0f);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "StereoTubeTrem"; }
    const char* getDescription() const override { return "Valverb-style stereo tube/opto tremolo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'T', 't', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kStereoTubeTremNames[index];
        parameter.symbol = kStereoTubeTremSymbols[index];
        parameter.ranges.min = kStereoTubeTremMin[index];
        parameter.ranges.max = kStereoTubeTremMax[index];
        parameter.ranges.def = kStereoTubeTremDef[index];
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
        float* outL = outputs[0];
        float* outR = outputs[1];

        for (uint32_t i = 0; i < frames; ++i)
        {
            const rbmod::StereoInputPair feed = rbmod::stereoPedalFeeds(inputs[0][i], inputs[1][i]);
            outL[i] = left.process(feed.left);
            outR[i] = right.process(feed.right);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoTubeTremPlugin)
};

Plugin* createPlugin()
{
    return new StereoTubeTremPlugin();
}

END_NAMESPACE_DISTRHO
