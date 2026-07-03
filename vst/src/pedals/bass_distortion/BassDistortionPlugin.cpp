/*
 * BassDistortion - Pro Co RAT style model for Bass_Pedal_BassDistortion.
 *
 * Schematic blocks modeled here: input coupling, compensated LM308 high-gain
 * stage, anti-parallel 1N4148 clipping diodes, passive RAT Filter, JFET-ish
 * output buffer, and the real Distortion/Filter/Volume control set.
 */
#include "DistrhoPlugin.hpp"
#include "BassDistortionParams.h"
#include "../_shared/opamp.hpp"
#include "../_shared/semiconductors.hpp"
#include "../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float onePoleCoef(float fc, float fs)
{
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 2.05f);
}

class Rat
{
    float fs = 48000.0f;
    float zInput = 0.0f;
    float zFeedbackVoice = 0.0f;
    float zFilter = 0.0f;
    float zBuffer = 0.0f;
    float zOutDc = 0.0f;
    float cInput = 0.0f;
    float cFeedbackVoice = 0.0f;
    float cFilter = 0.0f;
    float cBuffer = 0.0f;
    float cOutDc = 0.0f;
    float driveGain = 95.0f;
    float outputGain = 0.85f;
    rbshared::OpAmpStage lm308;
    rbcomponents::AntiParallelDiodePair clipper;

public:
    Rat()
    {
        lm308.setSpec(rbshared::lm308Spec());
        clipper.setSpec(rbcomponents::diode1N4148());
        clipper.setSourceR(560.0f);
    }

    void setSampleRate(float s)
    {
        fs = s > 1000.0f ? s : 48000.0f;
        lm308.setSampleRate(fs);
        recalcFixed();
    }

    void recalcFixed()
    {
        cInput = onePoleCoef(24.0f, fs);
        cFeedbackVoice = onePoleCoef(3700.0f, fs);
        cBuffer = onePoleCoef(12500.0f, fs);
        cOutDc = onePoleCoef(10.0f, fs);
    }

    void setParams(float distortion, float filter, float volume)
    {
        const float d = audioTaper(distortion);
        // The old 38..1888 range slammed the LM308 into full clip even at the
        // Distortion knob's minimum, so the control was dead (identical RMS/tone
        // across its whole travel). 1.5..261 keeps the bottom of the pot near-clean
        // and reaches full RAT saturation by ~noon, so the knob actually sweeps.
        driveGain = 1.5f + 260.0f * d;
        const float dark = audioTaper(filter);
        const float cutoff = 6800.0f - 5850.0f * dark;
        cFilter = onePoleCoef(cutoff, fs);
        outputGain = 1.2f * audioTaper(volume);
    }

    float process(float x)
    {
        zInput += cInput * (x - zInput);
        float s = x - zInput;

        float driven = lm308.process(s * driveGain, driveGain);
        zFeedbackVoice += cFeedbackVoice * (driven - zFeedbackVoice);
        driven = 0.64f * driven + 0.36f * zFeedbackVoice;

        s = clipper.process(driven);
        zFilter += cFilter * (s - zFilter);
        s = zFilter;

        zBuffer += cBuffer * (s - zBuffer);
        s = std::tanh(zBuffer * 1.08f) * 0.92f;
        zOutDc += cOutDc * (s - zOutDc);
        return (s - zOutDc) * outputGain;
    }
};

class BassDistortionPlugin : public Plugin
{
    Rat L, R;
    rbshared::Oversampler4x osL, osR;
    static constexpr int kOS = rbshared::Oversampler4x::OS;
    float fParams[kParamCount];

    void recalc()
    {
        L.setParams(fParams[kDistortion], fParams[kFilter], fParams[kVolume]);
        R.setParams(fParams[kDistortion], fParams[kFilter], fParams[kVolume]);
    }

public:
    BassDistortionPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            fParams[i] = kBassDistortionDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(kOS * sr);
        R.setSampleRate(kOS * sr);
        recalc();
    }

protected:
    const char* getLabel() const override { return "BassDistortion"; }
    const char* getDescription() const override { return "Pro Co RAT distortion"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'B', 'D', 's'); }

    void initParameter(uint32_t i, Parameter& p) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassDistortionNames[i];
        p.symbol = kBassDistortionSymbols[i];
        p.ranges.min = kBassDistortionMin[i];
        p.ranges.max = kBassDistortionMax[i];
        p.ranges.def = kBassDistortionDef[i];
    }

    float getParameterValue(uint32_t i) const override
    {
        return (i < (uint32_t)kParamCount) ? fParams[i] : 0.0f;
    }

    void setParameterValue(uint32_t i, float v) override
    {
        if (i < (uint32_t)kParamCount)
        {
            fParams[i] = clamp01(v);
            recalc();
        }
    }

    void sampleRateChanged(double r) override
    {
        osL.reset();
        osR.reset();
        L.setSampleRate(kOS * (float)r);
        R.setSampleRate(kOS * (float)r);
        recalc();
    }

    void run(const float** in, float** out, uint32_t frames) override
    {
        const float* iL = in[0];
        const float* iR = in[1];
        float* oL = out[0];
        float* oR = out[1];
        float ubL[kOS];
        float ubR[kOS];
        for (uint32_t i = 0; i < frames; ++i)
        {
            osL.upsample(iL[i], ubL);
            osR.upsample(iR[i], ubR);
            for (int k = 0; k < kOS; ++k)
            {
                ubL[k] = L.process(ubL[k]);
                ubR[k] = R.process(ubR[k]);
            }
            oL[i] = osL.downsample(ubL);
            oR[i] = osR.downsample(ubR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassDistortionPlugin)
};

Plugin* createPlugin() { return new BassDistortionPlugin(); }

END_NAMESPACE_DISTRHO
