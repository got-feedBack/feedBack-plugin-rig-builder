#ifndef EN30_CORE_H
#define EN30_CORE_H

/*
 * EN30Core - BOX DC30 / Vox AC30C2 (Custom series) component model.
 *
 * This is not a full SPICE/MNA netlist solver. It is a white-box audio model
 * where each audible block is driven by the component values in the local
 * AC30C2 schematic: RC coupling caps, grid leaks, cathode bypass caps, the
 * three 12AX7 preamp stages (V1/V2/V3), the Top Boost treble/bass network,
 * the VR9 4n7 Tone Cut, four EL84s with a SILICON (1N4007) supply — so only
 * gentle sag, not a GZ34 dip — the output transformer and 2x12 speaker.
 *
 * Local references:
 *   amps/vox ac30 (en30)/Vox_ac30c2.pdf            (THIS model — Custom series)
 *   amps/vox ac30 (en30)/Vox_ac30cc2_ac30cc2x_2005_sm.pdf
 *   amps/vox ac30 (en30)/ac30-60-02-iss5.pdf
 *
 * Target amp: the Vox AC30C2 (Custom series), silicon-rectified. The real panel
 * pots are: Normal Vol, TB Vol, Treble, Bass, Reverb Tone (VR5 A500K), Reverb
 * Level (VR6 B100K), Tremolo Speed/Depth, Tone Cut (VR9 B220K), Master. The Top
 * Boost stack has NO mid pot (its mid is a fixed resistor — the Vox scoop). Two
 * faithful EXTRAS give the Rocksmith Mid/Bright knobs a home without inventing
 * fake pots: Input = the channel-jumper select (cable jumper on the C2 / CHANNEL
 * LINK switch SW1 on the CC2), and Bright = the Top Boost brilliance bright-cap
 * amount (a fixed cap on the C2 / a 2-position switch on the CC2). Rocksmith
 * mapping: Gain->TB Vol, Bass->Bass, Treble->Treble, Pres->Tone Cut(inv),
 * Mid->Normal Vol (+Input=Both), Bright->the brilliance cap.
 *
 * Audit fixes (vs the schematics): the Top Boost stack is now the REAL 3rd-order
 * FMV transfer function (56pF/100K/22nF/1M), the post-stack recovery is a CLEAN
 * op-amp (not a saturating tube) followed by the V3 driver tube, the Tone Cut
 * corner is fixed (only its depth tracks the knob), the EL84 bias no longer
 * drifts with Treble/Bass, the spring reverb is tapped from the preamp send and
 * returned before the power amp, and the tremolo modulates at the phase-inverter
 * input. A long-tailed-pair phase inverter colouration sits before the EL84s.
 */

#include "EN30Params.h"
#include <cmath>

namespace en30 {

static constexpr float kPi = 3.14159265359f;
static constexpr float kEpsilon = 1.0e-9f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    return std::fmax(10.0f, std::fmin(hz, nyquist));
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

static inline float dbToAmp(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

class RcHighPass
{
    float a = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void reset()
    {
        x1 = y1 = 0.0f;
    }

    void setRC(float sr, float resistanceOhm, float capacitanceF)
    {
        const float dt = 1.0f / std::fmax(sr, 1000.0f);
        const float tau = std::fmax(resistanceOhm * capacitanceF, kEpsilon);
        a = tau / (tau + dt);
    }

    void setHz(float sr, float hz)
    {
        hz = clampFreq(hz, sr);
        const float tau = 1.0f / (2.0f * kPi * hz);
        const float dt = 1.0f / std::fmax(sr, 1000.0f);
        a = tau / (tau + dt);
    }

    float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = y;
        return y;
    }
};

class RcLowPass
{
    float a = 1.0f;
    float z = 0.0f;

public:
    void reset()
    {
        z = 0.0f;
    }

    void setRC(float sr, float resistanceOhm, float capacitanceF)
    {
        const float dt = 1.0f / std::fmax(sr, 1000.0f);
        const float tau = std::fmax(resistanceOhm * capacitanceF, kEpsilon);
        a = dt / (tau + dt);
    }

    void setHz(float sr, float hz)
    {
        hz = clampFreq(hz, sr);
        const float tau = 1.0f / (2.0f * kPi * hz);
        const float dt = 1.0f / std::fmax(sr, 1000.0f);
        a = dt / (tau + dt);
    }

    float process(float x)
    {
        z += a * (x - z);
        return z;
    }
};

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
};

class DcBlock
{
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void reset()
    {
        x1 = y1 = 0.0f;
    }

    float process(float x)
    {
        const float y = x - x1 + 0.995f * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

static inline float triode12AX7(float x, float bias)
{
    // Smooth approximation of a biased 12AX7 transfer curve: asymmetric,
    // softer near cutoff, steeper when grid drive goes positive.
    const float g = x + bias;
    const float warped = 1.55f * g + 0.34f * g * std::fabs(g);
    const float idle = 1.55f * bias + 0.34f * bias * std::fabs(bias);
    return std::tanh(warped) - std::tanh(idle);
}

static inline float el84PentodePair(float x, float bias)
{
    const float p = std::tanh(1.35f * (x + bias) + 0.08f * x * x);
    const float n = std::tanh(1.35f * (-x + bias) + 0.08f * x * x);
    const float idle = std::tanh(1.35f * bias);
    return 0.5f * ((p - idle) - (n - idle));
}

class CathodeBypass
{
    RcHighPass capPath;
    float unbypassedGain = 0.58f;
    float bypassBoost = 0.42f;
    float compression = 0.18f;
    float env = 0.0f;
    float attack = 0.0f;
    float release = 0.0f;

public:
    void reset()
    {
        capPath.reset();
        env = 0.0f;
    }

    void set(float sr, float rkOhm, float ckF, float baseGain,
             float boostGain, float compressionAmount)
    {
        capPath.setRC(sr, rkOhm, ckF);
        unbypassedGain = baseGain;
        bypassBoost = boostGain;
        compression = compressionAmount;
        attack = 1.0f - std::exp(-1.0f / (0.006f * sr));
        release = 1.0f - std::exp(-1.0f / (0.075f * sr));
    }

    float process(float x)
    {
        const float level = std::fabs(x);
        env += (level - env) * (level > env ? attack : release);
        const float cathodeDegeneration = 1.0f / (1.0f + env * compression);
        return (x * unbypassedGain + capPath.process(x) * bypassBoost) * cathodeDegeneration;
    }
};

class TriodeStage
{
    RcHighPass couplingCap;
    RcLowPass gridStopperPole;
    CathodeBypass cathode;
    float plateGain = 1.0f;
    float baseDrive = 1.0f;
    float bias = -0.08f;
    float outputGain = 1.0f;

public:
    void reset()
    {
        couplingCap.reset();
        gridStopperPole.reset();
        cathode.reset();
    }

    void set(float sr,
             float couplingF, float nextGridLeakOhm,
             float gridStopperOhm, float millerCapF,
             float cathodeOhm, float cathodeCapF,
             float plateOhm, float drive, float biasV,
             float outGain, float unbypassedGain, float bypassBoost)
    {
        couplingCap.setRC(sr, nextGridLeakOhm, couplingF);
        gridStopperPole.setRC(sr, gridStopperOhm, millerCapF);
        cathode.set(sr, cathodeOhm, cathodeCapF, unbypassedGain, bypassBoost, 0.12f);
        plateGain = (100.0f * plateOhm / (plateOhm + 62500.0f)) / 62.0f;
        baseDrive = drive;
        bias = biasV;
        outputGain = outGain;
    }

    float process(float x, float driveKnob, float supplyScale)
    {
        float v = couplingCap.process(x);
        v = gridStopperPole.process(v);
        v = cathode.process(v);
        const float headroom = 0.78f + 0.38f * supplyScale;
        const float drive = baseDrive * (0.70f + 1.15f * driveKnob);
        return triode12AX7(v * drive / headroom, bias) * headroom * plateGain * outputGain;
    }
};

class CathodeFollower
{
    RcLowPass gridStopperPole;
    float gridCurrent = 0.0f;
    float attack = 0.0f;
    float release = 0.0f;

public:
    void reset()
    {
        gridStopperPole.reset();
        gridCurrent = 0.0f;
    }

    void set(float sr)
    {
        // R8 56k and stray/Miller capacitance before the follower.
        gridStopperPole.setRC(sr, 56000.0f, 95.0e-12f);
        attack = 1.0f - std::exp(-1.0f / (0.0025f * sr));
        release = 1.0f - std::exp(-1.0f / (0.050f * sr));
    }

    float process(float x)
    {
        float g = gridStopperPole.process(x);
        const float positiveGrid = std::fmax(0.0f, g - 0.42f);
        gridCurrent += (positiveGrid - gridCurrent) * (positiveGrid > gridCurrent ? attack : release);
        const float loaded = g / (1.0f + 0.55f * gridCurrent);
        return 0.86f * loaded + 0.14f * softClip(loaded * 1.35f);
    }
};

// Vox AC30C2 "Top Boost" Treble/Bass network — the REAL FMV-topology passive
// stack driven by the V1b cathode follower, derived component-by-component from
// the AC30C2 schematic: Treble pot VR3 1M, Bass pot VR4 1M, slope R19 100K,
// treble cap C23 56pF, bass cap 22nF (C28/C38). The Vox has NO mid pot — the mid
// leg is a FIXED resistor to ground, which is exactly what carves the famous Vox
// mid scoop. Implemented as the standard 3rd-order FMV transfer function,
// bilinear-transformed (same analytic machinery as the Plexi/JTM45 stacks in this
// repo), with the mid wiper pinned. This replaces the previous parallel
// filter-bank + three invented correction biquads (audit fidelity 34/100).
class TopBoostToneStack
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, x3 = 0.0f, y1 = 0.0f, y2 = 0.0f, y3 = 0.0f;
    float sampleRate = 48000.0f;
    static inline float pot(float v)
    {
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return v < 0.001f ? 0.001f : (v > 0.999f ? 0.999f : v);
    }

public:
    void reset() { x1 = x2 = x3 = y1 = y2 = y3 = 0.0f; }
    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; }

    void update(float treble, float bass)
    {
        const float t = pot(treble);
        const float m = 0.13f;        // FIXED Vox mid leg (no mid pot -> the scoop)
        const float l = pot(bass);

        const float R1 = 1.0e6f;      // VR3 Treble pot 1M
        const float R2 = 1.0e6f;      // VR4 Bass pot 1M
        const float R3 = 25.0e3f;     // fixed mid leg to ground (sets scoop depth)
        const float R4 = 100.0e3f;    // R19 slope resistor 100K (AC30C2)
        const float C1 = 56.0e-12f;   // C23 treble cap 56pF
        const float C2 = 22.0e-9f;    // bass cap 22nF
        const float C3 = 22.0e-9f;    // mid cap 22nF

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
        const float cc = c * c, ccc = cc * c;
        const float nb0 = -ab0 - ab1*c - ab2*cc - ab3*ccc;
        const float nb1 = -3.0f*ab0 - ab1*c + ab2*cc + 3.0f*ab3*ccc;
        const float nb2 = -3.0f*ab0 + ab1*c + ab2*cc - 3.0f*ab3*ccc;
        const float nb3 = -ab0 + ab1*c - ab2*cc + ab3*ccc;
        const float na0 = -aa0 - aa1*c - aa2*cc - aa3*ccc;
        const float na1 = -3.0f*aa0 - aa1*c + aa2*cc + 3.0f*aa3*ccc;
        const float na2 = -3.0f*aa0 + aa1*c + aa2*cc - 3.0f*aa3*ccc;
        const float na3 = -aa0 + aa1*c - aa2*cc + aa3*ccc;
        if (std::fabs(na0) < 1.0e-30f) { b0 = 1.0f; b1 = b2 = b3 = a1 = a2 = a3 = 0.0f; return; }
        const float inv = 1.0f / na0;
        b0 = nb0*inv; b1 = nb1*inv; b2 = nb2*inv; b3 = nb3*inv;
        a1 = na1*inv; a2 = na2*inv; a3 = na3*inv;
    }

    float process(float x)
    {
        const float y = b0*x + b1*x1 + b2*x2 + b3*x3 - a1*y1 - a2*y2 - a3*y3;
        x3 = x2; x2 = x1; x1 = x;
        y3 = y2; y2 = y1; y1 = y;
        return y;
    }
};

class CutNetwork
{
    RcHighPass c1c2Cut4n7;
    float amount = 0.0f;

public:
    void reset()
    {
        c1c2Cut4n7.reset();
    }

    void set(float sr, float pres)
    {
        // AC30C2 Tone Cut: VR9 B220K + C80 4n7 shunt at the V3 driver node (a
        // global treble bleed, post-preamp / pre-power-amp). The cap is FIXED, so
        // the shunt corner stays put (~154 Hz with the 220k pot, 1/(2*pi*220k*4n7));
        // only the DEPTH of the high-frequency removal tracks the knob. (Previously
        // the corner itself swept 154 Hz -> 1.5 kHz, which is non-physical.)
        c1c2Cut4n7.setRC(sr, 220000.0f, 4.7e-9f);
        amount = 0.88f * (1.0f - pres);   // pres = 1 - Cut: more Cut -> darker
    }

    float process(float x)
    {
        return x - amount * c1c2Cut4n7.process(x);
    }
};

class SolidStateSupply
{
    float sr = 48000.0f;
    float rectifierSag = 0.0f;
    float cathodeRise = 0.0f;
    float sagAttack = 0.0f;
    float sagRelease = 0.0f;
    float cathodeCoeff = 0.0f;

public:
    void reset()
    {
        rectifierSag = 0.0f;
        cathodeRise = 0.0f;
    }

    void setSampleRate(float sampleRate)
    {
        sr = sampleRate > 1000.0f ? sampleRate : 48000.0f;
        // AC30C2 uses a SILICON bridge (D5-D8 1N4007), not a GZ34 tube rectifier:
        // the B+ barely sags (low diode resistance), so the dip is small + quick.
        sagAttack = 1.0f - std::exp(-1.0f / (0.008f * sr));
        sagRelease = 1.0f - std::exp(-1.0f / (0.140f * sr));
        // EL84 shared cathode-bias R/C still breathes (R101/R104 3K3, C with the
        // 100uF/220uF filter caps) -> a gentle dynamic compression, ~11 ms.
        const float tau = 50.0f * 220.0e-6f;
        cathodeCoeff = 1.0f - std::exp(-1.0f / (std::fmax(tau, 0.001f) * sr));
    }

    float process(float env, float gain, float hot)
    {
        rectifierSag += (env - rectifierSag) * (env > rectifierSag ? sagAttack : sagRelease);
        cathodeRise += (env - cathodeRise) * cathodeCoeff;
        // Solid-state rectified: only a slight B+ dip (tight, not a saggy GZ34);
        // most of the "breathing" comes from the EL84 cathode bias instead.
        const float bPlus = 1.0f / (1.0f + rectifierSag * (0.10f + 0.20f * gain + 0.10f * hot));
        const float cathodeBias = 1.0f / (1.0f + cathodeRise * (0.14f + 0.24f * gain));
        return bPlus * cathodeBias;
    }
};

class El84PowerAmp
{
    RcHighPass c24c25Coupling100n;
    RcLowPass screenNode;
    SolidStateSupply supply;

public:
    void reset()
    {
        c24c25Coupling100n.reset();
        screenNode.reset();
        supply.reset();
    }

    void setSampleRate(float sr)
    {
        // C24/C25 100n into roughly 1M PI/power-grid reference.
        c24c25Coupling100n.setRC(sr, 1000000.0f, 100.0e-9f);
        // 100R screen resistors plus supply filtering smooth screen current.
        screenNode.setRC(sr, 100.0f, 47.0e-6f);
        supply.setSampleRate(sr);
    }

    float process(float x, float gain, float hot)
    {
        float grid = c24c25Coupling100n.process(x);
        const float env = screenNode.process(std::fabs(grid));
        const float supplyScale = supply.process(env, gain, hot);
        const float drive = (1.02f + 1.45f * gain + 1.25f * hot) * supplyScale;
        // Fixed cathode-bias operating point — the EL84 bias is set by the shared
        // 50R/220uF cathode network, NOT by the Treble/Bass tone controls (which
        // sit far upstream in the preamp). Previously the bias drifted with
        // (treble - bass), which has no circuit counterpart.
        float y = el84PentodePair(grid * drive, 0.03f);
        // No global NFB in the AC30; preserve the raw EL84 edge but avoid
        // digital runaway when a booster hits the input. Lighter secondary clip
        // so the cranked tone stays open instead of squashing.
        y = 0.94f * y + 0.06f * softClip(y * (1.4f + 0.5f * gain));
        return y * supplyScale;
    }
};

// --- compact mono spring reverb (3 allpass diffusers + 2 damped combs,
//     band-limited like a real spring tank) ---
class SpringReverb
{
    float ap0[1024], ap1[1024], ap2[1024];
    float cb0[3600], cb1[3600];
    int p0 = 0, p1 = 0, p2 = 0, c0 = 0, c1 = 0;
    int n0 = 225, n1 = 341, n2 = 441, nc0 = 1617, nc1 = 1991;
    float damp0 = 0.0f, damp1 = 0.0f;
    Biquad inHp, inLp;
    static inline float apStep(float* buf, int& p, int n, float in, float g)
    {
        const float bo = buf[p];
        const float v = in + bo * g;
        buf[p] = v;
        if (++p >= n) p = 0;
        return bo - v * g;
    }
public:
    void setSampleRate(float sr)
    {
        const float s = (sr > 1000.0f ? sr : 48000.0f) / 48000.0f;
        n0 = (int)(225 * s); n1 = (int)(341 * s); n2 = (int)(441 * s);
        nc0 = (int)(1617 * s); nc1 = (int)(1991 * s);
        if (nc0 > 3599) nc0 = 3599; if (nc1 > 3599) nc1 = 3599;
        inHp.setHighPass(sr, 220.0f, 0.7f);     // springs roll off the lows
        inLp.setLowPass(sr, 5000.0f, 0.7f);     // and the highs ("boing"); headroom
                                                // for the bright Reverb Tone tap
        clear();
    }
    void clear()
    {
        for (int i = 0; i < 1024; ++i) ap0[i] = ap1[i] = ap2[i] = 0.0f;
        for (int i = 0; i < 3600; ++i) cb0[i] = cb1[i] = 0.0f;
        p0 = p1 = p2 = c0 = c1 = 0; damp0 = damp1 = 0.0f;
    }
    float process(float x)
    {
        x = inLp.process(inHp.process(x));
        x = apStep(ap0, p0, n0, x, 0.6f);
        x = apStep(ap1, p1, n1, x, 0.6f);
        x = apStep(ap2, p2, n2, x, 0.6f);
        const float o0 = cb0[c0]; damp0 += 0.42f * (o0 - damp0); cb0[c0] = x + damp0 * 0.71f; if (++c0 >= nc0) c0 = 0;
        const float o1 = cb1[c1]; damp1 += 0.42f * (o1 - damp1); cb1[c1] = x + damp1 * 0.69f; if (++c1 >= nc1) c1 = 0;
        return (o0 + o1) * 0.5f;
    }
};

// --- amplitude tremolo LFO (Speed = rate, Depth = amount) ---
class Tremolo
{
    float phase = 0.0f, inc = 0.0f, sr = 48000.0f;
public:
    void setSampleRate(float s) { sr = s > 1000.0f ? s : 48000.0f; }
    void setSpeed(float speed) { const float hz = 1.8f + 8.0f * clamp01(speed); inc = 2.0f * kPi * hz / sr; }
    inline float tick(float depth)
    {
        phase += inc; if (phase > 2.0f * kPi) phase -= 2.0f * kPi;
        const float lfo = 0.5f - 0.5f * std::cos(phase);   // 0..1
        return 1.0f - clamp01(depth) * 0.92f * lfo;
    }
    void reset() { phase = 0.0f; }
};

class EN30Core
{
    float sampleRate = 48000.0f;
    float normalVol = kEN30Def[kNormalVol];
    float tbVol    = kEN30Def[kTBVol];
    float treble   = kEN30Def[kTreble];
    float bass     = kEN30Def[kBass];
    float revTone  = kEN30Def[kRevTone];   // Reverb Tone  VR5 A500K
    float revLevel = kEN30Def[kRevLevel];  // Reverb Level VR6 B100K
    float speed    = kEN30Def[kSpeed];
    float depth    = kEN30Def[kDepth];
    float cut_     = kEN30Def[kCut];        // Tone Cut     VR9 B220K  [RS Pres inv]
    float master   = kEN30Def[kMaster];
    float input_   = kEN30Def[kInput];      // Normal(0)/Both(0.5)/Top Boost(1)
    float bright   = kEN30Def[kBright];     // Top Boost bright-cap amount [RS Bright]

    // Top Boost (Brilliant) preamp chain, faithful to the AC30C2:
    //   V1a 12AX7 gain -> TB Volume (+C15 bright cap) -> V1b cathode follower ->
    //   passive Top Boost stack -> OP-AMP recovery (NJM2147, clean makeup) ->
    //   [FX loop, unmodelled] -> V3 12AX7 driver -> Tone Cut -> PI -> EL84s.
    RcLowPass inputGridStopper;
    RcHighPass v1Bright120p;      // C15 120pF treble-bypass on the TB Volume
    TriodeStage v1aFirstTriode;   // V1a 12AX7 first gain stage
    CathodeFollower v1bFollower;  // V1b 12AX7 cathode follower driving the stack
    TopBoostToneStack topBoost;
    RcHighPass opAmpCoupling;     // coupling into the op-amp recovery (22n/470k)
    TriodeStage v3Driver;         // V3 12AX7 driver (post-FX-loop), feeds the Cut/PI
    CutNetwork cut;
    Biquad piRolloff;             // long-tailed-pair + coupling bandwidth limit
    El84PowerAmp powerAmp;
    RcHighPass normalHp;          // Normal channel input
    RcLowPass normalLp;
    Biquad normalMid;             // Normal channel mid-forward voicing (fills scoop)
    SpringReverb spring;
    RcLowPass revToneDark;        // Reverb Tone: fixed dark LP, crossfaded to bright
    Tremolo trem;

    // Clean op-amp recovery gain that makes up the passive Top Boost stack loss
    // (~0.59 at default -> ~1.5x net) plus a little headroom; tubes add the grit.
    static constexpr float kOpAmpRecoveryGain = 2.6f;

    Biquad transformerLow;
    Biquad speakerHp;
    Biquad speakerBody;
    Biquad speakerChime;
    Biquad speakerFizzNotch;
    Biquad speakerLp;
    DcBlock dcBlock;

    // The AC30C2 has only the real panel pots plus the channel-jumper input and
    // the Top Boost brilliance cap. Internal derivation:
    //   gain (brilliant drive) <- TB Vol ;  pres (open/cut) <- 1 - Tone Cut ;
    //   b (Top Boost brightness) <- Bright bright-cap amount.
    void updateComponentValues()
    {
        const float gain = tbVol;
        const float pres = 1.0f - clamp01(cut_);
        const float mid  = 0.5f;             // Top Boost stack has no mid pot
        const float b    = clamp01(bright);  // Top Boost brilliance (RS Bright)
        const float g = smoothstep(gain);
        const float hot = smoothstepRange(0.52f, 0.96f, gain);

        (void)mid;
        // Input jack/grid: AC30C2 uses a 56k grid stopper into the 1M grid leak,
        // with the first-stage Miller capacitance setting the ultrasonic rolloff.
        inputGridStopper.setRC(sampleRate, 56000.0f, 55.0e-12f);

        // Brilliant volume treble bypass cap C15 120pF (always present, no switch).
        v1Bright120p.setRC(sampleRate, 470000.0f, 120.0e-12f);

        // V1a 12AX7: 100k plate (R12), 1k5 cathode (R16) with 22u bypass (C11).
        v1aFirstTriode.set(sampleRate,
                           1.0e-6f, 1000000.0f,
                           56000.0f, 55.0e-12f,
                           1500.0f, 22.0e-6f,
                           100000.0f, 1.15f, -0.075f,
                           1.18f, 0.58f, 0.54f);

        v1bFollower.set(sampleRate);
        topBoost.update(treble, bass);

        // Op-amp recovery (NJM2147) after the lossy Top Boost network: clean
        // makeup, no tube saturation here. Input coupling 22n into ~470k.
        opAmpCoupling.setRC(sampleRate, 470000.0f, 22.0e-9f);

        // V3 12AX7 driver (post-FX-loop), feeds the Tone Cut + phase inverter.
        // Modest drive — it is a driver, adds warmth/compression only when cranked.
        v3Driver.set(sampleRate,
                     22.0e-9f, 470000.0f,
                     56000.0f, 80.0e-12f,
                     1500.0f, 22.0e-6f,
                     100000.0f, 1.05f, -0.085f,
                     1.30f, 0.60f, 0.46f);

        cut.set(sampleRate, pres);
        // Long-tailed-pair phase inverter + interstage coupling: a gentle HF
        // bandwidth limit (the PI is not infinite-bandwidth) before the EL84s.
        piRolloff.setLowPass(sampleRate, 11000.0f, 0.70f);
        powerAmp.setSampleRate(sampleRate);

        transformerLow.setPeaking(sampleRate, 95.0f, 0.72f, -1.5f + 1.8f * bass);
        speakerHp.setHighPass(sampleRate, 72.0f, 0.72f);
        speakerBody.setPeaking(sampleRate, 335.0f, 0.80f, 1.2f + 1.5f * bass - 0.5f * hot);
        // 2x12 chime band — a fixed speaker voicing. It no longer tracks the Tone
        // Cut (Cut now lives solely in CutNetwork); only the brilliance cap nudges
        // it, consistent with the real bright cap reaching the output.
        speakerChime.setPeaking(sampleRate, 2400.0f, 0.74f, 2.6f + 1.6f * b);
        speakerFizzNotch.setPeaking(sampleRate, 4700.0f, 0.9f, -2.3f - 0.7f * g);
        // Fixed 2x12 top-end rolloff (the Greenback/Blue character); the AC30
        // chime sits just under it. Bright cap lifts the corner a touch.
        speakerLp.setLowPass(sampleRate, 7000.0f + 1100.0f * b, 0.62f);

        // Normal channel: darker than Top Boost (no treble network) and
        // mid-forward — jumpering it in (Input=Both, Normal Vol up = RS Mid)
        // fills the Top Boost mid-scoop. Band-limit + a broad ~640 Hz mid hump.
        normalHp.setHz(sampleRate, 70.0f);
        normalLp.setHz(sampleRate, 5200.0f);
        normalMid.setPeaking(sampleRate, 640.0f, 0.80f, 4.5f);

        // Reverb Tone (VR5 A500K, C47 47pF bright tap + C49 10nF dark tap): a
        // crossfade between a fixed dark low-pass (~1.2 kHz) and the full-band
        // spring return, so the top half of the control genuinely adds brightness
        // instead of sweeping uselessly above the spring's own roll-off.
        revToneDark.setHz(sampleRate, 1200.0f);
        trem.setSpeed(speed);
    }

public:
    void reset()
    {
        inputGridStopper.reset();
        normalHp.reset();
        normalLp.reset();
        normalMid.reset();
        spring.clear();
        revToneDark.reset();
        trem.reset();
        v1Bright120p.reset();
        v1aFirstTriode.reset();
        v1bFollower.reset();
        topBoost.reset();
        opAmpCoupling.reset();
        v3Driver.reset();
        cut.reset();
        piRolloff.reset();
        powerAmp.reset();
        transformerLow.reset();
        speakerHp.reset();
        speakerBody.reset();
        speakerChime.reset();
        speakerFizzNotch.reset();
        speakerLp.reset();
        dcBlock.reset();
        updateComponentValues();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        topBoost.setSampleRate(sampleRate);
        spring.setSampleRate(sampleRate);
        trem.setSampleRate(sampleRate);
        reset();
    }

    void setNormalVol(float v){ normalVol = clamp01(v); }
    void setTBVol(float v)    { tbVol = clamp01(v);    updateComponentValues(); }
    void setTreble(float v)   { treble = clamp01(v);   updateComponentValues(); }
    void setBass(float v)     { bass = clamp01(v);     updateComponentValues(); }
    void setRevTone(float v)  { revTone = clamp01(v);  updateComponentValues(); }
    void setRevLevel(float v) { revLevel = clamp01(v); }
    void setSpeed(float v)    { speed = clamp01(v);    updateComponentValues(); }
    void setDepth(float v)    { depth = clamp01(v); }
    void setCut(float v)      { cut_ = clamp01(v);     updateComponentValues(); }
    void setMaster(float v)   { master = clamp01(v); }
    void setInput(float v)    { input_ = clamp01(v); }
    void setBright(float v)   { bright = clamp01(v);   updateComponentValues(); }

    float process(float in)
    {
        const float b = clamp01(bright);
        // Input cable selector: 0 = Normal jack, 0.5 = Both (jumpered), 1 = Top
        // Boost jack. Gate each channel's output (both stages always run so their
        // filter states stay live and switching is click-free).
        const float inp = clamp01(input_);
        const float tbG   = clamp01(inp * 2.0f);          // on at Both + Top Boost
        const float nGate = clamp01((1.0f - inp) * 2.0f); // on at Normal + Both

        // Top Boost channel drive comes ONLY from its own TB Volume (RS Gain).
        const float gTb   = smoothstep(tbVol);
        const float hotTb = smoothstepRange(0.52f, 0.96f, tbVol);

        float x = inputGridStopper.process(in);

        // --- Brilliant (Top Boost) channel — the Rocksmith path ---
        // V1a -> TB Volume (C15 120pF bright bypass, scaled by Bright) -> V1b
        // cathode follower -> passive Top Boost stack -> op-amp recovery (clean) ->
        // V3 driver tube.
        float v1 = v1aFirstTriode.process(x, 0.55f + 0.45f * hotTb, 1.0f);
        const float volume = 0.16f + 1.62f * gTb + 0.88f * hotTb;   // TB Volume
        const float brightBleed = (1.0f - 0.42f * gTb) * (0.05f + 0.20f * b) * v1Bright120p.process(v1);
        float tbOut = v1 * volume + brightBleed;
        tbOut = v1bFollower.process(tbOut);
        tbOut = topBoost.process(tbOut);
        // Op-amp recovery: CLEAN makeup gain (NJM2147), not a saturating tube.
        // softClip(r/3)*3 is transparent for |r| < ~1.5 and only soft-limits at
        // the op-amp rails under extreme drive — the grit comes from the tubes.
        {
            const float r = opAmpCoupling.process(tbOut) * kOpAmpRecoveryGain;
            tbOut = softClip(r * 0.333f) * 3.0f;
        }
        // V3 driver tube adds the tube warmth/compression before the Cut/PI.
        tbOut = v3Driver.process(tbOut, 0.70f + 0.55f * gTb + 0.40f * hotTb, 1.0f);

        // --- Normal channel — darker, mid-forward. Jumpered in by Input=Both and
        //     its own Normal Vol (RS Mid rides this to fill the Top Boost scoop). ---
        float nrm = normalMid.process(normalLp.process(normalHp.process(x)));
        nrm = softClip(nrm * (1.4f + 3.0f * normalVol));
        nrm *= (0.62f + 1.25f * normalVol);

        float y = tbOut * tbG + nrm * nGate;

        // Effective drive into the power amp + makeup follows the ACTIVE channel,
        // so in Normal mode the TB Volume has no effect: Normal(normalVol) ->
        // Both/Top Boost(tbVol). (Top Boost dominates the drive once jumpered.)
        const float effGain = inp < 0.5f
            ? normalVol + (tbVol - normalVol) * (inp * 2.0f)
            : tbVol;
        const float g   = smoothstep(effGain);
        const float hot = smoothstepRange(0.52f, 0.96f, effGain);

        // --- Tone Cut (VR9 + C80 4n7) at the V3 driver node + Master volume ---
        y = cut.process(y);
        y *= (0.85f + 0.30f * master);

        // --- spring reverb: SEND is tapped from the preamp (here), RETURN mixed
        //     back BEFORE the power amp, so the wet goes through the EL84s + speaker
        //     like the real amp (was previously applied to the speaker output). ---
        if (revLevel > 0.001f)
        {
            const float sp = spring.process(y);
            const float dark = revToneDark.process(sp);
            const float wet = dark + clamp01(revTone) * (sp - dark);  // dark<->bright
            y += wet * (0.70f * revLevel);
        }

        // --- tremolo: the Q4 MOSFET shunts the bus at the PI input, so modulate
        //     here (pre power amp), not on the final speaker output. ---
        if (depth > 0.001f)
            y *= trem.tick(depth);

        // --- long-tailed-pair phase inverter (bandwidth limit + gentle edge) ---
        y = piRolloff.process(y);
        y = 0.97f * y + 0.03f * softClip(y * 1.6f);

        // --- 4x EL84 power amp, solid-state rectified (tight, only gentle sag) ---
        y = powerAmp.process(y, effGain, hot);
        y = dcBlock.process(y);

        // --- output transformer + 2x12 Celestion ---
        y = transformerLow.process(y);
        y = speakerHp.process(y);
        y = speakerBody.process(y);
        y = speakerChime.process(y);
        y = speakerFizzNotch.process(y);
        y = speakerLp.process(y);

        // --- output makeup: holds Rocksmith loudness ~constant vs TB Vol and vs
        //     the jumpered Normal channel (RS Mid), so the level stays at the
        //     reference while the tone/mids shift. ---
        const float normalBlend = nGate * normalVol;   // how much Normal is summed in
        const float toneEnergy = 1.0f
            + 0.013f * std::fabs((bass - 0.5f) * 20.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 22.0f)
            + 0.12f * normalBlend;
        // Loudness compensation: the TB Volume (= RS Gain) drives the preamp over
        // a ~22 dB range, so WITHOUT compensation cranking the gain blasts the
        // output. We divide the makeup by (a power of) that same preamp-drive
        // proxy so the output stays ~flat across the gain sweep, with only a gentle
        // few-dB rise when cranked (exponent < 1 leaves a little of the natural
        // "louder when hot" behaviour). Previously the makeup *rose* with gain.
        // Loudness vs Gain: the raw amp is loudest around mid TB Volume and, past
        // that, the power amp saturates so the output naturally PLATEAUS/eases off
        // — i.e. cranking the gain does NOT make a real AC30 keep getting louder.
        // So the makeup only lifts the clean->breakup region (g up to ~0.5) and
        // then stays FLAT; it must not rise into the saturated zone or max gain
        // blasts. (The previous makeup rose all the way to the top.)
        const float liftG = (g < 0.5f) ? g : 0.5f;
        const float makeup = 1.0f + 0.70f * liftG;
        const float level = (0.66f * makeup * (0.55f + 0.62f * master)) / toneEnergy;
        return softClip(y * level) * 0.98f;
    }
};

} // namespace en30

#endif // EN30_CORE_H
