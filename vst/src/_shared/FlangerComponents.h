#ifndef RB_PEDAL_FLANGER_COMPONENTS_H
#define RB_PEDAL_FLANGER_COMPONENTS_H

#include "ChorusComponents.h"
#include "opamp.hpp"
#include <algorithm>
#include <cmath>

namespace rbflanger {

struct BbdChipSpec
{
    int buckets;
    float clockMinHz;
    float clockMaxHz;
    float delayClockFactor;
    float headroom;
    float clockBleed;
    float noise;
};

static inline BbdChipSpec mn3207Spec()
{
    return { 1024, 10000.0f, 200000.0f, 0.5f, 0.92f, 0.0010f, 0.000030f };
}

static inline BbdChipSpec mn3207Bf2ClockedSpec()
{
    return { 1024, 40000.0f, 500000.0f, 0.5f, 0.92f, 0.0011f, 0.000030f };
}

static inline BbdChipSpec mn3204Spec()
{
    return { 512, 40000.0f, 500000.0f, 0.5f, 0.90f, 0.0009f, 0.000032f };
}

static inline BbdChipSpec sad1024Spec()
{
    return { 1024, 1500.0f, 1500000.0f, 0.5f, 1.05f, 0.0014f, 0.000035f };
}

static inline BbdChipSpec rd5106aSpec()
{
    return { 256, 500.0f, 1000000.0f, 2.0f, 0.88f, 0.0012f, 0.000040f };
}

static inline BbdChipSpec mn3006Spec()
{
    return { 128, 10000.0f, 200000.0f, 0.5f, 1.10f, 0.0006f, 0.000011f };
}

static inline BbdChipSpec mn3007Spec()
{
    return { 1024, 10000.0f, 100000.0f, 0.5f, 1.10f, 0.0008f, 0.000018f };
}

static inline BbdChipSpec mn3009Spec()
{
    return { 256, 10000.0f, 200000.0f, 0.5f, 1.14f, 0.0006f, 0.000009f };
}

static inline BbdChipSpec mf108mChainSpec()
{
    return { 1408, 12000.0f, 200000.0f, 0.5f, 1.12f, 0.0007f, 0.000025f };
}

static inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float logInterp(float lo, float hi, float t)
{
    t = rbmod::clamp01(t);
    return std::exp(std::log(lo) + (std::log(hi) - std::log(lo)) * t);
}

class BucketBrigadeLine
{
    rbmod::DelayBuffer delay;
    rbmod::NoiseSource noise;
    BbdChipSpec spec = mn3207Spec();
    float sampleRate = 48000.0f;
    float lp1 = 0.0f;
    float lp2 = 0.0f;
    float clockPhase = 0.0f;

public:
    void setSpec(const BbdChipSpec& s)
    {
        spec = s;
    }

    void setSampleRate(float sr, float maxDelayMs)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        delay.resizeForMs(sampleRate, maxDelayMs + 8.0f);
        noise.seed((unsigned int)spec.buckets * 2654435761u);
        reset();
    }

    void reset()
    {
        delay.reset();
        lp1 = 0.0f;
        lp2 = 0.0f;
        clockPhase = 0.0f;
    }

    float clockForDelayMs(float delayMs) const
    {
        const float seconds = rbmod::clamp(delayMs, 0.1f, 80.0f) * 0.001f;
        return rbmod::clamp(spec.delayClockFactor * (float)spec.buckets / seconds, spec.clockMinHz, spec.clockMaxHz);
    }

    float process(float input, float clockHz, float postFilterHz, float grit, float bleed)
    {
        clockHz = rbmod::clamp(clockHz, spec.clockMinHz, spec.clockMaxHz);
        const float delaySamples = (spec.delayClockFactor * (float)spec.buckets * sampleRate) / clockHz;

        const float driven = spec.headroom * std::tanh(input / spec.headroom);
        const float tap = delay.read(delaySamples);
        delay.write(driven);

        const float bbdBandwidth = rbmod::clamp(std::fmin(postFilterHz, clockHz * 0.42f), 900.0f, sampleRate * 0.42f);
        const float a = rbmod::onePoleCoeffHz(bbdBandwidth, sampleRate);
        lp1 += a * (tap - lp1);
        lp2 += a * (lp1 - lp2);

        clockPhase += clockHz / sampleRate;
        clockPhase -= std::floor(clockPhase);

        const float clockFade = rbmod::clamp((sampleRate * 0.45f - clockHz) / (sampleRate * 0.08f), 0.0f, 1.0f);
        const float clockLeak = spec.clockBleed * bleed * clockFade * std::sin(rbmod::kTwoPi * clockPhase);
        const float hiss = spec.noise * (0.3f + 0.7f * grit) * noise.next();
        return spec.headroom * std::tanh((lp2 + clockLeak + hiss) / spec.headroom);
    }
};

struct FlangerVoicing
{
    BbdChipSpec bbd = mn3207Spec();
    rbshared::OpAmpSpec opamp = rbshared::upc4558Spec();
    float minDelayMs = 1.0f;
    float maxDelayMs = 13.0f;
    float minRateHz = 0.0625f;
    float maxRateHz = 10.0f;
    float inputHpHz = 28.0f;
    float inputLpHz = 6800.0f;
    float bbdLpHz = 5200.0f;
    float outputLpHz = 6200.0f;
    float colorHpHz = 2300.0f;
    float delaySlewHz = 0.0f;
    float feedbackMax = 0.62f;
    float feedbackSign = -1.0f;
    float wetSign = -1.0f;
    float dryLevel = 0.88f;
    float wetLevel = 0.58f;
    float dryDucking = 0.18f;
    float wetMixMin = 0.18f;
    float wetMixScale = 0.82f;
    float lfoTriangle = 0.82f;
    float flangeRangeMaxMs = 0.0f;
    float depthBase = 0.04f;
    float depthScale = 0.48f;
    float driveMinDb = -0.5f;
    float driveMaxDb = 1.5f;
    float outputMinDb = -1.0f;
    float outputMaxDb = 1.0f;
    float compander = 0.45f;
};

class AnalogBbdFlanger
{
    FlangerVoicing voice;
    BucketBrigadeLine bbd;
    rbshared::OpAmpStage inputOpamp;
    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass colorLp;
    rbmod::LowPass outputLp1;
    rbmod::LowPass outputLp2;
    rbmod::BbdCompander compander;

    float sampleRate = 48000.0f;
    float phase = 0.0f;
    float phaseOffset = 0.0f;
    float manual = 0.5f;
    float width = 0.45f;
    float rate = 0.25f;
    float feedback = 0.25f;
    float mix = 1.0f;
    float drive = 0.35f;
    float output = 0.5f;
    float lfoShapeControl = -1.0f;
    float rangeControl = 0.0f;
    bool frozen = false;
    float feedbackState = 0.0f;
    float smoothedDelayMs = 0.0f;

    float lfoShape(float p) const
    {
        p -= std::floor(p);
        const float tri = 4.0f * std::fabs(p - 0.5f) - 1.0f;
        const float sine = std::sin(rbmod::kTwoPi * p);
        if (lfoShapeControl < 0.0f)
            return voice.lfoTriangle * tri + (1.0f - voice.lfoTriangle) * sine;

        const float shape = rbmod::clamp01(lfoShapeControl);
        const float rampUp = 2.0f * p - 1.0f;
        const float rampDown = 1.0f - 2.0f * p;
        const float square = p < 0.5f ? 1.0f : -1.0f;
        const float stepped = std::floor((p * 8.0f)) / 3.5f - 1.0f;

        if (shape < 0.20f)
            return sine;
        if (shape < 0.40f)
            return tri;
        if (shape < 0.60f)
            return rampUp;
        if (shape < 0.80f)
            return rampDown;
        return 0.55f * square + 0.45f * stepped;
    }

public:
    void setVoicing(const FlangerVoicing& v)
    {
        voice = v;
        inputOpamp.setSpec(voice.opamp);
        bbd.setSpec(voice.bbd);
        setSampleRate(sampleRate);
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        bbd.setSampleRate(sampleRate, voice.maxDelayMs);
        inputOpamp.setSampleRate(sampleRate);
        inputHp.setHz(voice.inputHpHz, sampleRate);
        inputLp.setHz(voice.inputLpHz, sampleRate);
        colorLp.setHz(voice.colorHpHz, sampleRate);
        outputLp1.setHz(voice.outputLpHz, sampleRate);
        outputLp2.setHz(voice.outputLpHz * 0.82f, sampleRate);
        compander.setSampleRate(sampleRate, 24.0f);
        reset();
    }

    void setPhaseOffset(float offset)
    {
        phaseOffset = offset - std::floor(offset);
        phase = phaseOffset;
    }

    void reset()
    {
        bbd.reset();
        inputOpamp.reset();
        inputHp.reset();
        inputLp.reset();
        colorLp.reset();
        outputLp1.reset();
        outputLp2.reset();
        compander.reset();
        phase = phaseOffset;
        feedbackState = 0.0f;
        smoothedDelayMs = 0.0f;
    }

    void setControls(float manualControl, float widthControl, float rateControl,
                     float feedbackControl, float mixControl, float driveControl,
                     float outputControl, bool freeze, float shapeControl = -1.0f,
                     float rangeMode = 0.0f)
    {
        manual = rbmod::clamp01(manualControl);
        width = rbmod::clamp01(widthControl);
        rate = rbmod::clamp01(rateControl);
        feedback = rbmod::clamp01(feedbackControl);
        mix = rbmod::clamp01(mixControl);
        drive = rbmod::clamp01(driveControl);
        output = rbmod::clamp01(outputControl);
        frozen = freeze;
        lfoShapeControl = shapeControl;
        rangeControl = rbmod::clamp01(rangeMode);
    }

    float process(float in)
    {
        const float rateHz = logInterp(voice.minRateHz, voice.maxRateHz, rbmod::audioTaper(rate));
        if (!frozen)
        {
            phase += rateHz / sampleRate;
            phase -= std::floor(phase);
        }

        const float lfo = frozen ? 0.0f : lfoShape(phase);
        const bool longRange = rangeControl >= 0.5f;
        const float rangeMinMs = longRange ? std::min(voice.maxDelayMs * 0.20f, voice.minDelayMs * 3.3f)
                                           : voice.minDelayMs;
        const float shortRangeMaxMs = voice.flangeRangeMaxMs > 0.0f
            ? voice.flangeRangeMaxMs
            : std::max(voice.minDelayMs * 2.2f, 17.5f);
        const float rangeMaxMs = longRange ? voice.maxDelayMs
                                           : std::min(voice.maxDelayMs, shortRangeMaxMs);
        const float centerMs = logInterp(rangeMaxMs, rangeMinMs, rbmod::audioTaper(manual));
        const float span = std::log(rangeMaxMs / rangeMinMs)
                         * (voice.depthBase + voice.depthScale * rbmod::smoothstep(width));
        const float targetDelayMs = rbmod::clamp(centerMs * std::exp(lfo * span), rangeMinMs, rangeMaxMs);
        if (smoothedDelayMs <= 0.0f)
            smoothedDelayMs = targetDelayMs;
        const float slew = voice.delaySlewHz > 0.0f ? rbmod::onePoleCoeffHz(voice.delaySlewHz, sampleRate) : 1.0f;
        smoothedDelayMs += slew * (targetDelayMs - smoothedDelayMs);
        const float delayMs = rbmod::clamp(smoothedDelayMs, rangeMinMs, rangeMaxMs);
        const float clockHz = bbd.clockForDelayMs(delayMs);

        float x = inputHp.process(in);
        const float driveDb = voice.driveMinDb + (voice.driveMaxDb - voice.driveMinDb) * rbmod::audioTaper(drive);
        x = inputOpamp.process(x * dbToGain(driveDb), 2.0f + 18.0f * rbmod::audioTaper(drive));
        x = inputLp.process(x);

        const float fb = voice.feedbackMax * rbmod::smoothstep(feedback);
        const float write = rbmod::softClip(x + voice.feedbackSign * feedbackState * fb);
        float wet = bbd.process(write, clockHz, voice.bbdLpHz * (1.06f - 0.24f * rbmod::smoothstep(width)),
                                rbmod::smoothstep(feedback), rbmod::smoothstep(width));

        wet = compander.process(wet, voice.compander);
        const float colorBand = wet - colorLp.process(wet);
        wet = rbmod::softClip(wet + colorBand * (0.08f + 0.32f * rbmod::smoothstep(feedback)));
        feedbackState = wet;

        wet = outputLp2.process(outputLp1.process(wet));

        const float dry = voice.dryLevel * (1.0f - voice.dryDucking * mix);
        const float wetGain = voice.wetLevel * (voice.wetMixMin + voice.wetMixScale * mix);
        const float outputDb = voice.outputMinDb + (voice.outputMaxDb - voice.outputMinDb) * output;
        return rbmod::softClip((dry * x + voice.wetSign * wetGain * wet) * dbToGain(outputDb));
    }
};

} // namespace rbflanger

#endif // RB_PEDAL_FLANGER_COMPONENTS_H
