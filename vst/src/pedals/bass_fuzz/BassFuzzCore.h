#ifndef BASS_FUZZ_CORE_H
#define BASS_FUZZ_CORE_H

/*
 * Bass Big Muff Pi core — DPF-free, designed to run at the OVERSAMPLED rate.
 *
 * Schematic blocks modeled: input coupling, BC547 common-emitter gain stages,
 * two 1N4148 anti-parallel clipping cells, the Big Muff passive tone stack, the
 * bass-version Dry/Bass blend switch, and the TLC2264 output buffer behavior.
 *
 * The core returns the natural circuit level. BassFuzzPlugin applies a measured
 * static calibration followed by the real Volume pot; it must not use an
 * envelope-driven wet/dry makeup stage because that changes attack and decay.
 */
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

namespace bassfuzz {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float quantize3(float v)
{
    return v < 0.25f ? 0.0f : (v < 0.75f ? 0.5f : 1.0f);
}

static inline float onePoleCoef(float fc, float fs)
{
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

static inline float audioTaper(float v)
{
    const float x = clamp01(v);
    return std::pow(x, 2.15f);
}

static inline float bjtCommonEmitter(float x, float bias)
{
    const float shifted = x + bias;
    const float compressed = std::tanh(shifted) - 0.18f * std::tanh(0.28f * shifted);
    return compressed - (std::tanh(bias) - 0.18f * std::tanh(0.28f * bias));
}

class BassBigMuffCore
{
    float fs = 96000.0f;     // oversampled rate
    float zInput = 0.0f;
    float zBassBoost = 0.0f;
    float zStage1 = 0.0f;
    float zStage2 = 0.0f;
    float zToneLow = 0.0f;
    float zToneHigh = 0.0f;
    float zOutDc = 0.0f;
    float cInput = 0.0f;
    float cBassBoost = 0.0f;
    float cStage1 = 0.0f;
    float cStage2 = 0.0f;
    float cToneLow = 0.0f;
    float cToneHigh = 0.0f;
    float cOutDc = 0.0f;
    float sustainGain = 55.0f;
    float stage2Gain = 12.0f;
    float tone = 0.52f;
    float mode = 0.0f;
    rbshared::OpAmpStage inputBuffer;
    rbshared::OpAmpStage outputBuffer;
    rbshared::OpAmpStage dryBuffer;
    rbcomponents::AntiParallelDiodePair clip1;
    rbcomponents::AntiParallelDiodePair clip2;

public:
    BassBigMuffCore()
    {
        inputBuffer.setSpec(rbshared::tlc2264Spec());
        outputBuffer.setSpec(rbshared::tlc2264Spec());
        dryBuffer.setSpec(rbshared::tlc2264Spec());
        clip1.setSpec(rbcomponents::diode1N4148());
        clip2.setSpec(rbcomponents::diode1N4148());
        clip1.setSourceR(3900.0f);
        clip2.setSourceR(3300.0f);
    }

    void reset()
    {
        zInput = zBassBoost = zStage1 = zStage2 = 0.0f;
        zToneLow = zToneHigh = zOutDc = 0.0f;
        inputBuffer.reset();
        outputBuffer.reset();
        dryBuffer.reset();
    }

    void setSampleRate(float s)
    {
        fs = s > 1000.0f ? s : 96000.0f;
        inputBuffer.setSampleRate(fs);
        outputBuffer.setSampleRate(fs);
        dryBuffer.setSampleRate(fs);
        recalcFixed();
    }

    void recalcFixed()
    {
        cInput = onePoleCoef(37.0f, fs);
        // R2/C4 feeds U1A from the already-clipped node 1. The reverse schematic
        // omits C4's value, so 155 Hz is the calibrated corner for this
        // low-frequency-selective branch used only by SW2 Bass Boost.
        cBassBoost = onePoleCoef(155.0f, fs);
        cStage1 = onePoleCoef(6900.0f, fs);
        cStage2 = onePoleCoef(5600.0f, fs);
        cToneLow = onePoleCoef(520.0f, fs);
        cToneHigh = onePoleCoef(760.0f, fs);
        cOutDc = onePoleCoef(12.0f, fs);
    }

    // Volume is applied by the wrapper after circuit calibration.
    void setParams(float sustain, float toneP, float bassDry)
    {
        // Both gain stages sweep with Sustain. The old floors (18 into stage 1, a
        // fixed 5.0 into stage 2) slammed BOTH stages even at Sustain=0, so the
        // knob was dead: a held note stayed fully sustained/compressed at every
        // setting (tail/head ~0 dB across the whole travel). Low floors let the
        // bottom of the pot decay naturally (~-17 dB tail) and sweep up to full
        // sustain by noon; the default 0.78 stays fully saturated as before.
        const float t = audioTaper(sustain);
        sustainGain = 3.0f + 150.0f * t;
        stage2Gain = 1.6f + 16.0f * t;
        tone = clamp01(toneP);
        mode = quantize3(bassDry);
    }

    // Returns the wet fuzz at its natural circuit amplitude.
    float process(float x)
    {
        zInput += cInput * (x - zInput);
        float s = inputBuffer.process(x - zInput, 1.4f);

        s = bjtCommonEmitter(s * sustainGain, 0.14f);
        s = clip1.process(s);
        zStage1 += cStage1 * (s - zStage1);
        s = zStage1;

        s = bjtCommonEmitter(s * stage2Gain, -0.09f);
        s = clip2.process(s);
        zStage2 += cStage2 * (s - zStage2);
        s = zStage2;

        // Node 1 -> R2/C4 -> U1A. This is clipped fuzz before the tone stack,
        // not a clean input path.
        zBassBoost += cBassBoost * (s - zBassBoost);
        const float bassBoostTap = zBassBoost;

        zToneLow += cToneLow * (s - zToneLow);
        zToneHigh += cToneHigh * (s - zToneHigh);
        const float low = zToneLow * 1.55f;
        const float high = (s - zToneHigh) * 1.28f;
        const float fuzzTone = low * (1.0f - tone) + high * tone;

        // SW2A Bass Boost injects U1A's filtered, already-clipped node-1 signal
        // through C9/R7 at Q1's input. NORMAL leaves that pole open.
        float recoveryInput = fuzzTone;
        if (mode == 0.5f)
            recoveryInput += 0.82f * bassBoostTap;

        const float fuzzOut = outputBuffer.process(recoveryInput, 2.2f);
        const float dryOut = dryBuffer.process(x, 1.1f); // U1B remains active in every SW2 position

        // SW2B Dry connects node A through U1B/R31 at U1C's final summing node.
        // It is the only switch position that contains unprocessed input.
        float out = fuzzOut;
        if (mode > 0.5f)
            out = 0.66f * fuzzOut + 10.0f * dryOut;

        zOutDc += cOutDc * (out - zOutDc);
        return out - zOutDc;
    }
};

} // namespace bassfuzz

#endif // BASS_FUZZ_CORE_H
