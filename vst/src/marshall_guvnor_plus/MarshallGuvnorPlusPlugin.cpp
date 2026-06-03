/*
 * MarshallGuvnorPlus - Marshall GV-2/Guv'nor Plus style drive for Rocksmith.
 *
 * Local references: pedals/Marshall GV2_1.png and pedals/marshall gv2_2.gif.
 * The circuit uses TL072 gain stages, LED/diode clipping, Bass/Mid/Treble tone
 * stack, and a Deep low-end control. The real Volume control is internally
 * compensated because Rocksmith pedal slots generally do not expose output.
 */
#include "DistrhoPlugin.hpp"
#include "MarshallGuvnorPlusParams.h"
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

static inline float ledClip(float x, float threshold)
{
    return threshold * std::tanh(x / threshold);
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

    void setLowShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);

        set(a * ((a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha),
            2.0f * a * ((a - 1.0f) - (a + 1.0f) * c),
            a * ((a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha,
            -2.0f * ((a - 1.0f) + (a + 1.0f) * c),
            (a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
};

} // namespace

class MarshallGuvnorPlusCore
{
    float sampleRate = 48000.0f;
    float gain = kMarshallGuvnorPlusDef[kGain];
    float bass = kMarshallGuvnorPlusDef[kBass];
    float mid = kMarshallGuvnorPlusDef[kMid];
    float treble = kMarshallGuvnorPlusDef[kTreble];
    float deep = kMarshallGuvnorPlusDef[kDeep];

    Biquad inputHp;
    Biquad deepShelf;
    Biquad preVoice;
    Biquad clipRollOff;
    Biquad bassShelf;
    Biquad midEq;
    Biquad trebleShelf;
    Biquad outputLp;

    static float eqDb(float normalized, float rangeDb)
    {
        return (clamp01(normalized) - 0.5f) * 2.0f * rangeDb;
    }

    void updateFilters()
    {
        const float g = smoothstep(gain);
        inputHp.setHighPass(sampleRate, 58.0f + 60.0f * gain, 0.70f);
        deepShelf.setLowShelf(sampleRate, 95.0f + 45.0f * deep, 0.78f,
                              -2.0f + 9.5f * deep);
        preVoice.setPeaking(sampleRate, 680.0f + 420.0f * mid, 0.78f,
                            1.4f + 2.6f * g);
        clipRollOff.setLowPass(sampleRate, 7200.0f - 1800.0f * g + 1000.0f * treble, 0.68f);
        bassShelf.setLowShelf(sampleRate, 135.0f, 0.75f, eqDb(bass, 11.0f));
        midEq.setPeaking(sampleRate, 620.0f + 560.0f * mid, 0.68f, eqDb(mid, 12.0f));
        trebleShelf.setHighShelf(sampleRate, 2300.0f + 1700.0f * treble, 0.72f, eqDb(treble, 12.5f));
        outputLp.setLowPass(sampleRate, 3900.0f + 7600.0f * treble, 0.62f);
    }

public:
    void reset()
    {
        inputHp.reset();
        deepShelf.reset();
        preVoice.reset();
        clipRollOff.reset();
        bassShelf.reset();
        midEq.reset();
        trebleShelf.reset();
        outputLp.reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setGain(float v) { gain = clamp01(v); updateFilters(); }
    void setBass(float v) { bass = clamp01(v); updateFilters(); }
    void setMid(float v) { mid = clamp01(v); updateFilters(); }
    void setTreble(float v) { treble = clamp01(v); updateFilters(); }
    void setDeep(float v) { deep = clamp01(v); updateFilters(); }

    float process(float in)
    {
        const float g = smoothstep(gain);
        float x = inputHp.process(in);
        x = deepShelf.process(x);
        x = preVoice.process(x);

        const float drive = 1.25f + 6.0f * gain + 12.0f * g;
        float y = x * drive;
        y = ledClip(y, 0.68f - 0.18f * gain);
        y = 0.82f * y + 0.18f * softClip(y * (1.7f + 2.0f * gain));
        y = clipRollOff.process(y);

        const float cleanLeak = 0.12f * (1.0f - gain);
        y = y * (1.0f - cleanLeak) + x * cleanLeak;

        y = bassShelf.process(y);
        y = midEq.process(y);
        y = trebleShelf.process(y);
        y = outputLp.process(y);

        const float level = 0.74f / (1.0f + 0.36f * gain + 0.20f * g + 0.12f * deep);
        return softClip(y * level) * 0.98f;
    }
};

class MarshallGuvnorPlusPlugin : public Plugin
{
    MarshallGuvnorPlusCore left;
    MarshallGuvnorPlusCore right;
    RBAutoMakeup makeupL;
    RBAutoMakeup makeupR;
    float params[kParamCount];

    void applyAll()
    {
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setBass(params[kBass]);
        right.setBass(params[kBass]);
        left.setMid(params[kMid]);
        right.setMid(params[kMid]);
        left.setTreble(params[kTreble]);
        right.setTreble(params[kTreble]);
        left.setDeep(params[kDeep]);
        right.setDeep(params[kDeep]);
    }

public:
    MarshallGuvnorPlusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kMarshallGuvnorPlusDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarshallGuvnorPlus"; }
    const char* getDescription() const override { return "Marshall GV-2 style drive"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'r', 'G', 'v'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMarshallGuvnorPlusNames[index];
        parameter.symbol = kMarshallGuvnorPlusSymbols[index];
        parameter.ranges.min = kMarshallGuvnorPlusMin[index];
        parameter.ranges.max = kMarshallGuvnorPlusMax[index];
        parameter.ranges.def = kMarshallGuvnorPlusDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarshallGuvnorPlusPlugin)
};

Plugin* createPlugin()
{
    return new MarshallGuvnorPlusPlugin();
}

END_NAMESPACE_DISTRHO
