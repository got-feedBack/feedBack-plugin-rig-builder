/*
 * BassFuzz - EHX Bass Big Muff Pi style model for Bass_Pedal_BassFuzz.
 *
 * Schematic blocks modeled here: input coupling, BC547 common-emitter gain
 * stages, two 1N4148 anti-parallel clipping cells, Big Muff passive tone stack,
 * the bass-version mode switch, and the TLC2264 output/buffer behavior.
 */
#include "DistrhoPlugin.hpp"
#include "BassFuzzParams.h"
#include "../_shared/opamp.hpp"
#include "../_shared/semiconductors.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float quantize3(float v)
{
    return v < 0.25f ? 0.0f : (v < 0.75f ? 0.5f : 1.0f);
}

static inline float onePoleCoef(float fc, float fs)
{
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

static inline float audioTaper(float v)
{
    const float x = clamp01(v);
    return std::pow(x, 2.15f);
}

static inline float bjtCommonEmitter(float x, float bias)
{
    const float shifted = x + bias;
    const float compressed = std::tanh(shifted) - 0.18f * std::tanh(0.28f * shifted);
    return compressed - (std::tanh(bias) - 0.18f * std::tanh(0.28f * bias));
}

class BassBigMuff
{
    float fs = 48000.0f;
    float zInput = 0.0f;
    float zCleanLow = 0.0f;
    float zStage1 = 0.0f;
    float zStage2 = 0.0f;
    float zToneLow = 0.0f;
    float zToneHigh = 0.0f;
    float zOutDc = 0.0f;
    float cInput = 0.0f;
    float cCleanLow = 0.0f;
    float cStage1 = 0.0f;
    float cStage2 = 0.0f;
    float cToneLow = 0.0f;
    float cToneHigh = 0.0f;
    float cOutDc = 0.0f;
    float sustainGain = 55.0f;
    float tone = 0.52f;
    float volumeGain = 0.75f;
    float mode = 0.0f;
    rbshared::OpAmpStage inputBuffer;
    rbshared::OpAmpStage outputBuffer;
    rbcomponents::AntiParallelDiodePair clip1;
    rbcomponents::AntiParallelDiodePair clip2;

public:
    BassBigMuff()
    {
        inputBuffer.setSpec(rbshared::tlc2264Spec());
        outputBuffer.setSpec(rbshared::tlc2264Spec());
        clip1.setSpec(rbcomponents::diode1N4148());
        clip2.setSpec(rbcomponents::diode1N4148());
        clip1.setSourceR(3900.0f);
        clip2.setSourceR(3300.0f);
    }

    void setSampleRate(float s)
    {
        fs = s > 1000.0f ? s : 48000.0f;
        inputBuffer.setSampleRate(fs);
        outputBuffer.setSampleRate(fs);
        recalcFixed();
    }

    void recalcFixed()
    {
        cInput = onePoleCoef(37.0f, fs);
        cCleanLow = onePoleCoef(155.0f, fs);
        cStage1 = onePoleCoef(6900.0f, fs);
        cStage2 = onePoleCoef(5600.0f, fs);
        cToneLow = onePoleCoef(520.0f, fs);
        cToneHigh = onePoleCoef(760.0f, fs);
        cOutDc = onePoleCoef(12.0f, fs);
    }

    void setParams(float sustain, float toneP, float volume, float bassDry)
    {
        sustainGain = 18.0f + 145.0f * audioTaper(sustain);
        tone = clamp01(toneP);
        volumeGain = 2.15f * audioTaper(volume);
        mode = quantize3(bassDry);
    }

    float process(float x)
    {
        zCleanLow += cCleanLow * (x - zCleanLow);
        const float lowTap = zCleanLow;

        zInput += cInput * (x - zInput);
        float s = inputBuffer.process(x - zInput, 1.4f);

        s = bjtCommonEmitter(s * sustainGain, 0.14f);
        s = clip1.process(s);
        zStage1 += cStage1 * (s - zStage1);
        s = zStage1;

        s = bjtCommonEmitter(s * (5.0f + 14.0f * audioTaper(sustainGain / 163.0f)), -0.09f);
        s = clip2.process(s);
        zStage2 += cStage2 * (s - zStage2);
        s = zStage2;

        zToneLow += cToneLow * (s - zToneLow);
        zToneHigh += cToneHigh * (s - zToneHigh);
        const float low = zToneLow * 1.55f;
        const float high = (s - zToneHigh) * 1.28f;
        float out = low * (1.0f - tone) + high * tone;

        if (mode == 0.5f)
            out = out * 0.88f + lowTap * 0.95f;
        else if (mode > 0.5f)
            out = out * 0.62f + x * 0.58f + lowTap * 0.45f;

        out = outputBuffer.process(out, 2.2f);
        zOutDc += cOutDc * (out - zOutDc);
        return (out - zOutDc) * volumeGain;
    }
};

class BassFuzzPlugin : public Plugin
{
    BassBigMuff L, R;
    float fParams[kParamCount];

    void recalc()
    {
        L.setParams(fParams[kSustain], fParams[kTone], fParams[kVolume], fParams[kBassDry]);
        R.setParams(fParams[kSustain], fParams[kTone], fParams[kVolume], fParams[kBassDry]);
    }

public:
    BassFuzzPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            fParams[i] = kBassFuzzDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr);
        R.setSampleRate(sr);
        recalc();
    }

protected:
    const char* getLabel() const override { return "BassFuzz"; }
    const char* getDescription() const override { return "Bass Big Muff Pi fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'B', 'F', 'z'); }

    void initParameter(uint32_t i, Parameter& p) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassFuzzNames[i];
        p.symbol = kBassFuzzSymbols[i];
        p.ranges.min = kBassFuzzMin[i];
        p.ranges.max = kBassFuzzMax[i];
        p.ranges.def = kBassFuzzDef[i];
    }

    float getParameterValue(uint32_t i) const override
    {
        return (i < (uint32_t)kParamCount) ? fParams[i] : 0.0f;
    }

    void setParameterValue(uint32_t i, float v) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        fParams[i] = (i == (uint32_t)kBassDry) ? quantize3(v) : clamp01(v);
        recalc();
    }

    void sampleRateChanged(double r) override
    {
        L.setSampleRate((float)r);
        R.setSampleRate((float)r);
        recalc();
    }

    void run(const float** in, float** out, uint32_t frames) override
    {
        const float* iL = in[0];
        const float* iR = in[1];
        float* oL = out[0];
        float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            oL[i] = L.process(iL[i]);
            oR[i] = R.process(iR[i]);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFuzzPlugin)
};

Plugin* createPlugin() { return new BassFuzzPlugin(); }

END_NAMESPACE_DISTRHO
