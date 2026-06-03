/*
 * LineDrive - Boss OS-2 style drive for Rocksmith's Pedal_LineDrive.
 *
 * Local reference: pedals/line drive.png. The OS-2 has input/output buffers,
 * two op-amp clipping paths, a Color blend between overdrive and distortion,
 * Tone, and Level. Rocksmith exposes only Gain and Tone, so Color is fixed to
 * a practical drive/distortion blend and Level is internally compensated.
 */
#include "DistrhoPlugin.hpp"
#include "LineDriveParams.h"
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

static inline float asymClip(float x, float pos, float neg)
{
    if (x >= 0.0f)
        return pos * std::tanh(x / pos);
    return -neg * std::tanh((-x) / neg);
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

class LineDriveCore
{
    float sampleRate = 48000.0f;
    float gain = kLineDriveDef[kGain];
    float tone = kLineDriveDef[kTone];

    Biquad inputHp;
    Biquad odVoice;
    Biquad distPre;
    Biquad distPost;
    Biquad postMid;
    Biquad toneShelf;
    Biquad toneLowPass;

    void updateFilters()
    {
        const float g = smoothstep(gain);
        inputHp.setHighPass(sampleRate, 72.0f + 92.0f * gain, 0.68f);
        odVoice.setPeaking(sampleRate, 710.0f + 180.0f * tone, 0.76f,
                           2.2f + 2.2f * g);
        distPre.setHighPass(sampleRate, 210.0f + 175.0f * gain, 0.72f);
        distPost.setLowPass(sampleRate, 6800.0f - 2100.0f * g + 1200.0f * tone, 0.68f);
        postMid.setPeaking(sampleRate, 920.0f + 420.0f * tone, 0.70f,
                           1.4f + 1.6f * gain - 1.3f * tone);
        toneShelf.setHighShelf(sampleRate, 2050.0f + 2100.0f * tone, 0.72f,
                               -7.0f + 14.0f * tone);
        toneLowPass.setLowPass(sampleRate, 3000.0f + 7800.0f * tone, 0.64f);
    }

public:
    void reset()
    {
        inputHp.reset();
        odVoice.reset();
        distPre.reset();
        distPost.reset();
        postMid.reset();
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

        // OS-2 has two clipping paths mixed by Color. Rocksmith has no Color,
        // so keep a fixed modern blend that leans distortion as Gain rises.
        float od = odVoice.process(x);
        od *= 1.15f + 5.0f * gain + 9.0f * g;
        od = asymClip(od + 0.012f * gain, 0.58f - 0.07f * gain, 0.86f - 0.10f * gain);

        float dist = distPre.process(x);
        dist *= 2.1f + 10.5f * gain + 18.0f * g;
        dist = softClip(dist * (0.82f + 1.85f * gain));
        dist = distPost.process(dist);

        const float color = 0.48f + 0.22f * gain;
        float y = od * (1.0f - color) + dist * color;

        // The Line Drive is often used before already-hot amps. Low Gain
        // should still color, but not create a hidden clean boost.
        const float cleanLeak = 0.14f * (1.0f - gain);
        y = y * (1.0f - cleanLeak) + x * cleanLeak;

        y = postMid.process(y);
        y = toneShelf.process(y);
        y = toneLowPass.process(y);

        const float level = 0.70f / (1.0f + 0.42f * gain + 0.30f * g);
        return softClip(y * level) * 0.98f;
    }
};

class LineDrivePlugin : public Plugin
{
    LineDriveCore left;
    LineDriveCore right;
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
    LineDrivePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kLineDriveDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "LineDrive"; }
    const char* getDescription() const override { return "OS-2 style line drive"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('L', 'n', 'D', 'r'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kLineDriveNames[index];
        parameter.symbol = kLineDriveSymbols[index];
        parameter.ranges.min = kLineDriveMin[index];
        parameter.ranges.max = kLineDriveMax[index];
        parameter.ranges.def = kLineDriveDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LineDrivePlugin)
};

Plugin* createPlugin()
{
    return new LineDrivePlugin();
}

END_NAMESPACE_DISTRHO
