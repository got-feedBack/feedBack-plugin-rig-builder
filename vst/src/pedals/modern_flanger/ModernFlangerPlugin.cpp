/*
 * ModernFlanger - Moog MF-108M Cluster Flux style pedal.
 *
 * Local reference: pedals/modern flange.pdf. The model keeps the real panel:
 * Time, Range, Feedback, Drive, Output Level, Mix, LFO Shape, LFO Rate and
 * LFO Amount, with a
 * chained MN3009/MN3006/MN3007-style BBD path, LM13700-like wet/dry/feedback
 * VCAs, and the schematic's 12 kHz - 200 kHz clock range.
 */
#include "DistrhoPlugin.hpp"
#include "ModernFlangerParams.h"
#include "../_shared/FlangerComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static rbflanger::FlangerVoicing mf108mVoicing()
{
    rbflanger::FlangerVoicing v;
    v.bbd = rbflanger::mf108mChainSpec();
    v.opamp = rbshared::opa1644Spec();
    v.minDelayMs = 3.55f;
    v.maxDelayMs = 58.5f;
    v.minRateHz = 0.035f;
    v.maxRateHz = 12.0f;
    v.inputHpHz = 22.0f;
    v.inputLpHz = 6750.0f;
    v.bbdLpHz = 6500.0f;
    v.outputLpHz = 7360.0f;
    v.colorHpHz = 2600.0f;
    v.feedbackSign = -1.0f;
    v.wetSign = -1.0f;
    v.bbd.clockBleed = 0.00028f;
    v.bbd.noise = 0.000014f;
    v.delaySlewHz = 72.0f;
    v.feedbackMax = 0.70f;
    v.dryLevel = 0.90f;
    v.wetLevel = 0.82f;
    v.dryDucking = 0.24f;
    v.wetMixMin = 0.20f;
    v.wetMixScale = 0.82f;
    v.lfoTriangle = 0.36f;
    v.flangeRangeMaxMs = 18.5f;
    v.depthBase = 0.055f;
    v.depthScale = 0.54f;
    v.driveMinDb = -6.9f;
    v.driveMaxDb = 27.25f;
    v.outputMinDb = -31.3f;
    v.outputMaxDb = 4.1f;
    v.compander = 0.62f;
    return v;
}

} // namespace

class ModernFlangerPlugin : public Plugin
{
    rbflanger::AnalogBbdFlanger left;
    rbflanger::AnalogBbdFlanger right;
    float params[kParamCount];

    void applyAll()
    {
        left.setControls(params[kDelayTime], params[kLfoAmount], params[kLfoRate], params[kFeedback],
                         params[kMix], params[kDrive], params[kOutput], false,
                         params[kLfoShape], params[kRange]);
        right.setControls(params[kDelayTime], params[kLfoAmount], params[kLfoRate], params[kFeedback],
                          params[kMix], params[kDrive], params[kOutput], false,
                          params[kLfoShape], params[kRange]);
    }

public:
    ModernFlangerPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kModernFlangerDef[i];

        const rbflanger::FlangerVoicing voice = mf108mVoicing();
        left.setVoicing(voice);
        right.setVoicing(voice);
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.50f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ModernFlanger"; }
    const char* getDescription() const override { return "MF-108M Cluster Flux style BBD flanger"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'd', 'F', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kModernFlangerNames[index];
        parameter.symbol = kModernFlangerSymbols[index];
        parameter.ranges.min = kModernFlangerMin[index];
        parameter.ranges.max = kModernFlangerMax[index];
        parameter.ranges.def = kModernFlangerDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModernFlangerPlugin)
};

Plugin* createPlugin()
{
    return new ModernFlangerPlugin();
}

END_NAMESPACE_DISTRHO
