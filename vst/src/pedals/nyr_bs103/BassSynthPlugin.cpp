/*
 * NYR BS103 — monophonic bass synth (MVP).
 *
 * Original design. NOT a clone of any circuit — pitch-tracking bass synths are
 * all-digital, so there is no schematic to model; this recreates the *function*
 * with original DSP and an original name/face (no real brand/model/preset names).
 *
 * The DSP lives in BassSynthCore.h (no DPF dependency, unit-testable offline).
 * This file is just the DPF wrapper: two per-channel cores (the host engine is
 * dual-mono, so both get identical input and produce identical, mono-compatible
 * output) plus parameter plumbing.
 */
#include "DistrhoPlugin.hpp"
#include "BassSynthParams.h"
#include "BassSynthCore.h"

START_NAMESPACE_DISTRHO

class BassSynthPlugin : public Plugin
{
    bs103::BassSynthCore left;
    bs103::BassSynthCore right;
    float params[kParamCount];

    void applyAll()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            bs103::BassSynthCore& c = ch == 0 ? left : right;
            c.setMix(params[kMix]);
            c.setSub(params[kSub]);
            c.setCutoff(params[kCutoff]);
            c.setResonance(params[kResonance]);
            c.setEnvelope(params[kEnvelope]);
            c.setShape(params[kShape]);
            c.setVoice(params[kVoice]);
            c.setMod(params[kMod]);
            c.setLevel(params[kLevel]);
        }
    }

public:
    BassSynthPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBassSynthDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "NYR BS103"; }
    const char* getDescription() const override { return "Monophonic bass synth"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'S', '3', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBassSynthNames[index];
        parameter.symbol = kBassSynthSymbols[index];
        parameter.ranges.min = kBassSynthMin[index];
        parameter.ranges.max = kBassSynthMax[index];
        parameter.ranges.def = kBassSynthDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = bs103::clamp01(value);
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
            outL[i] = left.process(inL[i]);
            outR[i] = right.process(inR[i]);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassSynthPlugin)
};

Plugin* createPlugin()
{
    return new BassSynthPlugin();
}

END_NAMESPACE_DISTRHO
