#ifndef TW40_CORE_H
#define TW40_CORE_H

/*
 * TW40Core - BENDER BASSMAN / Fender '59 Bassman (5F6-A tweed) component model.
 *
 * White-box audio model: each audible block is driven by the 5F6-A schematic
 * component values rather than a literal SPICE solve. Extracted to a header so
 * it is offline-testable in the closed-loop harness (see docs/schematics).
 *
 * Local reference:
 *   amps/Fender Bassman Tweed (TW40)/Fender_bassman_5f6a.pdf
 *   docs/schematics/tw40.md
 *
 * 5F6-A topology modelled here:
 *   two jumperable channels (BRIGHT with its 100pF bright cap + NORMAL), each
 *   with its own Volume off a 12AY7 input stage, summing into a 12AX7 -> the
 *   real FMV (Bassman) tone stack (Treble 250K / Bass 1M / Middle 25K) ->
 *   12AX7 driver + long-tail-pair phase inverter -> 2x 5881 (~45W) FIXED bias
 *   WITH global NFB -> GZ34 (stiff rectifier, moderate sag) -> 4x10. Presence
 *   taps the power-amp NFB.
 *
 * The 5F6-A is louder, tighter and has more headroom than the 5E3 Deluxe
 * (fixed bias + NFB + LTP PI + GZ34), and is the basis of the British crunch
 * that Marshall copied for the JTM45.
 *
 * the game: the 5F6-A has no gain knob, so RS Gain -> Bright Volume (the drive
 * into breakup); Treble/Bass/Mid -> tone stack, Pres -> Presence. A clickable
 * input cable picks Bright / Both(jumpered) / Normal.
 */

#include "TW40Params.h"
#include <cmath>

namespace tw40 {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    return std::fmax(20.0f, std::fmin(hz, nyquist));
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float smoothstepRange(float edge0, float edge1, float x)
{
    return smoothstep((x - edge0) / (edge1 - edge0));
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float asymTube(float x, float drive, float bias)
{
    const float pushed = x * drive + bias;
    const float y = std::tanh(pushed);
    const float correction = std::tanh(bias);
    return (y - correction) / (1.0f - 0.32f * std::fabs(correction));
}

static inline float tonePot(float v)
{
    v = clamp01(v);
    if (v < 0.001f)
        return 0.001f;
    if (v > 0.999f)
        return 0.999f;
    return v;
}

class Biquad
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f)
            na0 = 1.0f;
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        a1 = na1 * invA0;
        a2 = na2 * invA0;
    }

public:
    void reset()
    {
        z1 = z2 = 0.0f;
    }

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setHighPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setPeaking(float sr, float hz, float q, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a,
            1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }

    void setHighShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);

        set(a * ((a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
            a * ((a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha,
            2.0f * ((a - 1.0f) - (a + 1.0f) * c),
            (a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha);
    }

    void setLowShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);

        set(a * ((a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha),
            2.0f * a * ((a - 1.0f) - (a + 1.0f) * c),
            a * ((a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha,
            -2.0f * ((a - 1.0f) + (a + 1.0f) * c),
            (a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
};

// The real Bassman/FMV passive tone stack (Treble 250K, Bass 1M, Middle 25K,
// slope R 56K + 100K, C 250pF / 20nF / 20nF) as a bilinear-transformed 3rd-order
// transfer function. Component values straight off the 5F6-A schematic.
class BassmanToneStack
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, x3 = 0.0f, y1 = 0.0f, y2 = 0.0f, y3 = 0.0f;
    float sampleRate = 48000.0f;

public:
    void reset() { x1 = x2 = x3 = y1 = y2 = y3 = 0.0f; }
    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; }

    void update(float treble, float mid, float bass)
    {
        const float t = tonePot(treble);
        const float m = tonePot(mid);
        const float l = tonePot(bass);

        const float R1 = 250.0e3f;
        const float R2 = 1.0e6f;
        const float R3 = 25.0e3f;
        const float R4 = 56.0e3f;
        const float C1 = 250.0e-12f;
        const float C2 = 20.0e-9f;
        const float C3 = 20.0e-9f;

        const float ab0 = 0.0f;
        const float ab1 = t*C1*R1 + m*C3*R3 + l*(C1*R2 + C2*R2) + (C1*R3 + C2*R3);
        const float ab2 = t*(C1*C2*R1*R4 + C1*C3*R1*R4)
                        - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                        + m*(C1*C3*R1*R3 + C1*C3*R3*R3 + C2*C3*R3*R3)
                        + l*(C1*C2*R1*R2 + C1*C2*R2*R4 + C1*C3*R2*R4)
                        + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                        + (C1*C2*R1*R3 + C1*C2*R3*R4 + C1*C3*R3*R4);
        const float ab3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                        - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                        + t*l*C1*C2*C3*R1*R2*R4;
        const float aa0 = 1.0f;
        const float aa1 = (C1*R1 + C1*R3 + C2*R3 + C2*R4 + C3*R4)
                        + m*C3*R3 + l*(C1*R2 + C2*R2);
        const float aa2 = m*(C1*C3*R1*R3 - C2*C3*R3*R4 + C1*C3*R3*R3 + C2*C3*R3*R3)
                        - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                        + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                        + l*(C1*C2*R2*R4 + C1*C2*R1*R2 + C1*C3*R2*R4 + C2*C3*R2*R4)
                        + (C1*C2*R1*R4 + C1*C3*R1*R4 + C1*C2*R3*R4
                           + C1*C2*R1*R3 + C1*C3*R3*R4 + C2*C3*R3*R4);
        const float aa3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                        - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + m*(C1*C2*C3*R3*R3*R4 + C1*C2*C3*R1*R3*R3
                             - C1*C2*C3*R1*R3*R4)
                        + l*(C1*C2*C3*R1*R2*R4) + C1*C2*C3*R1*R3*R4;

        const float c = 2.0f * sampleRate;
        const float c2 = c * c;
        const float c3 = c2 * c;
        const float nb0 = -ab0 - ab1*c - ab2*c2 - ab3*c3;
        const float nb1 = -3.0f*ab0 - ab1*c + ab2*c2 + 3.0f*ab3*c3;
        const float nb2 = -3.0f*ab0 + ab1*c + ab2*c2 - 3.0f*ab3*c3;
        const float nb3 = -ab0 + ab1*c - ab2*c2 + ab3*c3;
        const float na0 = -aa0 - aa1*c - aa2*c2 - aa3*c3;
        const float na1 = -3.0f*aa0 - aa1*c + aa2*c2 + 3.0f*aa3*c3;
        const float na2 = -3.0f*aa0 + aa1*c + aa2*c2 - 3.0f*aa3*c3;
        const float na3 = -aa0 + aa1*c - aa2*c2 + aa3*c3;

        if (std::fabs(na0) < 1.0e-30f)
        {
            b0 = 1.0f; b1 = b2 = b3 = a1 = a2 = a3 = 0.0f;
            return;
        }
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0; b1 = nb1 * invA0; b2 = nb2 * invA0; b3 = nb3 * invA0;
        a1 = na1 * invA0; a2 = na2 * invA0; a3 = na3 * invA0;
    }

    float process(float x)
    {
        const float y = b0*x + b1*x1 + b2*x2 + b3*x3 - a1*y1 - a2*y2 - a3*y3;
        x3 = x2; x2 = x1; x1 = x;
        y3 = y2; y2 = y1; y1 = y;
        return y;
    }
};

class DcBlock
{
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void reset() { x1 = y1 = 0.0f; }
    float process(float x)
    {
        const float y = x - x1 + 0.995f * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

class TW40Core
{
    float sampleRate = 48000.0f;
    float input    = kTW40Def[kInput];
    float brightVol= kTW40Def[kBrightVol];
    float normalVol= kTW40Def[kNormalVol];
    float treble   = kTW40Def[kTreble];
    float bass     = kTW40Def[kBass];
    float mid      = kTW40Def[kMiddle];
    float pres     = kTW40Def[kPresence];

    // derived
    float brightG = 1.0f, normalG = 1.0f;   // channel gating from the input cable
    float effDrive = 0.5f;                   // overall drive proxy (the "gain" axis)

    Biquad inputHp;
    Biquad pickupLoad;
    Biquad brightShelf;
    Biquad normalBody;
    Biquad brightBody;
    Biquad interstageHp;
    Biquad cathodeFollowerLp;
    BassmanToneStack toneStack;
    Biquad stackMakeupLow;
    Biquad stackMakeupBody;
    Biquad phaseLowPass;
    Biquad presenceShelf;
    Biquad speakerHp;
    Biquad speakerThump;
    Biquad speakerLowMid;
    Biquad speakerBite;
    Biquad speakerAir;       // open-tweed top high-shelf (doubles as gain-dependent de-fizz)
    Biquad speakerLp;
    DcBlock dcBlock;

    float sag = 0.0f;

    static float eqDb(float normalized, float rangeDb)
    {
        return (clamp01(normalized) - 0.5f) * 2.0f * rangeDb;
    }

    void updateFilters()
    {
        // Input cable: Bright(<0.25) / Both(jumpered, 0.25-0.75) / Normal(>=0.75)
        brightG = (input < 0.75f) ? 1.0f : 0.0f;
        normalG = (input >= 0.25f) ? 1.0f : 0.0f;
        // Overall drive: the volumes ARE the gain (5F6-A has no gain knob). Both
        // channels jumpered sum, so the amp is driven harder.
        effDrive = clamp01(brightG * brightVol + normalG * normalVol * 0.85f);

        const float g = smoothstep(effDrive);
        const float pushed = smoothstepRange(0.42f, 0.92f, effDrive);
        // The 100pF bright cap bleeds treble most at LOW Bright Volume; plus base
        // brightness from Treble/Presence.
        const float bright = clamp01(0.32f * treble + 0.22f * pres + 0.42f * (1.0f - brightVol));

        inputHp.setHighPass(sampleRate, 42.0f + 52.0f * g + 26.0f * pushed, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12800.0f - 1800.0f * pushed + 900.0f * treble, 0.64f);
        brightShelf.setHighShelf(sampleRate, 1500.0f + 1150.0f * treble, 0.70f,
                                 -1.2f + 5.2f * bright + 1.7f * pres);
        normalBody.setPeaking(sampleRate, 190.0f + 55.0f * bass, 0.72f,
                              0.7f + 2.6f * bass - 1.2f * pushed);
        brightBody.setPeaking(sampleRate, 720.0f + 420.0f * mid, 0.82f,
                              -1.0f + 2.8f * mid + 0.9f * bright);

        interstageHp.setHighPass(sampleRate, 58.0f + 74.0f * pushed + 42.0f * (1.0f - bass), 0.70f);
        cathodeFollowerLp.setLowPass(sampleRate, 8800.0f + 1700.0f * treble - 1600.0f * pushed, 0.64f);
        toneStack.update(treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f + 30.0f * bass, 0.72f,
                                   eqDb(bass, 4.8f) - 1.4f * pushed);
        stackMakeupBody.setPeaking(sampleRate, 470.0f + 170.0f * mid, 0.66f,
                                   -1.2f + 4.8f * mid + 1.2f * pushed);
        // Interstage rolloff (cathode follower into the FMV stack). Opened up so it
        // is NOT the brightness bottleneck (the old 6.9k capped the top before the
        // speaker LP could); still closes under heavy drive.
        phaseLowPass.setLowPass(sampleRate, 10800.0f + 1700.0f * treble + 1200.0f * pres
                                            - 2000.0f * pushed, 0.64f);
        presenceShelf.setHighShelf(sampleRate, 2600.0f + 900.0f * pres, 0.78f,
                                   -4.0f + 8.6f * pres + 0.9f * treble);

        // --- Speaker / 4x10 cab voicing (the DOMINANT brightness lever) ---
        // The defaults were far too dark (LP ~6.2k + a fizz notch). A miked open-back
        // tweed 4x10 is bright: upper-mid bite + air, with the top rolling back only
        // when the amp is cranked hard. See _TUNING_PLAYBOOK (BOX DC30 / Deluxe).
        speakerHp.setHighPass(sampleRate, 74.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 122.0f, 0.84f, 0.9f + 2.4f * bass);
        speakerLowMid.setPeaking(sampleRate, 330.0f + 90.0f * mid, 0.78f,
                                 0.8f + 1.9f * mid - 0.7f * pushed);
        // Upper-mid presence ("bite", 2-3 kHz), only gently eased when cranked.
        speakerBite.setPeaking(sampleRate, 2650.0f + 520.0f * treble, 0.78f,
                               9.2f + 2.2f * treble + 1.6f * pres - 7.4f * pushed);
        // Air high-shelf (replaces the old fizz NOTCH): lifts the open-tweed top. The
        // 5F6-A "cranked" is a loud clean/edge amp (not a fizz monster), so the
        // gain-dependent retreat is GENTLE -- it stays bright even jumpered/loud.
        speakerAir.setHighShelf(sampleRate, 4600.0f, 0.70f,
                                18.5f + 2.0f * treble + 2.0f * pres - 16.5f * pushed);
        // Speaker LP opened WAY up (was ~6.2k = "apagado") so the cab is bright like a
        // miked 4x10; only eases a touch under heavy drive.
        speakerLp.setLowPass(sampleRate, 18500.0f + 2200.0f * treble + 1400.0f * pres
                                         - 8500.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); brightShelf.reset();
        normalBody.reset(); brightBody.reset();
        interstageHp.reset(); cathodeFollowerLp.reset();
        toneStack.reset(); stackMakeupLow.reset(); stackMakeupBody.reset();
        phaseLowPass.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset();
        speakerBite.reset(); speakerAir.reset(); speakerLp.reset();
        dcBlock.reset();
        sag = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        toneStack.setSampleRate(sampleRate);
        reset();
    }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kInput:     input = v; break;
            case kBrightVol: brightVol = v; break;
            case kNormalVol: normalVol = v; break;
            case kTreble:    treble = v; break;
            case kBass:      bass = v; break;
            case kMiddle:    mid = v; break;
            case kPresence:  pres = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults()
    {
        for (int i = 0; i < kParamCount; ++i)
            setParam(i, kTW40Def[i]);
    }

    float process(float in)
    {
        const float g = smoothstep(effDrive);
        const float pushed = smoothstepRange(0.42f, 0.92f, effDrive);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = softClip(x * (1.04f + 0.08f * pushed)) * (0.96f - 0.04f * pushed);

        // BRIGHT channel: bright cap + body, its own 12AY7 triode, then volume.
        float bch = brightShelf.process(brightBody.process(x));
        bch = asymTube(bch, 0.90f + 2.25f * effDrive + 2.3f * g, 0.012f + 0.016f * effDrive);
        // NORMAL channel: darker body, its own triode, then volume.
        float nch = normalBody.process(x);
        nch = asymTube(nch, 0.82f + 2.0f * effDrive + 2.0f * g, 0.010f + 0.014f * effDrive);

        // Jumpered mix: each channel scaled by its Volume and gated by the cable.
        float y = brightG * brightVol * bch + normalG * normalVol * 0.92f * nch;
        // A little clean leak at low drive (the tweed stays articulate when quiet).
        const float cleanLeak = 0.34f * (1.0f - smoothstepRange(0.28f, 0.78f, effDrive));
        y = y * (1.0f - cleanLeak) + x * cleanLeak * (brightG * brightVol + normalG * normalVol * 0.5f);

        // 12AX7 recovery / cathode-follower into the FMV tone stack.
        y = interstageHp.process(y);
        y = asymTube(y, 0.82f + 1.60f * effDrive + 2.2f * pushed, -0.006f - 0.010f * effDrive);
        y = cathodeFollowerLp.process(y);

        y = toneStack.process(y) * 1.70f;
        y = stackMakeupLow.process(y);
        y = stackMakeupBody.process(y);
        y = phaseLowPass.process(y);

        // 2x 5881 push-pull + GZ34 sag (the GZ34 is fairly stiff -> moderate sag).
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0060f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.150f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.36f + 0.86f * effDrive + 0.50f * pushed));

        const float powerDrive = (0.92f + 1.55f * effDrive + 2.25f * pushed) * sagDrop;
        y = asymTube(y, powerDrive, 0.006f + 0.014f * (treble - bass) + 0.010f * pres);
        y = 0.86f * y + 0.14f * softClip(y * (1.65f + 1.35f * pushed));
        y *= 0.98f - 0.08f * sag;

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerAir.process(y);
        y = speakerLp.process(y);

        // Loudness normalization: the volumes-as-gain means a low-volume setting
        // is much quieter than a cranked one. cleanMakeup lifts the quiet end so
        // the RS Gain (-> Bright Volume) sweep stays within a couple dB and the
        // single shared kLvl stage stays calibrated.
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.010f * std::fabs((pres - 0.5f) * 16.0f);
        const float cleanMakeup = 1.0f + 3.0f * std::exp(-effDrive / 0.30f);
        const float level = (0.72f + 0.14f * (1.0f - effDrive)) * cleanMakeup /
            ((1.0f + 0.28f * effDrive + 0.32f * pushed) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

} // namespace tw40

#endif // TW40_CORE_H
