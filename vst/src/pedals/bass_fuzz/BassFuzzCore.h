#ifndef BASS_FUZZ_CORE_H
#define BASS_FUZZ_CORE_H

/*
 * Bass Big Muff Pi core — DPF-free, designed to run at the OVERSAMPLED rate.
 *
 * Schematic blocks modeled: input coupling, BC547 common-emitter gain stages,
 * two 1N4148 anti-parallel clipping cells, the Big Muff passive tone stack, the
 * bass-version Dry/Bass blend switch, and the TLC2264 output buffer behavior.
 *
 * IMPORTANT: this core does NOT set the output level. The Sustain knob only
 * changes how hard the signal clips (the fuzz character), NOT the loudness —
 * the wrapper (BassFuzzPlugin) runs RBAutoMakeup so the pedal sits at the dry
 * level regardless of Sustain, then applies the real Volume pot on top. This is
 * what fixes "the synth-bass fuzz tone plays way too loud in the song": its
 * output is now loudness-locked to the clean DI like every other reworked fuzz.
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
    float zCleanLow = 0.0f;
    float zStage1 = 0.0f;
    float zStage2 = 0.0f;
    float zToneLow = 0.0f;
    float zToneHigh = 0.0f;
    float zOutDc = 0.0f;
    float cInput = 0.0f;
    float cCleanLow = 0.0f;
    float cStage1 = 0.0f;
    float cStage2 = 0.0f;
    float cToneLow = 0.0f;
    float cToneHigh = 0.0f;
    float cOutDc = 0.0f;
    float sustainGain = 55.0f;
    float tone = 0.52f;
    float mode = 0.0f;
    rbshared::OpAmpStage inputBuffer;
    rbshared::OpAmpStage outputBuffer;
    rbcomponents::AntiParallelDiodePair clip1;
    rbcomponents::AntiParallelDiodePair clip2;

public:
    BassBigMuffCore()
    {
        inputBuffer.setSpec(rbshared::tlc2264Spec());
        outputBuffer.setSpec(rbshared::tlc2264Spec());
        clip1.setSpec(rbcomponents::diode1N4148());
        clip2.setSpec(rbcomponents::diode1N4148());
        clip1.setSourceR(3900.0f);
        clip2.setSourceR(3300.0f);
    }

    void reset()
    {
        zInput = zCleanLow = zStage1 = zStage2 = 0.0f;
        zToneLow = zToneHigh = zOutDc = 0.0f;
    }

    void setSampleRate(float s)
    {
        fs = s > 1000.0f ? s : 96000.0f;
        inputBuffer.setSampleRate(fs);
        outputBuffer.setSampleRate(fs);
        recalcFixed();
    }

    void recalcFixed()
    {
        cInput = onePoleCoef(37.0f, fs);
        cCleanLow = onePoleCoef(155.0f, fs);
        cStage1 = onePoleCoef(6900.0f, fs);
        cStage2 = onePoleCoef(5600.0f, fs);
        cToneLow = onePoleCoef(520.0f, fs);
        cToneHigh = onePoleCoef(760.0f, fs);
        cOutDc = onePoleCoef(12.0f, fs);
    }

    // Volume is NOT set here — the wrapper applies it AFTER RBAutoMakeup.
    void setParams(float sustain, float toneP, float bassDry)
    {
        sustainGain = 18.0f + 145.0f * audioTaper(sustain);
        tone = clamp01(toneP);
        mode = quantize3(bassDry);
    }

    // Returns the WET fuzz at its natural (un-levelled) amplitude. RBAutoMakeup
    // in the wrapper locks this to the dry input loudness.
    float process(float x)
    {
        zCleanLow += cCleanLow * (x - zCleanLow);
        const float lowTap = zCleanLow;

        zInput += cInput * (x - zInput);
        float s = inputBuffer.process(x - zInput, 1.4f);

        s = bjtCommonEmitter(s * sustainGain, 0.14f);
        s = clip1.process(s);
        zStage1 += cStage1 * (s - zStage1);
        s = zStage1;

        s = bjtCommonEmitter(s * (5.0f + 14.0f * audioTaper(sustainGain / 163.0f)), -0.09f);
        s = clip2.process(s);
        zStage2 += cStage2 * (s - zStage2);
        s = zStage2;

        zToneLow += cToneLow * (s - zToneLow);
        zToneHigh += cToneHigh * (s - zToneHigh);
        const float low = zToneLow * 1.55f;
        const float high = (s - zToneHigh) * 1.28f;
        float out = low * (1.0f - tone) + high * tone;

        // Bass-version blend switch: center = fuzz + clean low, up = more dry/clean.
        if (mode == 0.5f)
            out = out * 0.88f + lowTap * 0.95f;
        else if (mode > 0.5f)
            out = out * 0.62f + x * 0.58f + lowTap * 0.45f;

        out = outputBuffer.process(out, 2.2f);
        zOutDc += cOutDc * (out - zOutDc);
        return out - zOutDc;
    }
};

} // namespace bassfuzz

#endif // BASS_FUZZ_CORE_H
