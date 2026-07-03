/*
 * StudioDelay - Boss RDD-10/RDD-20 style digital rack delay.
 *
 * Reference: racks/RDD-10_-_RDD-20_service_notes.pdf.  The service notes show
 * the signal path as input/pre-emphasis -> LPF -> uPC1571C compressor ->
 * A/D and 64K DRAM controlled by an RDD63H101P main controller/SN74LS628N VCO
 * -> expander -> tone/de-emphasis -> mixer.  Delay range is switched at
 * 1.5/3/6/12.5/25/50/100/200/400 ms, with a front panel fine control, feedback,
 * tone and delay level.
 *
 * the game only exposes TimeL, TimeR, Feedback, Filter and Mix, so this keeps
 * the public rack contract and models the missing Rate/Depth/Range controls as
 * fixed internal rack defaults.  TimeL/TimeR arrive normalized by the mapping
 * from milliseconds/700, then get clamped to the RDD's 0.75..400 ms range.
 */
#include "DistrhoPlugin.hpp"
#include "StudioDelayParams.h"
#include "../../pedals/_shared/ChorusComponents.h"
#include "../../pedals/_shared/opamp.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return rbmod::clamp01(v);
}

static inline float smoothstep(float v)
{
    return rbmod::smoothstep(v);
}

static inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float onePoleCoeffMs(float ms, float sr)
{
    const float safeSr = sr > 1000.0f ? sr : 48000.0f;
    return 1.0f - std::exp(-1.0f / std::fmax(1.0f, ms * 0.001f * safeSr));
}

static inline float logInterp(float lo, float hi, float t)
{
    return lo * std::pow(hi / lo, clamp01(t));
}

static inline float quantize(float x, float steps)
{
    x = rbmod::clamp(x, -0.985f, 0.985f);
    return std::floor(x * steps + (x >= 0.0f ? 0.5f : -0.5f)) / steps;
}

static float normTimeToRddMs(float normalized)
{
    // Existing RS mapping stores milliseconds as ms / 700.  Preserve that
    // behavior so a song value of 320 ms remains close to 320 ms here.
    return rbmod::clamp(clamp01(normalized) * 700.0f, 0.75f, 400.0f);
}

static float rddRangeAlignedMs(float wantedMs)
{
    static const float ranges[] = { 1.5f, 3.0f, 6.0f, 12.5f, 25.0f, 50.0f, 100.0f, 200.0f, 400.0f };
    float selected = ranges[8];
    for (float range : ranges)
    {
        if (wantedMs <= range)
        {
            selected = range;
            break;
        }
    }

    const float fine = rbmod::clamp(wantedMs / selected, 0.50f, 1.0f);
    return selected * fine;
}

class RddCompander
{
    float sampleRate = 48000.0f;
    float compEnv = 0.0f;
    float expEnv = 0.0f;
    float compA = 0.0f;
    float expA = 0.0f;

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        compA = onePoleCoeffMs(5.0f, sampleRate);
        expA = onePoleCoeffMs(18.0f, sampleRate);
        reset();
    }

    void reset()
    {
        compEnv = 0.0f;
        expEnv = 0.0f;
    }

    float compress(float x)
    {
        compEnv += compA * (std::fabs(x) - compEnv);
        const float gain = 1.18f / (1.0f + 1.65f * compEnv);
        return rbmod::softClip(x * gain);
    }

    float expand(float x)
    {
        expEnv += expA * (std::fabs(x) - expEnv);
        const float gate = expEnv / (expEnv + 0.012f);
        const float gain = 0.76f + 0.36f * smoothstep(gate);
        return rbmod::softClip(x * gain);
    }
};

class RddDelayChannel
{
    float sampleRate = 48000.0f;
    float delayMs = 240.0f;
    float smoothedDelayMs = 240.0f;
    float feedback = 0.28f;
    float tone = 0.55f;
    float sideOffset = 0.0f;
    float lfoPhase = 0.0f;
    float clockPhase = 0.0f;
    float lastFeedback = 0.0f;

    rbshared::OpAmpStage inputAmp;
    rbshared::OpAmpStage outputAmp;
    rbmod::HighPass inputHp;
    rbmod::LowPass antiAliasLp;
    rbmod::LowPass preEmphasisLp;
    rbmod::LowPass memoryLp;
    rbmod::HighPass delayHp;
    rbmod::LowPass toneLp;
    rbmod::LowPass deEmphasisLp;
    rbmod::DelayBuffer delayLine;
    rbmod::NoiseSource noise;
    RddCompander compander;

    void updateFilters()
    {
        const float delayN = rbmod::clamp((delayMs - 0.75f) / (400.0f - 0.75f), 0.0f, 1.0f);
        const float toneCurve = smoothstep(tone);

        inputHp.setHz(10.0f, sampleRate);
        antiAliasLp.setHz(14500.0f - 2400.0f * delayN, sampleRate);
        preEmphasisLp.setHz(1450.0f, sampleRate);
        memoryLp.setHz(14200.0f - 3400.0f * delayN, sampleRate);
        delayHp.setHz(20.0f, sampleRate);
        toneLp.setHz(logInterp(850.0f, 15000.0f, toneCurve) * (1.0f - 0.16f * delayN), sampleRate);
        deEmphasisLp.setHz(3800.0f + 6200.0f * toneCurve, sampleRate);
    }

public:
    void setSampleRate(float sr, unsigned int seed)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        inputAmp.setSpec(rbshared::m5218Spec());
        outputAmp.setSpec(rbshared::m5218Spec());
        inputAmp.setSampleRate(sampleRate);
        outputAmp.setSampleRate(sampleRate);
        delayLine.resizeForMs(sampleRate, 430.0f);
        noise.seed(seed);
        compander.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        smoothedDelayMs = delayMs;
        lfoPhase = 0.0f;
        clockPhase = 0.0f;
        lastFeedback = 0.0f;
        inputAmp.reset();
        outputAmp.reset();
        inputHp.reset();
        antiAliasLp.reset();
        preEmphasisLp.reset();
        memoryLp.reset();
        delayHp.reset();
        toneLp.reset();
        deEmphasisLp.reset();
        delayLine.reset();
        compander.reset();
        updateFilters();
    }

    void setControls(float timeNorm, float feedbackNorm, float filterNorm, float side)
    {
        delayMs = rddRangeAlignedMs(normTimeToRddMs(timeNorm));
        feedback = 0.02f + 0.78f * smoothstep(feedbackNorm);
        tone = clamp01(filterNorm);
        sideOffset = side;
        updateFilters();
    }

    float processWet(float in)
    {
        const float conditioned = inputAmp.process(inputHp.process(in) * dbToGain(1.2f), 1.7f);
        const float preLow = preEmphasisLp.process(conditioned);
        const float preEmphasis = antiAliasLp.process(conditioned + 0.42f * (conditioned - preLow));
        const float encoded = compander.compress(preEmphasis);

        const float smoothA = onePoleCoeffMs(18.0f, sampleRate);
        smoothedDelayMs += smoothA * (delayMs - smoothedDelayMs);

        const float delayN = rbmod::clamp((smoothedDelayMs - 0.75f) / (400.0f - 0.75f), 0.0f, 1.0f);
        // The RDD is a CLEAN digital delay: its MODULATION Depth defaults to 0, so
        // repeats are pitch-stable (no tape-style wow). Keeping a wow LFO here made
        // long feedback tails slowly detune/chorus, which is wrong for this unit.
        const float readMs = smoothedDelayMs;

        float wet = delayLine.read(readMs * sampleRate * 0.001f);
        wet = memoryLp.process(wet);

        // Model the digital memory/clock as quantization plus very small clock
        // feedthrough that becomes more audible at longer delay ranges.
        const float clockHz = rbmod::clamp(40000.0f * (400.0f / std::fmax(6.0f, smoothedDelayMs)), 40000.0f, 82500.0f);
        clockPhase += clockHz / sampleRate;
        clockPhase -= std::floor(clockPhase);
        wet = quantize(wet, 4095.0f);
        wet += std::sin(rbmod::kTwoPi * clockPhase) * (0.000010f + 0.000030f * delayN);
        wet += noise.next() * (0.000010f + 0.000026f * delayN);

        float memoryInput = encoded + lastFeedback * feedback;
        memoryInput = rbmod::softClip(memoryInput * (0.92f + 0.16f * feedback));
        memoryInput = quantize(memoryInput, 4095.0f);
        delayLine.write(memoryInput);

        wet = compander.expand(wet);
        wet = delayHp.process(wet);
        wet = toneLp.process(wet);
        const float de = deEmphasisLp.process(wet);
        wet = de * (0.82f + 0.10f * tone) + wet * (0.10f + 0.08f * tone);
        wet = outputAmp.process(wet * 1.18f, 1.4f);
        wet = rbmod::softClip(wet);

        lastFeedback = wet;
        return wet;
    }
};

} // namespace

class StudioDelayPlugin : public Plugin
{
    RddDelayChannel left;
    RddDelayChannel right;
    float params[kParamCount];
    float mix = kStudioDelayDef[kMix];

    void applyAll()
    {
        left.setControls(params[kTimeL], params[kFeedback], params[kFilter], 0.0f);
        right.setControls(params[kTimeR], params[kFeedback], params[kFilter], 1.0f);
        mix = clamp01(params[kMix]);
    }

public:
    StudioDelayPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kStudioDelayDef[i];

        left.setSampleRate(48000.0f, 0x52dd10u);
        right.setSampleRate(48000.0f, 0x52dd20u);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "StudioDelay"; }
    const char* getDescription() const override { return "Boss RDD-10/RDD-20 style digital delay"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'D', 'l', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;

        parameter.hints = kParameterIsAutomatable;
        parameter.name = kStudioDelayNames[index];
        parameter.symbol = kStudioDelaySymbols[index];
        parameter.ranges.min = kStudioDelayMin[index];
        parameter.ranges.max = kStudioDelayMax[index];
        parameter.ranges.def = kStudioDelayDef[index];
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
        left.setSampleRate((float)newSampleRate, 0x52dd10u);
        right.setSampleRate((float)newSampleRate, 0x52dd20u);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float m = smoothstep(mix);
        const float dryGain = 1.0f - 0.74f * m;
        const float wetGain = 0.92f * m;

        for (uint32_t i = 0; i < frames; ++i)
        {
            const rbmod::StereoInputPair feed = rbmod::stereoPedalFeeds(inputs[0][i], inputs[1][i]);
            const float wetL = left.processWet(feed.left);
            const float wetR = right.processWet(feed.right);
            outputs[0][i] = feed.left * dryGain + wetL * wetGain;
            outputs[1][i] = feed.right * dryGain + wetR * wetGain;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioDelayPlugin)
};

Plugin* createPlugin()
{
    return new StudioDelayPlugin();
}

END_NAMESPACE_DISTRHO
