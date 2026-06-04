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
 * The AC30C2 panel is the only source of controls: Normal Vol, TB Vol, Treble,
 * Bass, Reverb Tone, Reverb Level, Tremolo Speed/Depth, Tone Cut, Master. There
 * is NO Mid and NO Bright control (the Brilliant channel is inherently bright).
 * Rocksmith's Gain/Bass/Treble/Pres map onto TB Vol/Bass/Treble/Tone Cut(inv);
 * RS Mid + Bright have no panel target on a real AC30, so they are dropped.
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

class TopBoostToneStack
{
    RcLowPass c5Bass22n;
    RcHighPass c4Treble22n;
    RcHighPass c6Treble47p;
    Biquad interactionScoop;
    Biquad midFill;
    Biquad postTrebleShelf;
    float bassMix = 1.0f;
    float midMix = 0.55f;
    float trebleMix = 1.0f;
    float airMix = 0.35f;
    float insertionLoss = 0.36f;

public:
    void reset()
    {
        c5Bass22n.reset();
        c4Treble22n.reset();
        c6Treble47p.reset();
        interactionScoop.reset();
        midFill.reset();
        postTrebleShelf.reset();
    }

    void set(float sr, float bass, float mid, float treble, float bright)
    {
        // AC30 Top Boost: C5/C4 22n, C6 47p, R5/R6 220k, Bass/Treble A1M.
        const float pot = 1000000.0f;
        const float slope = 220000.0f;
        // C5 (Bass, 22n) sets a LOW-shelf corner. The cap does NOT see the raw 1M
        // pot — in the cathode-follower-driven network its effective (Thevenin)
        // resistance is far lower, so the audible bass corner lands in the
        // ~120-420 Hz region (it was previously computed from the raw pot value,
        // which pushed the corner to ~6-24 Hz and made the Bass knob nearly inert).
        const float bassFc = 120.0f + 300.0f * bass;          // audible bass corner
        const float rb = 1.0f / (2.0f * kPi * bassFc * 22.0e-9f);
        const float rt = slope + pot * (0.06f + 0.94f * (1.0f - treble));
        const float rAir = slope + pot * (0.03f + 0.97f * (1.0f - treble));

        c5Bass22n.setRC(sr, rb, 22.0e-9f);
        c4Treble22n.setRC(sr, rt, 22.0e-9f);
        c6Treble47p.setRC(sr, rAir, 47.0e-12f);

        const float bothUp = bass * treble;
        const float bothDown = (1.0f - bass) * (1.0f - treble);
        const float crossLoad = bass * (1.0f - treble) + treble * (1.0f - bass);

        interactionScoop.setPeaking(sr, 520.0f + 90.0f * crossLoad, 0.66f,
                                    -11.0f * bothUp + 2.6f * bothDown - 1.2f * crossLoad);
        midFill.setPeaking(sr, 680.0f + 240.0f * mid, 0.68f, -4.6f + 9.2f * mid);
        postTrebleShelf.setHighShelf(sr, 2100.0f + 900.0f * treble, 0.70f,
                                     (treble - 0.5f) * 18.5f + 2.6f * bright);

        bassMix = 0.42f + 2.30f * bass;
        midMix = 0.50f + 0.55f * mid - 0.18f * bothUp;
        trebleMix = 0.18f + 1.40f * treble;
        airMix = (0.08f + 0.85f * treble) * (bright >= 0.5f ? 1.0f : 0.42f);
        insertionLoss = 0.31f + 0.10f * mid - 0.035f * bothUp;
    }

    float process(float x)
    {
        const float low = c5Bass22n.process(x) * bassMix;
        const float high = c4Treble22n.process(x) * trebleMix;
        const float air = c6Treble47p.process(x) * airMix;
        const float mid = x * midMix;
        float y = (low + mid + high + air) * insertionLoss;
        y = interactionScoop.process(y);
        y = midFill.process(y);
        y = postTrebleShelf.process(y);
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
        // VR1 A220k and C1/C2 4n7 cancel highs between PI plates. Wider/deeper
        // so the Cut (Rocksmith Pres) is clearly audible (it was too subtle once
        // the power amp regenerated highs): corner from ~1.5 kHz (full cut) up,
        // with a stronger removal depth.
        const float rCut = 22000.0f + 185000.0f * pres;
        c1c2Cut4n7.setRC(sr, rCut, 4.7e-9f);
        amount = 0.85f * (1.0f - pres);
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

    float process(float x, float gain, float hot, float bass, float treble)
    {
        float grid = c24c25Coupling100n.process(x);
        const float env = screenNode.process(std::fabs(grid));
        const float supplyScale = supply.process(env, gain, hot);
        const float drive = (1.02f + 1.45f * gain + 1.25f * hot) * supplyScale;
        float y = el84PentodePair(grid * drive, 0.03f + 0.012f * (treble - bass));
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
        inLp.setLowPass(sr, 3600.0f, 0.7f);     // and the highs ("boing")
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
    float revTone  = kEN30Def[kRevTone];   // Reverb Tone  VR8 A100K
    float revLevel = kEN30Def[kRevLevel];  // Reverb Level VR5 A500K
    float speed    = kEN30Def[kSpeed];
    float depth    = kEN30Def[kDepth];
    float cut_     = kEN30Def[kCut];        // Tone Cut     VR9 B220K  [RS Pres inv]
    float master   = kEN30Def[kMaster];
    float input_   = kEN30Def[kInput];      // Normal(0)/Both(0.5)/Top Boost(1)
    float bright   = kEN30Def[kBright];     // Top Boost bright-cap amount [RS Bright]

    RcLowPass inputGridStopper;
    RcHighPass v1Bright120p;      // C13 120pF treble-bypass on TB Volume
    TriodeStage v8aFirstTriode;
    CathodeFollower v8bFollower;
    TopBoostToneStack topBoost;
    TriodeStage v7bRecovery;
    CutNetwork cut;
    El84PowerAmp powerAmp;
    RcHighPass normalHp;          // Normal channel input
    RcLowPass normalLp;
    Biquad normalMid;             // Normal channel mid-forward voicing (fills scoop)
    SpringReverb spring;
    RcLowPass revToneLp;          // Reverb Tone: tilt/low-pass on the wet return
    Tremolo trem;

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

        // Input jack/grid: 68k stopper into 1M grid leak, with the first-stage
        // Miller capacitance setting the ultrasonic rolloff.
        inputGridStopper.setRC(sampleRate, 68000.0f, 55.0e-12f);

        // Brilliant volume treble bypass cap C13 120pF (always present, no switch).
        v1Bright120p.setRC(sampleRate, 470000.0f, 120.0e-12f);

        // V1 12AX7: 100k plate, 1k5 cathode with 22u bypass. The first coupling
        // cap is effectively direct from input for audio, so use a sub-audio HP.
        v8aFirstTriode.set(sampleRate,
                           1.0e-6f, 1000000.0f,
                           68000.0f, 55.0e-12f,
                           1500.0f, 22.0e-6f,
                           100000.0f, 1.15f, -0.075f,
                           1.18f, 0.58f, 0.54f);

        v8bFollower.set(sampleRate);
        topBoost.set(sampleRate, bass, mid, treble, b);

        // V7b recovery after the lossy Top Boost network. C36/C37 bypass parts
        // in the local schematic keep this stage full-range and chimey.
        v7bRecovery.set(sampleRate,
                        22.0e-9f, 470000.0f,
                        56000.0f, 80.0e-12f,
                        1500.0f, 10.0e-6f,
                        100000.0f, 1.28f, -0.085f,
                        1.62f, 0.62f, 0.46f);

        cut.set(sampleRate, pres);
        powerAmp.setSampleRate(sampleRate);

        transformerLow.setPeaking(sampleRate, 95.0f, 0.72f, -1.5f + 1.8f * bass);
        speakerHp.setHighPass(sampleRate, 72.0f, 0.72f);
        speakerBody.setPeaking(sampleRate, 335.0f, 0.80f, 1.2f + 1.5f * bass - 0.5f * hot);
        // Chime/presence band sits post-power-amp, so let Pres and Bright move it
        // here too — that way they stay audible even when the EL84s regenerate
        // highs (the pre-PI Cut alone was getting masked by the distortion).
        speakerChime.setPeaking(sampleRate, 2250.0f + 440.0f * treble, 0.74f,
                                2.0f + 2.3f * treble + 1.7f * b + 2.4f * pres);
        // Shallower notch that no longer deepens hard with Gain (it was killing
        // the highs exactly when the amp was driven = "distorted but muffled").
        speakerFizzNotch.setPeaking(sampleRate, 4700.0f + 380.0f * pres, 0.9f,
                                    -2.3f - 0.7f * g);
        // Open the top so the AC30 chime is there and Treble/Pres/Bright land
        // BELOW the rolloff (they were acting above it -> "they do nothing").
        speakerLp.setLowPass(sampleRate, 6600.0f + 3400.0f * pres + 1100.0f * b, 0.62f);

        // Normal channel: darker than Top Boost (no treble network) and
        // mid-forward — jumpering it in (Input=Both, Normal Vol up = RS Mid)
        // fills the Top Boost mid-scoop. Band-limit + a broad ~640 Hz mid hump.
        normalHp.setHz(sampleRate, 70.0f);
        normalLp.setHz(sampleRate, 5200.0f);
        normalMid.setPeaking(sampleRate, 640.0f, 0.80f, 4.5f);

        // Reverb Tone (VR8 A100K): a tilt/low-pass on the spring return.
        // Dark at 0 (~1.4 kHz) opening up past the spring's own 3.6 kHz roll-off.
        revToneLp.setHz(sampleRate, 1400.0f + 6000.0f * clamp01(revTone));
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
        revToneLp.reset();
        trem.reset();
        v1Bright120p.reset();
        v8aFirstTriode.reset();
        v8bFollower.reset();
        topBoost.reset();
        v7bRecovery.reset();
        cut.reset();
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
        // V1 -> TB Volume (C13 120pF bright bypass, scaled by Bright) -> cathode
        // follower -> Top Boost treble/bass stack -> V3 recovery.
        float v1 = v8aFirstTriode.process(x, 0.55f + 0.45f * hotTb, 1.0f);
        const float volume = 0.16f + 1.62f * gTb + 0.88f * hotTb;   // TB Volume
        const float brightBleed = (1.0f - 0.42f * gTb) * (0.05f + 0.20f * b) * v1Bright120p.process(v1);
        float tbOut = v1 * volume + brightBleed;
        tbOut = v8bFollower.process(tbOut);
        tbOut = topBoost.process(tbOut);
        tbOut = v7bRecovery.process(tbOut, 0.75f + 0.65f * gTb + 0.45f * hotTb, 1.0f);

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

        // --- Tone Cut (VR9 + C80 4n7) + Master volume into the power amp ---
        y = cut.process(y);
        y *= (0.85f + 0.30f * master);

        // --- 4x EL84 power amp, solid-state rectified (tight, only gentle sag) ---
        y = powerAmp.process(y, effGain, hot, bass, treble);
        y = dcBlock.process(y);

        // --- output transformer + 2x12 Celestion ---
        y = transformerLow.process(y);
        y = speakerHp.process(y);
        y = speakerBody.process(y);
        y = speakerChime.process(y);
        y = speakerFizzNotch.process(y);
        y = speakerLp.process(y);

        // --- spring reverb (parallel): VR5 Level, VR8 Tone (tilt on the wet) ---
        if (revLevel > 0.001f)
        {
            const float wet = revToneLp.process(spring.process(y));
            y += wet * (0.95f * revLevel);
        }

        // --- tremolo (amplitude LFO): VR7 Speed, VR6 Depth ---
        if (depth > 0.001f)
            y *= trem.tick(depth);

        // --- output makeup: holds Rocksmith loudness ~constant vs TB Vol and vs
        //     the jumpered Normal channel (RS Mid), so the level stays at the
        //     reference while the tone/mids shift. ---
        const float normalBlend = nGate * normalVol;   // how much Normal is summed in
        const float toneEnergy = 1.0f
            + 0.013f * std::fabs((bass - 0.5f) * 20.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 22.0f)
            + 0.12f * normalBlend;
        const float makeup = 1.0f + 0.74f * g + 0.42f * hot;
        const float level = (0.66f * makeup * (0.55f + 0.62f * master)) / toneEnergy;
        return softClip(y * level) * 0.98f;
    }
};

} // namespace en30

#endif // EN30_CORE_H
