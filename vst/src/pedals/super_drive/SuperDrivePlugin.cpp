/*
 * SuperDrive - Boss SD-1 style overdrive for Rocksmith's Pedal_SuperDrive.
 *
 * Local reference: pedals/super drive.pdf. The SD-1 uses a uPC4558 op-amp with
 * asymmetric silicon diode clipping in the feedback path, a pronounced mid
 * voice, and a tone network after clipping. Rocksmith exposes only Gain and
 * Tone, so the real pedal's Level control is internally compensated.
 */
#include "DistrhoPlugin.hpp"
#include "SuperDriveParams.h"
#include "../_shared/automakeup.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    return std::fmax(20.0f, std::fmin(hz, nyquist));
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float sd1FeedbackClip(float x, float gain)
{
    // SD-1/OD-1 family feedback clipping: one diode one way, two in series the
    // other. Keep it soft because this is inside the op-amp feedback path.
    const float posThresh = 0.46f - 0.06f * gain;
    const float negThresh = 0.82f - 0.10f * gain;
    if (x >= 0.0f)
        return posThresh * std::tanh(x / posThresh);
    return -negThresh * std::tanh((-x) / negThresh);
}

class Biquad
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f)
            na0 = 1.0f;
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        a1 = na1 * invA0;
        a2 = na2 * invA0;
    }

public:
    void reset()
    {
        z1 = z2 = 0.0f;
    }

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setHighPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setPeaking(float sr, float hz, float q, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a,
            1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }

    void setHighShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);

        set(a * ((a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
            a * ((a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha,
            2.0f * ((a - 1.0f) - (a + 1.0f) * c),
            (a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
};

} // namespace

class SuperDriveCore
{
    float sampleRate = 48000.0f;
    float gain = kSuperDriveDef[kGain];
    float tone = kSuperDriveDef[kTone];

    Biquad inputHp;
    Biquad feedbackVoice;
    Biquad opAmpRollOff;
    Biquad midHump;
    Biquad toneShelf;
    Biquad toneLowPass;

    void updateFilters()
    {
        const float g = smoothstep(gain);
        inputHp.setHighPass(sampleRate, 95.0f + 115.0f * gain, 0.68f);
        feedbackVoice.setPeaking(sampleRate, 720.0f + 150.0f * gain, 0.78f,
                                 2.4f + 3.2f * g);
        opAmpRollOff.setLowPass(sampleRate, 7600.0f - 1900.0f * g, 0.70f);
        midHump.setPeaking(sampleRate, 860.0f + 260.0f * tone, 0.72f,
                           3.0f + 1.7f * g - 1.0f * tone);
        toneShelf.setHighShelf(sampleRate, 1850.0f + 2100.0f * tone, 0.72f,
                               -8.0f + 15.0f * tone);
        toneLowPass.setLowPass(sampleRate, 2550.0f + 7600.0f * tone, 0.64f);
    }

public:
    void reset()
    {
        inputHp.reset();
        feedbackVoice.reset();
        opAmpRollOff.reset();
        midHump.reset();
        toneShelf.reset();
        toneLowPass.reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setGain(float v)
    {
        gain = clamp01(v);
        updateFilters();
    }

    void setTone(float v)
    {
        tone = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float g = smoothstep(gain);
        float x = inputHp.process(in);
        x = feedbackVoice.process(x);

        // Drive pot around the op-amp. Gain=1 in Rocksmith still should retain
        // a mild SD-1 voice, while Gain=80 reaches obvious overdrive.
        const float drive = 1.15f + 6.0f * gain + 18.5f * g;
        float y = x * drive;
        y = opAmpRollOff.process(y);

        const float clipped = sd1FeedbackClip(y + 0.018f * gain, gain);
        const float opAmpHair = softClip(y * (0.18f + 0.30f * gain)) * 0.22f;
        y = clipped * (0.96f + 0.05f * gain) + opAmpHair;

        // Low Rocksmith Gain settings are used as almost-clean color in a few
        // presets, so keep some unclipped op-amp path near the bottom.
        const float cleanLeak = 0.22f * (1.0f - gain);
        y = y * (1.0f - cleanLeak) + x * cleanLeak;

        y = midHump.process(y);
        y = toneShelf.process(y);
        y = toneLowPass.process(y);

        // No exposed Level knob. Keep perceived loudness controlled as Drive
        // increases so this behaves like an overdrive slot, not a boost slot.
        const float level = 0.76f / (1.0f + 0.45f * gain + 0.22f * g);
        return softClip(y * level) * 0.98f;
    }
};

class SuperDrivePlugin : public Plugin
{
    SuperDriveCore left;
    SuperDriveCore right;
    RBAutoMakeup makeupL;
    RBAutoMakeup makeupR;
    float params[kParamCount];

    void applyAll()
    {
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
    }

public:
    SuperDrivePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kSuperDriveDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "SuperDrive"; }
    const char* getDescription() const override { return "SD-1 style super overdrive"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'p', 'D', 'r'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kSuperDriveNames[index];
        parameter.symbol = kSuperDriveSymbols[index];
        parameter.ranges.min = kSuperDriveMin[index];
        parameter.ranges.max = kSuperDriveMax[index];
        parameter.ranges.def = kSuperDriveDef[index];
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
        makeupL.snap();
        makeupR.snap();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
        makeupL.setSampleRate((float)newSampleRate);
        makeupR.setSampleRate((float)newSampleRate);
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
            // Auto makeup-gain: match output loudness to the dry input so the
            // drive's controls change only the amount of clip, not the level.
            outL[i] = makeupL.process(inL[i], left.process(inL[i]));
            outR[i] = makeupR.process(inR[i], right.process(inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SuperDrivePlugin)
};

Plugin* createPlugin()
{
    return new SuperDrivePlugin();
}

END_NAMESPACE_DISTRHO
