/*
 * ModernFlanger - Moog MF-108M Cluster Flux style pedal.
 *
 * Local reference: pedals/modern flange.pdf. The model keeps the real panel:
 * Time, Range, Feedback, Drive, Output Level, Mix, LFO Shape, LFO Rate and
 * LFO Amount, with a
 * MN3009/MN3007 BBD path, LM13700-like wet/dry/feedback VCAs, and the
 * schematic's 12 kHz - 200 kHz clock range. The optional MN3006 shown on the
 * production drawing is marked NOPOP and is not included in the stage count.
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
    v.opamp = rbshared::tl074aSpec();
    v.minDelayMs = 0.60f;
    v.maxDelayMs = 10.0f;
    v.alternateMinDelayMs = 5.0f;
    v.alternateMaxDelayMs = 50.0f;
    v.minRateHz = 0.05f;
    v.maxRateHz = 50.0f;
    v.inputHpHz = 22.0f;
    v.inputLpHz = 10000.0f;
    v.bbdLpHz = 10000.0f;
    v.outputLpHz = 12200.0f;
    v.colorHpHz = 2600.0f;
    v.feedbackColorBase = 0.0f;
    v.feedbackColorScale = 0.0f;
    v.feedbackSign = 1.0f;
    v.wetSign = -1.0f;
    v.bbd.clockBleed = 0.00028f;
    v.bbd.noise = 0.000014f;
    v.delaySlewHz = 72.0f;
    v.feedbackMax = 0.90f;
    v.dryLevel = 0.90f;
    v.wetLevel = 0.82f;
    v.dryDucking = 0.24f;
    v.wetMixMin = 0.0f;
    v.wetMixScale = 1.0f;
    v.lfoTriangle = 0.36f;
    v.flangeRangeMaxMs = 10.0f;
    v.depthBase = 0.0f;
    v.depthScale = 0.357f;
    v.driveMinDb = -6.9f;
    v.driveMaxDb = 27.25f;
    v.outputMinDb = -31.3f;
    v.outputMaxDb = 4.1f;
    v.outputTaperExponent = 3.2f;
    v.mixTaperExponent = 1.48f;
    v.outputCalibrationDb = 1.88f;
    v.feedbackExtremeOutputDb = 2.4f;
    v.alternateRangeOutputDb = -0.2f;
    v.positiveFeedbackScale = 1.011f;
    v.feedbackRunaway = 0.15f;
    v.compander = 0.62f;
    v.useCompander = false;
    v.rateTaperExponent = 1.0f;
    v.rateKnee1Input = 0.2007f;
    v.rateKnee1Output = 0.0f;
    v.rateKnee2Input = 0.4337f;
    v.rateKnee2Output = 0.3742f;
    v.rateKnee3Input = 0.7934f;
    v.rateKnee3Output = 0.7917f;
    v.manualTaperExponent = 0.75f;
    v.alternateManualTaperExponent = 0.51f;
    v.negativeFeedbackExponent = 2.6f;
    v.positiveFeedbackExponent = 1.7f;
    v.highRateDepthReduction = 0.55f;
    v.highRateDepthExponent = 12.0f;
    v.widthCenterBellShift = -0.15f;
    v.manualIncreasesDelay = true;
    v.bipolarFeedback = true;
    v.trueCrossfade = true;
    v.directDelayTarget = true;
    v.feedbackAfterOutputFilter = true;
    v.linearPath = true;
    v.shortRangeBuckets = 256;
    v.longRangeBuckets = 1280;
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
        left.setPhaseOffset(0.187f);
        right.setPhaseOffset(0.187f);
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ModernFlanger"; }
    const char* getDescription() const override { return "MF-108M Cluster Flux style BBD flanger"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 5, 0); }
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
