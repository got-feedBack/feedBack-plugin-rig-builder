/*
 * StudioChorus - Boss RCE-10 Chorus Ensemble model for Rack_StudioChorus.
 *
 * Reference: racks/RCE-10_service_notes.pdf.  The RCE-10 is not a BBD chorus:
 * the service note shows pre/de-emphasis around op-amp stages, an NE572
 * compressor/expander pair, 12-bit digital delay memory with R-2R conversion,
 * dual output expanders, LPF/de-emphasis and a dual effect-level control.
 */
#include "DistrhoPlugin.hpp"
#include "StudioChorusParams.h"
#include "../../_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float signedLogCompress(float x)
{
    const float sign = x < 0.0f ? -1.0f : 1.0f;
    const float ax = rbmod::clamp(std::fabs(x), 0.0f, 1.0f);
    return sign * std::log1p(8.0f * ax) / std::log1p(8.0f);
}

static inline float signedLogExpand(float x)
{
    const float sign = x < 0.0f ? -1.0f : 1.0f;
    const float ax = rbmod::clamp(std::fabs(x), 0.0f, 1.0f);
    return sign * std::expm1(ax * std::log1p(8.0f)) / 8.0f;
}

static inline float quantize12Bit(float x)
{
    x = rbmod::clamp(x, -0.985f, 0.985f);
    return std::floor(x * 2047.0f + (x >= 0.0f ? 0.5f : -0.5f)) / 2047.0f;
}

static inline float shapedRceLfo(float phase)
{
    phase -= std::floor(phase);
    const float tri = 4.0f * std::fabs(phase - 0.5f) - 1.0f;
    const float sine = std::sin(rbmod::kTwoPi * phase);
    return 0.66f * sine - 0.34f * tri;
}

} // namespace

class Rce10Core
{
    float sampleRate = 48000.0f;
    float rate = kStudioChorusDef[kRate];
    float depth = kStudioChorusDef[kDepth];
    float effectLevel = kStudioChorusDef[kMix];
    float effectEq = kStudioChorusDef[kEq];
    float preDelay = kStudioChorusDef[kDelay];
    float lfoPhase = 0.0f;
    float compEnv = 0.0f;
    float compCoeff = 0.0005f;
    float expEnvL = 0.0f;
    float expEnvR = 0.0f;
    float expCoeff = 0.0004f;

    rbmod::DelayBuffer memory;
    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass preEmphasisLp;
    rbmod::LowPass wetPreLpL;
    rbmod::LowPass wetPreLpR;
    rbmod::HighPass wetHpL;
    rbmod::HighPass wetHpR;
    rbmod::LowPass wetLpL;
    rbmod::LowPass wetLpR;
    rbmod::LowPass eqTiltLpL;
    rbmod::LowPass eqTiltLpR;
    rbmod::LowPass outputLpL;
    rbmod::LowPass outputLpR;
    rbmod::NoiseSource noise;

    float rateHz() const
    {
        // Keep the RS Rate-in-Hz mapping valid: normalized 0..1 = about 0.1..6 Hz.
        return 0.10f + 5.90f * rbmod::clamp01(rate);
    }

    void updateFilters()
    {
        const float eq = rbmod::smoothstep(effectEq);

        inputHp.setHz(16.0f, sampleRate);
        inputLp.setHz(14600.0f, sampleRate);
        preEmphasisLp.setHz(2250.0f + 900.0f * eq, sampleRate);

        wetPreLpL.setHz(12400.0f, sampleRate);
        wetPreLpR.setHz(12400.0f, sampleRate);
        // Fixed de-emphasis high-pass (the RCE-10 has no low-cut pot).
        wetHpL.setHz(45.0f, sampleRate);
        wetHpR.setHz(45.0f, sampleRate);
        wetLpL.setHz(5200.0f + 8800.0f * eq, sampleRate);
        wetLpR.setHz(5200.0f + 8800.0f * eq, sampleRate);
        eqTiltLpL.setHz(960.0f + 1900.0f * eq, sampleRate);
        eqTiltLpR.setHz(960.0f + 1900.0f * eq, sampleRate);
        outputLpL.setHz(15500.0f, sampleRate);
        outputLpR.setHz(15500.0f, sampleRate);
    }

    float adcPath(float x)
    {
        x = inputHp.process(x);
        x = inputLp.process(x);

        const float preLow = preEmphasisLp.process(x);
        x = rbmod::softClip((0.88f * x + 0.20f * (x - preLow)) * 1.045f);

        compEnv += compCoeff * (std::fabs(x) - compEnv);
        const float compressorGain = 1.0f / (0.62f + 2.25f * compEnv);
        x = signedLogCompress(rbmod::clamp(x * compressorGain * 0.62f, -0.98f, 0.98f));
        x = quantize12Bit(x);
        x += noise.next() * 0.000045f;
        return rbmod::clamp(x, -1.0f, 1.0f);
    }

    float dacPath(float x, bool right)
    {
        x = signedLogExpand(x);

        float& env = right ? expEnvR : expEnvL;
        env += expCoeff * (std::fabs(x) - env);
        x *= 0.70f + 1.65f * env;

        rbmod::LowPass& preLp = right ? wetPreLpR : wetPreLpL;
        rbmod::HighPass& hp = right ? wetHpR : wetHpL;
        rbmod::LowPass& lp = right ? wetLpR : wetLpL;
        rbmod::LowPass& tiltLp = right ? eqTiltLpR : eqTiltLpL;

        x = preLp.process(x);
        x = hp.process(x);
        x = lp.process(x);

        const float low = tiltLp.process(x);
        const float high = x - low;
        const float eq = rbmod::smoothstep(effectEq);
        x = low * (0.90f - 0.20f * eq) + high * (0.34f + 1.05f * eq);
        return rbmod::softClip(x * (1.07f + 0.09f * eq));
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        memory.resizeForMs(sampleRate, 72.0f);
        compCoeff = 1.0f - std::exp(-1.0f / (0.016f * sampleRate));
        expCoeff = 1.0f - std::exp(-1.0f / (0.020f * sampleRate));
        updateFilters();
        reset();
    }

    void reset()
    {
        memory.reset();
        inputHp.reset();
        inputLp.reset();
        preEmphasisLp.reset();
        wetPreLpL.reset();
        wetPreLpR.reset();
        wetHpL.reset();
        wetHpR.reset();
        wetLpL.reset();
        wetLpR.reset();
        eqTiltLpL.reset();
        eqTiltLpR.reset();
        outputLpL.reset();
        outputLpR.reset();
        lfoPhase = 0.0f;
        compEnv = 0.0f;
        expEnvL = 0.0f;
        expEnvR = 0.0f;
    }

    void setParams(float newRate, float newDepth, float newEffectLevel,
                   float newEffectEq, float newPreDelay)
    {
        rate = rbmod::clamp01(newRate);
        depth = rbmod::clamp01(newDepth);
        effectLevel = rbmod::clamp01(newEffectLevel);
        effectEq = rbmod::clamp01(newEffectEq);
        preDelay = rbmod::clamp01(newPreDelay);
        updateFilters();
    }

    void setSeed(unsigned int seed)
    {
        noise.seed(seed);
    }

    void process(float inL, float inR, float& outL, float& outR)
    {
        const rbmod::StereoInputPair feed = rbmod::stereoPedalFeeds(inL, inR);
        const float dryL = feed.left;
        const float dryR = feed.right;
        const float mono = 0.5f * (dryL + dryR);

        lfoPhase += rateHz() / sampleRate;
        if (lfoPhase >= 1.0f)
            lfoPhase -= std::floor(lfoPhase);

        const float d = std::pow(rbmod::clamp01(depth), 1.45f);
        const float timeTaper = std::pow(rbmod::clamp01(preDelay), 1.18f);
        const float baseMs = 6.8f + 31.0f * timeTaper;
        float sweepMs = 0.10f + 8.60f * d;
        if (sweepMs > baseMs - 3.0f)
            sweepMs = baseMs - 3.0f;

        const float width = 0.60f;   // RCE-10 A/B stereo image is fixed (no width pot)
        const float phaseSpread = 0.07f + 0.43f * width;
        const float lfoL = shapedRceLfo(lfoPhase);
        const float lfoR = shapedRceLfo(lfoPhase + phaseSpread);
        const float auxL = shapedRceLfo(lfoPhase + 0.19f + 0.09f * width);
        const float auxR = shapedRceLfo(lfoPhase + phaseSpread + 0.23f);

        const float delayL = rbmod::clamp(baseMs + sweepMs * (0.80f * lfoL + 0.20f * auxL), 2.0f, 62.0f);
        const float delayR = rbmod::clamp(baseMs + sweepMs * (0.80f * lfoR + 0.20f * auxR), 2.0f, 62.0f);

        const float encoded = adcPath(mono);
        const float tapL = memory.read(delayL * 0.001f * sampleRate);
        const float tapR = memory.read(delayR * 0.001f * sampleRate);
        memory.write(encoded);

        float wetL = dacPath(tapL, false);
        float wetR = dacPath(tapR, true);

        // Output A/B on the service note are not hard-panned dry/wet.  At narrow
        // width the wet paths collapse; clockwise width leaves the two delay
        // phases mostly independent.
        const float collapse = 0.5f * (1.0f - width);
        const float monoWet = 0.5f * (wetL + wetR);
        wetL = wetL * (1.0f - collapse) + monoWet * collapse;
        wetR = wetR * (1.0f - collapse) + monoWet * collapse;

        const float level = std::pow(effectLevel, 1.28f);
        const float dryGain = 0.91f - 0.035f * level;
        const float wetGain = 0.015f + 1.05f * level;
        outL = outputLpL.process(rbmod::softClip(dryL * dryGain + wetL * wetGain) * 0.985f);
        outR = outputLpR.process(rbmod::softClip(dryR * dryGain + wetR * wetGain) * 0.985f);
    }
};

class StudioChorusPlugin : public Plugin
{
    Rce10Core core;
    float fParams[kParamCount];

    void recalc()
    {
        core.setParams(fParams[kRate], fParams[kDepth], fParams[kMix],
                       fParams[kEq], fParams[kDelay]);
    }

public:
    StudioChorusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            fParams[i] = kStudioChorusDef[i];
        core.setSeed(0x52434531u);
        core.setSampleRate(48000.0f);
        recalc();
    }

protected:
    const char* getLabel() const override { return "StudioChorus"; }
    const char* getDescription() const override { return "Boss RCE-10 12-bit digital chorus ensemble"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'B', 'S', 'c'); }

    void initParameter(uint32_t i, Parameter& p) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        p.hints = kParameterIsAutomatable;
        p.name = kStudioChorusNames[i];
        p.symbol = kStudioChorusSymbols[i];
        p.ranges.min = kStudioChorusMin[i];
        p.ranges.max = kStudioChorusMax[i];
        p.ranges.def = kStudioChorusDef[i];
    }

    float getParameterValue(uint32_t i) const override
    {
        return (i < (uint32_t)kParamCount) ? fParams[i] : 0.0f;
    }

    void setParameterValue(uint32_t i, float v) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        fParams[i] = rbmod::clamp01(v);
        recalc();
    }

    void sampleRateChanged(double r) override
    {
        core.setSampleRate((float)r);
        recalc();
    }

    void run(const float** in, float** out, uint32_t frames) override
    {
        const float* iL = in[0];
        const float* iR = in[1];
        float* oL = out[0];
        float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i)
            core.process(iL[i], iR[i], oL[i], oR[i]);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioChorusPlugin)
};

Plugin* createPlugin()
{
    return new StudioChorusPlugin();
}

END_NAMESPACE_DISTRHO
