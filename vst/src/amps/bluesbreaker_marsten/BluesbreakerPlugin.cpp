/*
 * MARSTEN BLUESBREAKER - Marshall 1962 Bluesbreaker combo for the game's
 * Amp_Marshall1962Bluesbreaker. Parody brand "Marsten" (same as the Plexi /
 * DSL100 / JCM800); the in-app face must never read "Marshall".
 *
 * CIRCUIT-REAL DSP (schematic-first), built component-by-component off:
 *   amps/Marshall Bluesbreaker/Marshall_bluesbreaker_reissue_45w_1962.pdf
 *   ("1962 STD Reissue Valve Tremolo Combo", PCB JMP44A).
 *
 * The 1962 is the JTM45 circuit in a 2x12 combo + a power-amp TREMOLO. Lineage:
 * Bassman 5F6-A -> JTM45 -> 1962, so the gain-staging MIRRORS the tw40 (Bender
 * Bassman) build verbatim, adapted only for the real 1962 differences found on
 * the schematic.
 *
 * REAL stages modelled (Notes box: V1-3,6 = ECC83; V4,V5 = 5881/6L6 family; V7 = GZ34):
 *   V1a (ECC83)  Ch II input gain stage  (R12 68k grid stop, 1M+68k leak,
 *                plate R31 150k, shared cathode R10 820R // C3 330uF = fully
 *                bypassed -> max gain). -> rbtube::TubeStage (12AX7).
 *   V1b (ECC83)  Ch I  input gain stage  (R14/R13 68k, same shared cathode).
 *                -> rbtube::TubeStage (12AX7).
 *     Volume I = VR2 A1M (Ch I), Volume II = VR1 A1M (Ch II), 220p bright caps
 *     C5/C6 + 100p across the pots (the bright-cap shelves).
 *   V2a (ECC83)  recovery / mixer stage  (plate R32 100k, mix R15 1k / R19 100k).
 *   V2b (ECC83)  cathode follower into the tone stack.
 *                Both folded into one rbtube::TubeStage recovery stage.
 *   TONE STACK   Marshall FMV/TMB, JTM45 values: Treble VR3 220k (C7 220p),
 *                Bass VR5 1M (C8 22n / C9 22n), Mid VR4 22k, slope R20 56k.
 *                -> rbtube::ToneStackYeh (double; 3rd-order float NaNs at 192k).
 *   V3 (ECC83)   long-tail-pair phase inverter (R23 470R tail, R22/R24 1M,
 *                plates R33 82k / R34 100k, C14 47p).
 *   V4 + V5      2x 5881 push-pull, class AB, ~30W combo. GZ34 rectifier
 *                (warm, sag-y). Global NFB R25 27k from the 16ohm tap. Screen
 *                470R 5W. -> rbtube::PowerAmp5881 (sag + OT band-pass).
 *   PRESENCE     VR6 4k7 taps the NFB loop -> presence high-shelf (NFB-approx).
 *   TREMOLO      V6 (ECC83) phase-shift LFO + J174 FET amplitude-modulates the
 *                power-amp output. SPEED = VR8 1M (rate), INTENSITY = VR7 220k
 *                (depth, 0 = OFF). Deterministic per-sample phase accumulator.
 *
 * the game: no gain knob, so RS Gain -> LOUDNESS 1 (clean->crunch->roar);
 * Treble/Bass/Mid -> tone stack, Pres -> Presence. Tremolo off by default
 * (Intensity 0). Cab Sim stays on until the host supplies a cabinet/IR.
 * See rs_knob_to_vst_param.json.
 *
 * Runs ONE mono core at 2x oversampling (rbshared::Oversampler4x, OS=2) with a
 * dual-mono output (the amp IS a mono device) - matches tw40/en30/tw26.
 */
#include "DistrhoPlugin.hpp"
#include "BluesbreakerParams.h"
#include "../../_shared/tube_stage.hpp"   // real 12AX7 / 6L6 circuit models + Yeh tone stack
#include "../../_shared/oversampler.hpp"  // 2x anti-alias around the nonlinear chain
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts
// never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

namespace {

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

} // namespace

class BluesbreakerCore
{
    float sampleRate = 48000.0f;
    float speed     = kBluesbreakerDef[kSpeed];
    float intensity = kBluesbreakerDef[kIntensity];
    float pres      = kBluesbreakerDef[kPresence];
    float bass      = kBluesbreakerDef[kBass];
    float mid       = kBluesbreakerDef[kMiddle];
    float treble    = kBluesbreakerDef[kTreble];
    float loud1     = kBluesbreakerDef[kLoudness1];
    float loud2     = kBluesbreakerDef[kLoudness2];
    float input     = kBluesbreakerDef[kInput];
    float cabSim    = kBluesbreakerDef[kCabSim];

    // derived: channel gating from the input cable + the loudness-as-gain proxy.
    float brightG = 1.0f, normalG = 1.0f;
    float effDrive = 0.5f;

    // tremolo LFO (per-sample phase accumulator; deterministic, no Date/rand)
    float tremPhase = 0.0f;

    Biquad inputHp;
    Biquad pickupLoad;
    Biquad brightCapShelf;
    Biquad brightBody;
    Biquad normalBody;
    Biquad interstageHp;
    Biquad cathodeFollowerLp;
    rbtube::ToneStackYeh toneStack;          // Marshall FMV/TMB, JTM45 values (REAL, double)
    Biquad stackMakeupLow;
    Biquad stackMakeupBody;
    Biquad phaseLowPass;
    Biquad presenceShelf;
    Biquad speakerHp;
    Biquad speakerThump;
    Biquad speakerLowMid;
    Biquad speakerBite;
    Biquad speakerFizzNotch;
    Biquad speakerLp;
    DcBlock dcBlock;

    // ── REAL tube stages (Koren circuit models) replacing the tanh asymTube ──
    rbtube::TubeStage   brightTube, normalTube;  // V1a/V1b ECC83 input stages (per channel)
    rbtube::TubeStage   recoveryTube;            // V2a/V2b ECC83 recovery/CF into the stack
    rbtube::Miller12AX7 brightMiller, normalMiller, recoveryMiller;
    rbtube::CouplingCapGridLeak brightCoupleToRecovery; // V1b -> Volume I -> mixer/V2a
    rbtube::CouplingCapGridLeak normalCoupleToRecovery; // V1a -> Volume II -> mixer/V2a
    rbtube::CouplingCapGridLeak coupleToPi;       // V2 -> V3 coupling + grid blocking
    rbtube::PhaseInverterLTP12AX7 phaseInverter;  // V3 ECC83 long-tail pair
    rbtube::MultiNodeBPlus supply;                // GZ34 + B+ filter-node sag
    rbtube::PowerAmp5881 power;                   // V4/V5 2x 5881 push-pull (~30W combo)
    float lastPowerLoad = 0.0f;
    float lastScreenLoad = 0.0f;
    float lastPreampLoad = 0.0f;

    static float eqDb(float normalized, float rangeDb)
    {
        return (clamp01(normalized) - 0.5f) * 2.0f * rangeDb;
    }

    void updateFilters()
    {
        // Input cable: Ch I(<0.25) / Both(jumpered, 0.25-0.75) / Ch II(>=0.75).
        brightG = (input < 0.75f) ? 1.0f : 0.0f;
        normalG = (input >= 0.25f) ? 1.0f : 0.0f;
        // The Loudness (Volume I/II) pots are the gain; jumpered (both channels)
        // drives the amp harder. Volume I (with the brighter bright cap) is the
        // primary voice.
        effDrive = clamp01(brightG * loud1 + normalG * loud2 * 0.80f);

        const float g = smoothstep(effDrive);
        const float pushed = smoothstepRange(0.40f, 0.92f, effDrive);

        // ── real ECC83 (12AX7) / 5881 circuit stages (cathode-biased, self-bias solved) ──
        // V1 shares one cathode R10 820R // C3 330uF -> fully bypassed (fck ~0.6 Hz)
        // = the high-gain Marshall input. V2 recovery on its own bypassed cathode.
        brightTube.set(sampleRate, 1, 250.0f, 40.0f, 3.0f, 820.0f);    // V1b Ch I input (ECC83)
        normalTube.set(sampleRate, 1, 250.0f, 40.0f, 3.0f, 820.0f);    // V1a Ch II input (ECC83)
        recoveryTube.set(sampleRate, 1, 250.0f, 40.0f, 55.0f, 1500.0f);// V2 recovery / CF (ECC83)
        brightMiller.set(sampleRate, 68000.0f, 55.0f, 8.0f);
        normalMiller.set(sampleRate, 68000.0f, 55.0f, 8.0f);
        recoveryMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        // V1 coupling caps, the A1M Loudness pots and the 270k-ish mixer impedance
        // into V2a. Keeping separate states preserves the jumpered-channel feel.
        brightCoupleToRecovery.set(sampleRate, 1000000.0f, 22.0e-9f, 270000.0f,
                                   0.30f, 0.06f, 0.18f);
        normalCoupleToRecovery.set(sampleRate, 1000000.0f, 22.0e-9f, 270000.0f,
                                   0.30f, 0.05f, 0.16f);
        // V2 -> V3 coupling cap into the LTP input grid. The grid-leak recovery is
        // what gives the 1962 its softer "bloom" when Loudness is high.
        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 100000.0f,
                       0.30f, 0.06f, 0.22f);
        phaseInverter.setMarshall(sampleRate, 0.95f + 1.45f * effDrive + 0.70f * pushed, 0.88f);
        // GZ34 + Marshall 45W/JTM45-family filter chain: moderate reservoir,
        // screen/PI node and slower preamp node. This replaces a single sag scalar
        // with node-dependent compression.
        supply.set(sampleRate,
                   115.0f, 32.0f,
                   8200.0f, 32.0f,
                   10000.0f, 16.0f,
                   0.21f + 0.08f * pushed,
                   0.15f + 0.06f * pushed,
                   0.08f + 0.03f * effDrive,
                   0.20f);
        // 2x 5881 push-pull, fixed bias. Keep it warm enough that clean notes
        // stay in class-AB conduction instead of crossing over/chopping.
        power.set(sampleRate, 5.0f + 7.6f * effDrive + 8.4f * pushed, -38.0f, 0.24f, 50.0f, 11000.0f);
        power.out = 0.010f;

        // 220p bright caps (C5/C6) bleed treble across Volume I/II, most at low
        // settings; plus base sparkle from Treble/Presence (Marshall is bright,
        // but the JTM45 is warmer than the plexi -> a touch less bright).
        const float bright = clamp01(0.32f * treble + 0.18f * pres + 0.48f * (1.0f - loud1));

        inputHp.setHighPass(sampleRate, 42.0f + 54.0f * g + 28.0f * pushed, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12600.0f - 1600.0f * pushed + 900.0f * treble, 0.64f);
        brightCapShelf.setHighShelf(sampleRate, 1500.0f + 1100.0f * treble, 0.70f,
                                    -1.0f + 5.6f * bright + 1.7f * pres);
        brightBody.setPeaking(sampleRate, 700.0f + 420.0f * mid, 0.80f,
                              -0.7f + 2.9f * mid + 0.9f * bright);
        normalBody.setPeaking(sampleRate, 175.0f + 55.0f * bass, 0.72f,
                              0.8f + 2.6f * bass - 1.0f * pushed);

        interstageHp.setHighPass(sampleRate, 58.0f + 76.0f * pushed + 38.0f * (1.0f - bass), 0.70f);
        cathodeFollowerLp.setLowPass(sampleRate, 8800.0f + 1600.0f * treble - 1400.0f * pushed, 0.64f);
        // Marshall FMV/TMB tone stack, JTM45 / 1962 component values off the
        // schematic: Treble VR3 220k (C7 220p), Bass VR5 1M (C8 22n), Mid VR4 22k
        // (C9 22n), slope R20 56k. (220pF treble cap + 56k slope = warmer, less
        // aggressive top than the 1959SLP plexi's 500pF/33k.)
        toneStack.setComponents(220.0e3, 1.0e6, 22.0e3, 56.0e3,
                                220.0e-12, 22.0e-9, 22.0e-9);
        toneStack.update(sampleRate, treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f + 30.0f * bass, 0.72f,
                                   eqDb(bass, 4.6f) - 1.2f * pushed);
        stackMakeupBody.setPeaking(sampleRate, 520.0f + 180.0f * mid, 0.66f,
                                   -0.8f + 4.6f * mid + 1.4f * pushed);  // JTM45 mid warmth
        phaseLowPass.setLowPass(sampleRate, 10500.0f + 1300.0f * treble + 1000.0f * pres
                                            - 2000.0f * pushed, 0.64f);
        presenceShelf.setHighShelf(sampleRate, 2600.0f + 850.0f * pres, 0.78f,
                                   -3.6f + 8.4f * pres + 0.9f * treble);

        // Marshall 2x12 combo (Celestion-ish): tight HP, low thump, upper-mid
        // bite, fizz notch + rolloff. A combo 2x12 is a touch darker/woodier
        // than the plexi 4x12.
        speakerHp.setHighPass(sampleRate, 80.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 112.0f, 0.84f, 0.9f + 2.3f * bass);
        speakerLowMid.setPeaking(sampleRate, 370.0f + 90.0f * mid, 0.78f,
                                 0.8f + 1.9f * mid - 0.7f * pushed);
        speakerBite.setPeaking(sampleRate, 2650.0f + 480.0f * treble, 0.74f,
                               2.5f + 2.0f * treble + 1.0f * pres - 0.5f * pushed);   // the Marshall crunch bite
        // Cab top: a real combo 2x12 ATTENUATES the extreme top (negative shelf +
        // LP), it does NOT add fizz. (Keeps the crest honest -- a positive fizz
        // shelf inflates crest without distorting.)
        speakerFizzNotch.setHighShelf(sampleRate, 4700.0f, 0.70f,
                                      -2.0f + 1.5f * treble + 1.0f * pres - 3.5f * pushed);
        speakerLp.setLowPass(sampleRate, 12500.0f + 1500.0f * treble + 700.0f * pres
                                         - 3000.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); brightCapShelf.reset();
        brightBody.reset(); normalBody.reset();
        interstageHp.reset(); cathodeFollowerLp.reset();
        toneStack.reset(); stackMakeupLow.reset(); stackMakeupBody.reset();
        phaseLowPass.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset();
        speakerBite.reset(); speakerFizzNotch.reset(); speakerLp.reset();
        dcBlock.reset();
        tremPhase = 0.0f;
        brightMiller.reset(); normalMiller.reset(); recoveryMiller.reset();
        brightTube.reset(); normalTube.reset(); recoveryTube.reset();
        brightCoupleToRecovery.reset(); normalCoupleToRecovery.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kSpeed:     speed = v; break;
            case kIntensity: intensity = v; break;
            case kPresence:  pres = v; break;
            case kBass:      bass = v; break;
            case kMiddle:    mid = v; break;
            case kTreble:    treble = v; break;
            // Real pots at zero mute the amp, but game "Gain low" must mean a
            // clean Bluesbreaker, not silence. Keep a small floor, then apply
            // the pot only once after V1.
            case kLoudness1: loud1 = std::fmax(0.26f, v); break;
            case kLoudness2: loud2 = std::fmax(0.24f, v); break;
            case kInput:     input = v; break;
            case kCabSim:    cabSim = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults()
    {
        for (int i = 0; i < kParamCount; ++i)
            setParam(i, kBluesbreakerDef[i]);
    }

    float process(float in)
    {
        const float pushed = smoothstepRange(0.40f, 0.92f, effDrive);
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = softClip(x * (1.05f + 0.10f * pushed)) * (0.95f - 0.04f * pushed);

        // Ch I (bright/lead): 220p bright cap + body, its own REAL ECC83 (V1b).
        // Volume I is after V1, so it attenuates the V1 output once.
        float bch = brightCapShelf.process(brightBody.process(x));
        bch = brightTube.process(brightMiller.process(bch) * 4.55f * bplus.preamp);
        bch = brightCoupleToRecovery.process(bch * loud1, 0.82f + 2.55f * loud1);
        // Ch II (normal): darker body, its own REAL ECC83 (V1a).
        float nch = normalBody.process(x);
        nch = normalTube.process(normalMiller.process(nch) * 3.95f * bplus.preamp);
        nch = normalCoupleToRecovery.process(nch * loud2, 0.72f + 2.15f * loud2);

        // Jumpered mix: channel outputs are already scaled by their Loudness pots
        // before the coupling/grid-leak state.
        float y = brightG * bch + normalG * 0.92f * nch;

        // V2 ECC83 recovery / cathode follower into the tone stack (REAL).
        y = interstageHp.process(y);
        y = recoveryTube.process(recoveryMiller.process(y) *
                                 (10.0f + 2.2f * effDrive) * bplus.preamp);
        y = cathodeFollowerLp.process(y);

        y = toneStack.process(y) * 2.25f;
        y = stackMakeupLow.process(y);
        y = stackMakeupBody.process(y);
        y = phaseLowPass.process(y);
        y = coupleToPi.process(y, 1.0f + 0.25f * pushed);
        lastPreampLoad = 0.15f * std::fabs(y) + 0.04f * effDrive;
        y = phaseInverter.process(y * bplus.preamp);
        lastPowerLoad = 0.85f * std::fabs(y) + 0.20f * pushed;
        lastScreenLoad = 0.50f * std::fabs(y) + 0.10f * effDrive;

        // V4/V5 2x 5881 push-pull (~30W combo), REAL: pentode table + OT.
        // The GZ34/filter-node response is injected via the B+ scales above.
        y = power.process(y * bplus.power * bplus.screen);
        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        float cab = speakerHp.process(y);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizzNotch.process(cab);
        cab = speakerLp.process(cab);
        y += cabSim * (cab - y);

        // POWER-AMP TREMOLO: the V6 phase-shift LFO + J174 FET amplitude-
        // modulates the output. SPEED -> rate (~2.0..7.5 Hz), INTENSITY ->
        // depth (0 = OFF). Deterministic per-sample phase accumulator (no
        // Date/rand), so the offline RMS is stable with Intensity = 0.
        if (intensity > 0.0001f)
        {
            const float rateHz = 2.0f + 5.5f * clamp01(speed);
            tremPhase += 2.0f * kPi * rateHz / sampleRate;
            if (tremPhase >= 2.0f * kPi) tremPhase -= 2.0f * kPi;
            // depth: 0 -> unity, 1 -> dips to ~0.25 (vintage tube trem isn't a
            // full chop). lfo in [0,1], 1 at the peak.
            const float lfo = 0.5f * (1.0f + std::sin(tremPhase));
            const float depth = 0.75f * smoothstep(intensity);
            const float trem = 1.0f - depth * (1.0f - lfo);
            y *= trem;
        }

        // Loudness normalization (NO cleanMakeup -- it inverts the crest curve by
        // pushing the clean tone into the output softClip). A gentle level law +
        // the shared final softClip (OT clip) keeps the RS Gain (-> Loudness 1)
        // sweep musical without faking distortion.
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.010f * std::fabs((pres - 0.5f) * 16.0f);
        const float cleanPotLift = 1.0f + 4.8f * (1.0f - smoothstepRange(0.28f, 0.58f, loud1));
        const float level = cleanPotLift * (0.72f + 0.14f * (1.0f - effDrive)) /
            ((1.0f + 0.28f * effDrive + 0.32f * pushed) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class BluesbreakerPlugin : public Plugin
{
    BluesbreakerCore core;
    float params[kParamCount];
    rbshared::Oversampler4x os;                 // anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        for (int i = 0; i < kParamCount; ++i)
            core.setParam(i, params[i]);
    }

public:
    BluesbreakerPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBluesbreakerDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarstenBluesbreaker"; }
    const char* getDescription() const override { return "Marshall 1962 Bluesbreaker style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'b', '6', '2'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBluesbreakerNames[index];
        parameter.symbol = kBluesbreakerSymbols[index];
        parameter.ranges.min = kBluesbreakerMin[index];
        parameter.ranges.max = kBluesbreakerMax[index];
        parameter.ranges.def = kBluesbreakerDef[index];
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
        core.setParam((int)index, params[index]);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        core.setSampleRate(kOS * (float)newSampleRate);
        os.reset();
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* in0 = inputs[0];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            float ub[kOS];
            os.upsample(3.2f * in0[i], ub);
            for (int k = 0; k < kOS; ++k) ub[k] = rbAmpLvl(0.820f * core.process(ub[k]));
            const float y = os.downsample(ub);
            outL[i] = y;
            outR[i] = y;   // dual-mono: one core, same signal both sides = centered/balanced
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BluesbreakerPlugin)
};

Plugin* createPlugin()
{
    return new BluesbreakerPlugin();
}

END_NAMESPACE_DISTRHO
