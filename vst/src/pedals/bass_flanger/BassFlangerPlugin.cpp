/*
 * BassFlanger / FL-3 - Boss BF-2B style bass flanger.
 *
 * Local reference: pedals/BOSS BF-2 BF-2B.pdf. The BF-2B page uses M5218P
 * audio opamps, TL022 LFO/clock support, MN3102 clock driver and MN3204
 * 512-stage BBD. The service notes specify a 0.5 ms - 6.5 ms delay sweep and
 * 100 ms - 16 s LFO period; the plugin exposes the real Manual, Depth, Rate
 * and Res controls and keeps dry/wet fixed like the hardware.
 */
#include "DistrhoPlugin.hpp"
#include "BassFlangerParams.h"
#include "../_shared/FlangerComponents.h"

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float value)
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static rbflanger::FlangerVoicing bf2bVoicing()
{
    rbflanger::FlangerVoicing v;
    v.bbd = rbflanger::mn3204Spec();
    v.opamp = rbshared::m5218Spec();
    v.minDelayMs = 0.52f;
    v.maxDelayMs = 6.45f;
    v.minRateHz = 0.0625f;
    v.maxRateHz = 10.0f;
    v.inputHpHz = 36.0f;
    v.inputLpHz = 7200.0f;
    v.bbdLpHz = 5350.0f;
    v.outputLpHz = 6400.0f;
    v.colorHpHz = 1550.0f;
    v.delaySlewHz = 92.0f;
    v.feedbackMax = 0.74f;
    v.feedbackSign = -1.0f;
    v.wetSign = -1.0f;
    v.dryLevel = 0.94f;
    v.wetLevel = 0.62f;
    v.dryDucking = 0.08f;
    v.wetMixMin = 0.22f;
    v.wetMixScale = 0.78f;
    v.lfoTriangle = 0.90f;
    v.flangeRangeMaxMs = 6.45f;
    v.depthBase = 0.030f;
    v.depthScale = 0.58f;
    v.driveMinDb = -1.2f;
    v.driveMaxDb = 4.4f;
    v.outputMinDb = -0.9f;
    v.outputMaxDb = 0.7f;
    v.compander = 0.55f;
    return v;
}

} // namespace

class BassFlangerPlugin : public Plugin
{
    rbflanger::AnalogBbdFlanger left;
    rbflanger::AnalogBbdFlanger right;
    float params[kParamCount];

    void applyAll()
    {
        left.setControls(params[kManual], params[kDepth], params[kRate], params[kRes],
                         0.86f, 0.36f, 0.58f, false);
        right.setControls(params[kManual], params[kDepth], params[kRate], params[kRes],
                          0.86f, 0.36f, 0.58f, false);
    }

public:
    BassFlangerPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBassFlangerDef[i];

        const rbflanger::FlangerVoicing voice = bf2bVoicing();
        left.setVoicing(voice);
        right.setVoicing(voice);
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.00f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BassFlanger"; }
    const char* getDescription() const override { return "Boss BF-2B style MN3204 BBD bass flanger"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'B', 'F', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBassFlangerNames[index];
        parameter.symbol = kBassFlangerSymbols[index];
        parameter.ranges.min = kBassFlangerMin[index];
        parameter.ranges.max = kBassFlangerMax[index];
        parameter.ranges.def = kBassFlangerDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFlangerPlugin)
};

Plugin* createPlugin()
{
    return new BassFlangerPlugin();
}

END_NAMESPACE_DISTRHO
