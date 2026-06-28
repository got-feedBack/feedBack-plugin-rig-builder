/*
 * EightiesFlanger - MXR M117 Flanger Original style pedal.
 *
 * Local reference: pedals/80s flanger.jpg. The schematic uses 4558 stages, a
 * SAD1024 BBD, transistor clock/current shaping, and real Manual / Width /
 * Speed / Regen controls.
 */
#include "DistrhoPlugin.hpp"
#include "EightiesFlangerParams.h"
#include "../_shared/FlangerComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static rbflanger::FlangerVoicing mxrVoicing()
{
    rbflanger::FlangerVoicing v;
    v.bbd = rbflanger::sad1024Spec();
    v.opamp = rbshared::jrc4558Spec();
    v.minDelayMs = 0.48f;
    v.maxDelayMs = 11.8f;
    v.minRateHz = 0.045f;
    v.maxRateHz = 8.5f;
    v.inputHpHz = 24.0f;
    v.inputLpHz = 7600.0f;
    v.bbdLpHz = 6100.0f;
    v.outputLpHz = 6500.0f;
    v.colorHpHz = 1800.0f;
    v.feedbackMax = 0.74f;
    v.feedbackSign = -1.0f;
    v.wetSign = -1.0f;
    v.dryLevel = 0.88f;
    v.wetLevel = 0.64f;
    v.lfoTriangle = 0.76f;
    v.driveMinDb = -0.2f;
    v.driveMaxDb = 2.2f;
    v.outputMinDb = -0.6f;
    v.outputMaxDb = 1.0f;
    v.compander = 0.34f;
    return v;
}

} // namespace

class EightiesFlangerPlugin : public Plugin
{
    rbflanger::AnalogBbdFlanger left;
    rbflanger::AnalogBbdFlanger right;
    float params[kParamCount];

    void applyAll()
    {
        left.setControls(params[kManual], params[kWidth], params[kSpeed], params[kRegen],
                         1.0f, 0.40f, 0.56f, false);
        right.setControls(params[kManual], params[kWidth], params[kSpeed], params[kRegen],
                          1.0f, 0.40f, 0.56f, false);
    }

public:
    EightiesFlangerPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kEightiesFlangerDef[i];

        const rbflanger::FlangerVoicing voice = mxrVoicing();
        left.setVoicing(voice);
        right.setVoicing(voice);
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.032f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "EightiesFlanger"; }
    const char* getDescription() const override { return "MXR M117/SAD1024 style BBD flanger"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('E', '8', 'F', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kEightiesFlangerNames[index];
        parameter.symbol = kEightiesFlangerSymbols[index];
        parameter.ranges.min = kEightiesFlangerMin[index];
        parameter.ranges.max = kEightiesFlangerMax[index];
        parameter.ranges.def = kEightiesFlangerDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EightiesFlangerPlugin)
};

Plugin* createPlugin()
{
    return new EightiesFlangerPlugin();
}

END_NAMESPACE_DISTRHO
