/*
 * ClassicFlanger - Boss BF-2 style flanger.
 *
 * Local reference: pedals/classic flanger.pdf. The BF-2 circuit is modeled as
 * uPC4558 input/output conditioning, MN3207/MN3102-family BBD delay, TL022 LFO
 * clock sweep, fixed dry/wet mix, and real Manual / Depth / Rate / Res controls.
 */
#include "DistrhoPlugin.hpp"
#include "ClassicFlangerParams.h"
#include "../_shared/FlangerComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static rbflanger::FlangerVoicing bf2Voicing()
{
    rbflanger::FlangerVoicing v;
    v.bbd = rbflanger::mn3207Bf2ClockedSpec();
    v.bbd.headroom = 0.35f;
    v.opamp = rbshared::upc4558Spec();
    v.minDelayMs = 1.0f;
    v.maxDelayMs = 13.0f;
    v.minRateHz = 0.0625f;
    v.maxRateHz = 10.0f;
    v.inputHpHz = 32.0f;
    v.inputLpHz = 6600.0f;
    v.bbdLpHz = 5100.0f;
    v.outputLpHz = 5900.0f;
    v.colorHpHz = 2400.0f;
    v.delaySlewHz = 48.0f;
    v.feedbackMax = 0.68f;
    v.feedbackSign = -1.0f;
    v.wetSign = 1.0f;
    v.dryLevel = 0.74f;
    v.wetLevel = 0.60f;
    v.lfoTriangle = 0.88f;
    v.driveMinDb = -0.4f;
    v.driveMaxDb = 1.2f;
    // The fixed BF-2 blend already matches the reference wet/dry ratio, but
    // the complete output was about 3 dB low at every measured control point.
    v.outputMinDb = 4.7f;
    v.outputMaxDb = 4.7f;
    v.depthBase = 0.01f;
    v.depthScale = 0.59f;
    v.compander = 0.0f;
    v.rateTaperExponent = 1.36f;
    v.manualTaperExponent = 0.90f;
    v.widthTaperExponent = 1.13f;
    v.depthCenterMinScale = 0.175f;
    v.depthCenterMaxScale = 2.16f;
    v.manualDepthScale = 0.14f;
    v.highRateDepthReduction = 0.50f;
    v.widthCenterShift = -0.35f;
    v.widthCenterShiftExponent = 1.50f;
    v.useCompander = false;
    v.linearPath = true;
    return v;
}

} // namespace

class ClassicFlangerPlugin : public Plugin
{
    rbflanger::AnalogBbdFlanger left;
    rbflanger::AnalogBbdFlanger right;
    float params[kParamCount];

    void applyAll()
    {
        left.setControls(params[kManual], params[kDepth], params[kRate], params[kRes],
                         1.0f, 0.34f, 0.56f, false);
        right.setControls(params[kManual], params[kDepth], params[kRate], params[kRes],
                          1.0f, 0.34f, 0.56f, false);
    }

public:
    ClassicFlangerPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kClassicFlangerDef[i];

        const rbflanger::FlangerVoicing voice = bf2Voicing();
        left.setVoicing(voice);
        right.setVoicing(voice);
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.00f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ClassicFlanger"; }
    const char* getDescription() const override { return "Boss BF-2 style MN3207 BBD flanger"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 3, 0); }
    int64_t getUniqueId() const override { return d_cconst('C', 'l', 'F', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kClassicFlangerNames[index];
        parameter.symbol = kClassicFlangerSymbols[index];
        parameter.ranges.min = kClassicFlangerMin[index];
        parameter.ranges.max = kClassicFlangerMax[index];
        parameter.ranges.def = kClassicFlangerDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassicFlangerPlugin)
};

Plugin* createPlugin()
{
    return new ClassicFlangerPlugin();
}

END_NAMESPACE_DISTRHO
