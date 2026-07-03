/*
 * StereoPhaser - Boss RPH-10 Micro Rack style phaser for Rack_StereoPhaser.
 *
 * References:
 *   racks/RPH-10_owners_manual.pdf
 *   racks/rph-10 block diagram.jpeg
 *
 * The RPH-10 block diagram is: input level switch -> compressor -> 12-stage
 * phase shift block with feedback -> expander -> output level switch.  The
 * panel exposes Manual, Rate, Depth, Mode I/II/III (6/10/12 stages), and
 * Feedback.  the game only has Rate, Depth, and Mix, so this plugin keeps the
 * public slots and fixes the missing controls to a musical rack default.
 */
#include "DistrhoPlugin.hpp"
#include "StereoPhaserParams.h"
#include "../../pedals/_shared/ChorusComponents.h"
#include "../../pedals/_shared/opamp.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr int kStageCount = 12;

static inline float clamp01(float v)
{
    return rbmod::clamp01(v);
}

static inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float onePoleCoeff(float hz, float sr)
{
    return rbmod::onePoleCoeffHz(hz, sr > 1000.0f ? sr : 48000.0f);
}

static inline float rateHzFromRack(float rate)
{
    // Existing the game mapping treats the Rate slot as normalized 0.1..6 Hz.
    // Keep that behavior but use an audio taper so the first half of the knob
    // is not already near fast rotary territory.
    return 0.07f + 9.93f * std::pow(clamp01(rate), 1.55f);  // ~0.07..10 Hz (RPH-10 spec)
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
        const float rc = 1.0f / (2.0f * rbmod::kPi * rbmod::clamp(hz, 4.0f, safeSr * 0.40f));
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
        a = onePoleCoeff(hz, sr);
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

class FirstOrderAllpassHz
{
    float z = 0.0f;

public:
    void reset()
    {
        z = 0.0f;
    }

    float process(float x, float sr, float hz)
    {
        const float safeSr = sr > 1000.0f ? sr : 48000.0f;
        hz = rbmod::clamp(hz, 18.0f, safeSr * 0.42f);
        const float t = std::tan(rbmod::kPi * hz / safeSr);
        const float a = (t - 1.0f) / (t + 1.0f);
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
};

class RackCompander
{
    float sampleRate = 48000.0f;
    float env = 0.0f;
    float gain = 1.0f;
    float envA = 0.0f;

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        envA = onePoleCoeff(24.0f, sampleRate);
        reset();
    }

    void reset()
    {
        env = 0.0f;
        gain = 1.0f;
    }

    float compress(float x)
    {
        env += envA * (std::fabs(x) - env);
        const float target = 1.0f / std::sqrt(0.24f + 5.8f * env);
        gain += 0.0055f * (target - gain);
        return rbmod::softClip(x * (0.92f + 0.42f * gain));
    }

    float expand(float x, float amount)
    {
        const float restore = 0.92f + amount * (0.24f + 0.55f * env);
        return rbmod::softClip(x * restore);
    }
};

class Rph10Core
{
    float sampleRate = 48000.0f;
    float rate = kStereoPhaserDef[kRate];
    float depth = kStereoPhaserDef[kDepth];
    float mix = kStereoPhaserDef[kMix];
    float phaseOffset = 0.0f;
    float polarity = 1.0f;

    rbshared::OpAmpStage inputAmp;
    rbshared::OpAmpStage outputAmp;
    RackCompander compander;
    FirstOrderAllpassHz stages[kStageCount];
    HighPass inputHp;
    LowPass inputLp;
    LowPass outputLp;
    LowPass lfoLag;

    float lfoPhase = 0.0f;
    float feedback = 0.0f;

    void updateFilters()
    {
        const float d = rbmod::smoothstep(depth);
        const float m = rbmod::smoothstep(mix);
        inputHp.set(sampleRate, 22.0f);
        inputLp.set(sampleRate, 13200.0f - 1800.0f * m);
        outputLp.set(sampleRate, 9400.0f - 2100.0f * d - 900.0f * m);
        lfoLag.set(sampleRate, 3.2f + 9.5f * rbmod::smoothstep(rate));
    }

    float modulation()
    {
        const float p = lfoPhase + phaseOffset;
        const float wrapped = p - std::floor(p);
        const float sine = std::sin(rbmod::kTwoPi * wrapped);
        const float tri = 1.0f - 4.0f * std::fabs(wrapped - 0.5f);
        const float shaped = 0.5f + 0.5f * (0.66f * sine + 0.34f * tri);
        return lfoLag.process(clamp01(0.5f + polarity * (shaped - 0.5f)));
    }

public:
    void setPhaseOffset(float offset)
    {
        phaseOffset = offset - std::floor(offset);
    }

    void setPolarity(float p)
    {
        polarity = p < 0.0f ? -1.0f : 1.0f;
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        inputAmp.setSpec(rbshared::m5218Spec());
        outputAmp.setSpec(rbshared::m5218Spec());
        inputAmp.setSampleRate(sampleRate);
        outputAmp.setSampleRate(sampleRate);
        compander.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        lfoPhase = phaseOffset;
        feedback = 0.0f;
        for (int i = 0; i < kStageCount; ++i)
            stages[i].reset();
        inputHp.reset();
        inputLp.reset();
        outputLp.reset();
        lfoLag.reset();
        compander.reset();
        inputAmp.reset();
        outputAmp.reset();
        updateFilters();
    }

    void setControls(float rateNorm, float depthNorm, float mixNorm)
    {
        rate = clamp01(rateNorm);
        depth = clamp01(depthNorm);
        mix = clamp01(mixNorm);
        updateFilters();
    }

    float process(float in)
    {
        lfoPhase += rateHzFromRack(rate) / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float d = 0.06f + 0.94f * rbmod::smoothstep(depth);
        const float m = rbmod::smoothstep(mix);
        const float lfo = modulation();

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = inputAmp.process(x * dbToGain(-1.0f), 1.4f);

        const float compressed = compander.compress(x);

        static const float baseHz[kStageCount] = {
            52.0f, 72.0f, 101.0f, 142.0f, 205.0f, 298.0f,
            435.0f, 635.0f, 930.0f, 1360.0f, 1985.0f, 2880.0f
        };

        // The real rack has a separate Manual knob.  Fix it slightly above
        // center so the 12-stage mode gives audible notches without hollowing
        // the whole guitar range at default presets.
        const float manual = 0.58f;
        const float sweepCv = rbmod::clamp01(manual + (lfo - 0.5f) * (0.33f + 0.62f * d));
        const float feedbackAmount = 0.18f + 0.42f * d + 0.12f * m;

        float shifted = compressed - feedback * feedbackAmount;
        float tap6 = 0.0f;
        float tap10 = 0.0f;

        for (int i = 0; i < kStageCount; ++i)
        {
            const float skew = 0.82f + 0.043f * (float)i;
            const float localCv = rbmod::clamp01(sweepCv + 0.018f * (float)(i - 5));
            const float sweep = 0.36f + (12.4f + 7.4f * d) * rbmod::smoothstep(localCv);
            shifted = stages[i].process(shifted, sampleRate, baseHz[i] * sweep * skew);
            if (i == 5)
                tap6 = shifted;
            else if (i == 9)
                tap10 = shifted;
        }

        feedback = rbmod::softClip(shifted * (0.82f + 0.20f * m));

        // RPH-10 Mode I/II/III selects 6/10/12 stages.  With no Mode slot in
        // the game, Depth crossfades from a lighter mode to the strong mode.
        const float mode12 = rbmod::smoothstep(d);
        const float mode10 = 1.0f - std::fabs(mode12 - 0.55f) / 0.55f;
        const float mode6 = 1.0f - mode12;
        const float wetRaw = (0.30f * mode6 * tap6)
                           + (0.42f * rbmod::clamp01(mode10) * tap10)
                           + (0.58f * mode12 * shifted);

        float wet = compander.expand(outputLp.process(wetRaw), 0.58f + 0.22f * d);

        // At 50/50 mix the dry and phase-shifted paths make the classic moving
        // notches.  Avoid full wet at max Mix because a phaser becomes a plain
        // phase-shifted signal with weaker notches.
        const float wetLevel = 0.12f + 0.78f * m;
        const float dryLevel = 0.96f - 0.18f * m;
        float y = dryLevel * x - wetLevel * wet;

        y = outputAmp.process(y * dbToGain(0.2f), 1.15f);
        const float levelTrim = 0.96f - 0.16f * m - 0.10f * m * d;
        return rbmod::softClip(y) * levelTrim;
    }
};

} // namespace

class StereoPhaserPlugin : public Plugin
{
    Rph10Core left;
    Rph10Core right;
    float params[kParamCount];

    void applyAll()
    {
        left.setControls(params[kRate], params[kDepth], params[kMix]);
        right.setControls(params[kRate], params[kDepth], params[kMix]);
    }

public:
    StereoPhaserPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kStereoPhaserDef[i];

        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.00f);
        left.setPolarity(1.0f);
        right.setPolarity(-1.0f);
        left.setSampleRate(48000.0f);
        right.setSampleRate(48000.0f);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "StereoPhaser"; }
    const char* getDescription() const override { return "Boss RPH-10 12-stage stereo rack phaser"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'P', 'h', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kStereoPhaserNames[index];
        parameter.symbol = kStereoPhaserSymbols[index];
        parameter.ranges.min = kStereoPhaserMin[index];
        parameter.ranges.max = kStereoPhaserMax[index];
        parameter.ranges.def = kStereoPhaserDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoPhaserPlugin)
};

Plugin* createPlugin()
{
    return new StereoPhaserPlugin();
}

END_NAMESPACE_DISTRHO
