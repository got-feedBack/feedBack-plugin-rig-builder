/*
 * VintageFlanger - Deluxe Electric Mistress style pedal.
 *
 * Local reference: pedals/vintage flanger.jpg. The model follows the LM324
 * input/LFO blocks, RD5106A-family BBD delay, CD4013 clock division, fixed
 * blend path, Color feedback, Range sweep and Filter Matrix freeze switch.
 */
#include "DistrhoPlugin.hpp"
#include "VintageFlangerParams.h"
#include "../_shared/FlangerComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static rbflanger::FlangerVoicing mistressVoicing()
{
    rbflanger::FlangerVoicing v;
    v.bbd = rbflanger::rd5106aSpec();
    v.opamp = rbshared::lm324Spec();
    v.minDelayMs = 0.55f;
    v.maxDelayMs = 12.8f;
    v.minRateHz = 0.045f;
    v.maxRateHz = 6.2f;
    v.inputHpHz = 28.0f;
    v.inputLpHz = 6900.0f;
    v.bbdLpHz = 5200.0f;
    v.outputLpHz = 5700.0f;
    v.colorHpHz = 2200.0f;
    v.feedbackMax = 0.70f;
    v.feedbackSign = -1.0f;
    v.wetSign = -1.0f;
    v.dryLevel = 0.88f;
    v.wetLevel = 0.62f;
    v.lfoTriangle = 0.84f;
    v.driveMinDb = -0.5f;
    v.driveMaxDb = 1.6f;
    v.outputMinDb = -0.8f;
    v.outputMaxDb = 0.7f;
    v.compander = 0.30f;
    return v;
}

} // namespace

class VintageFlangerPlugin : public Plugin
{
    rbflanger::AnalogBbdFlanger left;
    rbflanger::AnalogBbdFlanger right;
    float params[kParamCount];

    void applyAll()
    {
        const bool matrix = params[kMatrix] >= 0.5f;
        left.setControls(0.44f, params[kRange], params[kRate], params[kColor],
                         1.0f, 0.36f, 0.55f, matrix);
        right.setControls(0.44f, params[kRange], params[kRate], params[kColor],
                          1.0f, 0.36f, 0.55f, matrix);
    }

public:
    VintageFlangerPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kVintageFlangerDef[i];

        const rbflanger::FlangerVoicing voice = mistressVoicing();
        left.setVoicing(voice);
        right.setVoicing(voice);
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.045f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "VintageFlanger"; }
    const char* getDescription() const override { return "Deluxe Electric Mistress style BBD flanger"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('V', 't', 'F', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kVintageFlangerNames[index];
        parameter.symbol = kVintageFlangerSymbols[index];
        parameter.ranges.min = kVintageFlangerMin[index];
        parameter.ranges.max = kVintageFlangerMax[index];
        parameter.ranges.def = kVintageFlangerDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VintageFlangerPlugin)
};

Plugin* createPlugin()
{
    return new VintageFlangerPlugin();
}

END_NAMESPACE_DISTRHO
