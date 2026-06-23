/*
 * MARSTEN DSL15 - Marshall DSL15H (15 W head) for the game's Amp_MarshallDSL15H.
 * Parody brand "Marsten" (matches the GM-2 / UV-1 Marshall-copy pedals and the
 * larger Marsten DSL100); the in-app face must never read "Marshall".
 *
 * CIRCUIT-REAL DSP (schematic-first, component-by-component), mirroring the tuned
 * Marsten DSL100 (the SAME JCM2000 DSL topology). Real Koren tube tables replace
 * the old tanh stand-ins; the JCM2000 TMB is the real Yeh tone stack (double); the
 * power amp is a real push-pull pentode table; 2x oversampling around the whole
 * nonlinear chain. The ONLY tanh left is the gentle final OT soft-clip.
 *
 * Local reference (full circuit, component-by-component, READ EVERY PAGE):
 *   amps/Marshall DSL15/Marshall_DSL15_60_02_v04.pdf  ("15W MAIN BOARD" iss4)
 *
 *   --- PAGE 1 (preamp + tone stack) ---
 *   INPUT R201 1K / C201 47n / R202 1M -> CON1B
 *   V1A ECC83 : grid-stop R31 68k, grid-leak R38 1M, plate R14 100k -> HT1,
 *               cathode R15 1k8 || C16 4u7  (fck ~= 19 Hz, bypassed)
 *               out C33 22n -> RL1B (Classic/Ultra channel relay FRT3)
 *   Gain pots : VR3 A1M (Classic GAIN) / VR1 A1M (Ultra GAIN) with bright network
 *               R79 10M / R80 47k / R81 470k / C57 4n7 / C58 1n / C75 C76 470p
 *   V1B ECC83 : grid-stop R43 1k, grid-leak R42 1M, plate R16 100k -> HT2,
 *               cathode R40 1k8 || C34 1u  (fck ~= 88 Hz)
 *   V2A ECC83 : grid R44 470k / C36 47n, grid-leak R45 2M2, plate R18 100k -> HT3,
 *               cathode R17 4k7 (cold / largely unbypassed)
 *   V2B ECC83 : grid R47 470k / C29 22n, grid-leak R49 470k, plate R20 100k -> HT4,
 *               cathode R48 2k2 || C25 1n  (effectively unbypassed in-band)
 *   V3A ECC83 : grid-leak R52 100k, plate -> HT5, cathode R34 18k (cold clipper)
 *   V3B ECC83 : grid-leak R51 470k, plate R22 100k -> HT5, cathode R33 1k
 *               out C37 330n -> R50 10k -> TONE STACK
 *   TONE STACK (Marshall JCM2000 TMB) : VR5 B200K TREBLE / VR7 A1M BASS /
 *               VR6 B20K MIDDLE, slope R84 33k (+R83 68k via SW2 Tone Shift),
 *               treble cap C60 470p (+C79 470p / SW2A), bass/mid C77 C78 C59 22n.
 *               -> VR2/VR4 A1M VOLUME (Classic/Ultra) -> PREAMP_OUTPUT
 *   (Reverb: NE5532 + DFX DSP, "HEAD ONLY" R99 = combo-only -> the head has none.)
 *
 *   --- PAGE 2 (phase inverter + power + PSU) ---
 *   V4A/V4B ECC83 : long-tailed-pair phase inverter. PREAMP_OUTPUT C38 100n ->
 *               V4A grid (R35 1M leak); shared cathode R55 470R -> R54 10k tail;
 *               V4B grid R56 1M. Plates -> C31 22n / C39 100n+C32 22n -> 6V6 grids.
 *   V5A 6V6 / V6A 6V6 : PUSH-PULL POWER (NOT EL84 / NOT EL34 -- the DSL15 is the
 *               6V6 little-DSL). Beam-pentode mode, grid-stop R62/R63 5k6, FIXED
 *               bias (BIAS1/BIAS2 from R59/R60 220k), screen R36/R37 1k 5W.
 *               -> OT TXOP-91001 (~15 W).  Output Power switch = full / ~7.5 W.
 *   Presence  : VR8 C10K  -> global negative feedback (OT secondary -> PI tail).
 *   DEEP sw   : SW3A/SW3B + R94 100R + R93 220K -> low-frequency NFB resonance.
 *
 * Full front panel (Dsl15Params.h, unchanged): two footswitchable channels off
 * the shared preamp -- CLASSIC (Clean->Crunch: Gain+Vol) and ULTRA (high-gain OD:
 * Gain+Vol), shared Bass/Middle/Treble + Tone Shift, Deep switch, Presence. No
 * Resonance pot, no reverb on the head, single per-channel master (the Volume).
 *
 * the game: the RS Gain knob drives the channel morph (Classic clean -> Crunch ->
 * Ultra), matching the gain_variants split. See rs_knob_to_vst_param.json.
 */
#include "DistrhoPlugin.hpp"
#include "Dsl15Params.h"
#include "../../_shared/tube_stage.hpp"   // real ECC83/12AX7 stages + 6V6 PP + Yeh tone stack
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): kLvl matches the
// amp to the common multitone loudness; the soft knee is transparent below
// +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts never hard-clip.
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
    return std::fmax(20.0f, std::fmin(hz, sr * 0.45f));
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

// only the final OT-clip soft saturation uses tanh; every gain stage is a real tube
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
        const float inv = 1.0f / na0;
        b0 = nb0 * inv;
        b1 = nb1 * inv;
        b2 = nb2 * inv;
        a1 = na1 * inv;
        a2 = na2 * inv;
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

} // namespace

class Dsl15Core
{
    float sampleRate = 48000.0f;
    // panel params
    float channel      = kDsl15Def[kChannel];
    float classicGain  = kDsl15Def[kClassicGain];
    float classicVol   = kDsl15Def[kClassicVolume];
    float ultraGain    = kDsl15Def[kUltraGain];
    float ultraVol     = kDsl15Def[kUltraVolume];
    float presence     = kDsl15Def[kPresence];
    float bass         = kDsl15Def[kBass];
    float mid          = kDsl15Def[kMiddle];
    float treble       = kDsl15Def[kTreble];
    float deepSw        = kDsl15Def[kDeep];
    float toneShiftSw  = kDsl15Def[kToneShift];
    float cabSim       = kDsl15Def[kCabSim];

    // derived (recomputed in updateFilters)
    float m = 0.7f;        // channel/drive morph: 0 = Classic clean .. 1 = Ultra max
    float chS = 1.0f;      // smoothed channel position (0 Classic .. 1 Ultra)
    float chVol = 0.5f;    // active channel volume
    float ts = 0.0f;       // effective Tone Shift depth
    float crunchA = 0.0f;  // crunch amount
    float ultraA = 0.0f;   // ultra amount
    float deep = 0.0f;     // Deep switch resonance amount

    Biquad inputHp, inputLp, brightShelf;
    Biquad cleanBody, crunchBody, ultraTight, ultraBite;
    Biquad interHp, interLp;
    Biquad toneShiftMid, toneShiftBite;
    Biquad phaseHp, phaseLp, presenceShelf, deepShelf, deepPeak;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizzNotch, speakerLp;
    DcBlock dcBlock;
    // -- real circuit (Koren tube tables) replacing the tanh asymTube --
    rbtube::TubeStage    v1, vClean, vCrunch, vUltraA, vUltraB, vCascade;  // ECC83/12AX7 stages
    rbtube::Miller12AX7  v1Miller, cleanMiller, crunchMiller, ultraAMiller, ultraBMiller, cascadeMiller;
    rbtube::CouplingCapGridLeak cleanCouple;
    rbtube::CouplingCapGridLeak crunchCouple;
    rbtube::CouplingCapGridLeak ultraCoupleAB;
    rbtube::CouplingCapGridLeak ultraCoupleToCascade;
    rbtube::CouplingCapGridLeak coupleToPi;                                // master -> LTP grid
    rbtube::PhaseInverterLTP12AX7 phaseInverter;                           // ECC83 long-tail pair
    rbtube::MultiNodeBPlus supply;                                          // diode rectifier + B+ nodes
    rbtube::PowerAmp6V6  power;                                            // 2x 6V6 push-pull (~15W)
    rbtube::ToneStackYeh tone;                                            // real Marshall JCM2000 TMB
    float inScale = 1.2f, toneMk = 13.0f;
    float lastPowerLoad = 0.0f;
    float lastScreenLoad = 0.0f;
    float lastPreampLoad = 0.0f;

    void setupTubes()
    {
        // The shared input + per-channel cascade, all ECC83/12AX7 (1M grid-leaks ->
        // 250k table, 250V supply, /40 output). fck climbs along the cascade exactly
        // as the real cathode caps shrink (V1A C16 4u7 -> ~19 Hz fully bypassed; V1B
        // C34 1u -> ~88 Hz; the later V2/V3 cold stages R17 4k7 / R34 18k / R33 1k are
        // largely unbypassed -> high fck = tight). Vk0 self-bias solved per stage.
        v1.set(sampleRate, 1, 250.0f, 40.0f, 19.0f, 1800.0f);   // V1A shared input (R15 1k8 || C16 4u7)
        vClean.set(sampleRate, 1, 250.0f, 40.0f, 88.0f, 1800.0f);  // V1B (R40 1k8 || C34 1u)
        vCrunch.set(sampleRate, 1, 250.0f, 40.0f, 60.0f, 1800.0f);
        vUltraA.set(sampleRate, 1, 250.0f, 40.0f, 120.0f, 2200.0f); // V2B-ish (R48 2k2, tight)
        vUltraB.set(sampleRate, 1, 250.0f, 40.0f, 160.0f, 1000.0f); // V3B-ish (R33 1k)
        vCascade.set(sampleRate, 1, 250.0f, 40.0f, 130.0f, 1500.0f);
        v1Miller.set(sampleRate,       68000.0f, 55.0f, 8.0f);
        cleanMiller.set(sampleRate,   180000.0f, 52.0f, 8.0f);
        crunchMiller.set(sampleRate,  180000.0f, 52.0f, 8.0f);
        ultraAMiller.set(sampleRate,  150000.0f, 55.0f, 8.0f);
        ultraBMiller.set(sampleRate,  150000.0f, 55.0f, 8.0f);
        cascadeMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        // Relay-selected coupling/gain-pot networks after V1A. The clean/crunch
        // and Ultra paths keep separate cap charge so switching voices does not
        // flatten the real blocking behavior.
        cleanCouple.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f,
                        0.10f, 0.30f, 0.70f);
        crunchCouple.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f,
                         0.13f, 0.42f, 1.20f);
        // Ultra V2A/V2B/V3B coupling from the schematic (47n/22n/330n families).
        // These are intentionally stronger than the Classic path because that is
        // where the DSL's cold-clipper compression lives.
        ultraCoupleAB.set(sampleRate, 470000.0f, 22.0e-9f, 180000.0f,
                          0.14f, 0.54f, 1.55f);
        ultraCoupleToCascade.set(sampleRate, 470000.0f, 47.0e-9f, 180000.0f,
                                 0.13f, 0.50f, 1.35f);
    }

    void updateFilters()
    {
        chS = smoothstep(channel);
        // Channel/drive morph. Classic side sweeps clean->crunch via Classic Gain;
        // Ultra side sweeps the high-gain OD via Ultra Gain. the game drives
        // `channel`, so a low Gain lands in the clean Classic region and a high Gain
        // in the hot Ultra lead.
        const float classicM = 0.03f + 0.48f * classicGain;   // ~0.03..0.51
        const float ultraM   = 0.54f + 0.42f * ultraGain;     // ~0.54..0.96
        m = clamp01(classicM * (1.0f - chS) + ultraM * chS);
        chVol = classicVol * (1.0f - chS) + ultraVol * chS;

        crunchA = smoothstepRange(0.24f, 0.60f, m);
        ultraA  = smoothstepRange(0.54f, 0.95f, m);
        // DEEP: a real front-panel low-end resonance switch (SW3 + R94/R93 in the NFB).
        deep = clamp01(deepSw);
        // Tone Shift: the DSL mid-scoop switch (deeper on the hotter voice). A hair of
        // low-mid is pulled even part-on for smoothness.
        ts = clamp01(toneShiftSw) * (0.70f + 0.30f * ultraA);

        inputHp.setHighPass(sampleRate, 56.0f + 60.0f * ultraA + 28.0f * (1.0f - bass), 0.70f);
        inputLp.setLowPass(sampleRate, 14600.0f - 3000.0f * ultraA + 1100.0f * treble, 0.64f);
        brightShelf.setHighShelf(sampleRate, 1080.0f + 1150.0f * treble, 0.70f,
                                 -1.8f + 5.0f * treble + 1.6f * presence + 1.0f * crunchA);
        cleanBody.setPeaking(sampleRate, 420.0f + 130.0f * mid, 0.76f,
                             -0.8f + 2.6f * mid + 1.4f * bass);
        crunchBody.setPeaking(sampleRate, 780.0f + 220.0f * mid, 0.82f,
                              -1.6f + 4.8f * mid + 1.9f * crunchA);
        // 6V6 little-DSL: a bit tighter / less low-shelf scoop than the EL34 DSL100.
        ultraTight.setLowShelf(sampleRate, 150.0f + 30.0f * bass, 0.76f,
                               -3.4f * ultraA + 3.2f * bass + 1.2f * deep);
        ultraBite.setPeaking(sampleRate, 1900.0f + 620.0f * treble, 0.82f,
                             0.4f + 3.4f * treble + 2.2f * ultraA + 1.0f * presence);
        interHp.setHighPass(sampleRate, 72.0f + 88.0f * ultraA + 34.0f * (1.0f - bass), 0.71f);
        interLp.setLowPass(sampleRate, 9500.0f + 1200.0f * treble - 1700.0f * ultraA, 0.64f);

        // Marshall JCM2000 (DSL) tone stack -- CIRCUIT-REAL (Yeh, double precision)
        // with the DSL15's real R/C from the schematic: Treble VR5 200k / Bass VR7 1M /
        // Mid VR6 20k, slope R84 33k; treble cap C60 470pF, bass/mid C 22nF/22nF.
        tone.setComponents(200e3, 1.0e6, 20e3, 33e3, 470e-12, 22e-9, 22e-9);
        tone.update(sampleRate, treble, mid, bass);
        // Tone Shift = real front-panel mid-scoop switch (SW2, post-stack voicing).
        toneShiftMid.setPeaking(sampleRate, 830.0f + 180.0f * treble, 1.08f, -8.0f * ts);
        toneShiftBite.setPeaking(sampleRate, 2600.0f + 530.0f * treble, 0.82f,
                                 2.4f * ts + 0.6f * ultraA);
        // 2x 6V6 power amp (~15W) drive (morph + ultra) + sag. Fixed-bias class AB
        // (BIAS1/BIAS2 supply, grounded cathodes) so the bias is -13 V -- the real 6V6
        // operating point and inside the table domain (the EL34's -40 would clamp the
        // 6V6 past cutoff = dead-zero). The 6V6s see a big (already-cascaded) preamp
        // swing, so the drive is FRACTIONAL: a real ~15W power amp is mostly headroom
        // and only adds power compression on top of the preamp dirt at the hot end. A
        // modest floor keeps the clean region loud-but-clean (high crest); it rises with
        // the morph + Ultra so the cranked lead gets extra 6V6 squash. The DSL15's dirt
        // is the preamp cascade. Smaller iron sags a hair sooner; the stiff fixed-bias
        // supply keeps the sag modest (Deep deepens the low-end resonance).
        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f,
                       0.14f, 0.48f, 1.05f);
        phaseInverter.setMarshall(sampleRate, 0.92f + 1.35f * m + 0.70f * ultraA, 0.88f);
        supply.set(sampleRate,
                   35.0f, 47.0f,
                   2200.0f, 47.0f,
                   10000.0f, 22.0f,
                   0.12f + 0.05f * ultraA + 0.04f * deep,
                   0.10f + 0.04f * ultraA + 0.03f * deep,
                   0.05f + 0.03f * ultraA,
                   0.17f);
        power.set(sampleRate, 2.9f + 9.2f * m + 4.7f * ultraA, -13.0f,
                  0.18f + 0.14f * deep, 60.0f, 11200.0f);
        power.out = 0.010f;
        power.biasShift = 3.0f;

        // Power-amp NFB: Presence (high) + Deep (low resonance) + speaker.
        phaseHp.setHighPass(sampleRate, 78.0f + 32.0f * ultraA, 0.72f);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1400.0f * treble + 700.0f * presence - 2000.0f * ultraA, 0.65f);
        presenceShelf.setHighShelf(sampleRate, 2750.0f + 850.0f * presence, 0.78f,
                                   -4.2f + 8.7f * presence + 1.3f * treble);
        // DEEP switch: a fixed low-end resonance bump when engaged.
        deepShelf.setLowShelf(sampleRate, 98.0f, 0.78f,
                              -0.4f + 6.4f * deep + 1.4f * ultraA);
        deepPeak.setPeaking(sampleRate, 116.0f, 0.92f,
                            0.4f + 4.4f * deep + 1.2f * bass);

        // 6V6 + 1x12 / 2x12 voicing (a touch boxier / less scooped than the 4x12). A
        // real cab ROLLS OFF the top (no +dB fizz shelf -- that inflates crest without
        // distorting); gentle HF cut + LP.
        speakerHp.setHighPass(sampleRate, 80.0f + 10.0f * ultraA, 0.72f);
        speakerThump.setPeaking(sampleRate, 128.0f, 0.88f,
                                0.8f + 2.3f * bass + 2.0f * deep);
        speakerLowMid.setPeaking(sampleRate, 430.0f + 155.0f * mid, 0.76f,
                                 0.6f + 2.5f * mid - 2.2f * ts);
        speakerBite.setPeaking(sampleRate, 2900.0f + 620.0f * treble, 0.78f,
                               2.2f + 2.4f * treble + 1.9f * presence + 0.3f * ultraA);
        speakerFizzNotch.setHighShelf(sampleRate, 4700.0f, 0.70f,
                                      -1.6f + 2.0f * treble + 2.0f * presence - 1.4f * ultraA);
        speakerLp.setLowPass(sampleRate, 12800.0f + 1800.0f * treble + 850.0f * presence - 2300.0f * ultraA, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset(); brightShelf.reset();
        cleanBody.reset(); crunchBody.reset(); ultraTight.reset(); ultraBite.reset();
        interHp.reset(); interLp.reset();
        toneShiftMid.reset(); toneShiftBite.reset();
        phaseHp.reset(); phaseLp.reset(); presenceShelf.reset(); deepShelf.reset(); deepPeak.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset();
        speakerFizzNotch.reset(); speakerLp.reset(); dcBlock.reset();
        v1Miller.reset(); cleanMiller.reset(); crunchMiller.reset();
        ultraAMiller.reset(); ultraBMiller.reset(); cascadeMiller.reset();
        v1.reset(); vClean.reset(); vCrunch.reset(); vUltraA.reset(); vUltraB.reset(); vCascade.reset();
        cleanCouple.reset(); crunchCouple.reset();
        ultraCoupleAB.reset(); ultraCoupleToCascade.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset(); tone.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
        setupTubes();
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
            case kChannel:       channel = v; break;
            case kClassicGain:   classicGain = v; break;
            case kClassicVolume: classicVol = v; break;
            case kUltraGain:     ultraGain = v; break;
            case kUltraVolume:   ultraVol = v; break;
            case kPresence:      presence = v; break;
            case kBass:          bass = v; break;
            case kMiddle:        mid = v; break;
            case kTreble:        treble = v; break;
            case kDeep:          deepSw = v; break;
            case kToneShift:     toneShiftSw = v; break;
            case kCabSim:        cabSim = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults()
    {
        for (int i = 0; i < kParamCount; ++i)
            setParam(i, kDsl15Def[i]);
    }

    float process(float in)
    {
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        const float cleanW = 1.0f - smoothstepRange(0.22f, 0.50f, m);
        const float ultraW = smoothstepRange(0.58f, 0.96f, m);
        float crunchW = 1.0f - cleanW - ultraW;
        if (crunchW < 0.0f)
            crunchW = 0.0f;
        const float sum = cleanW + crunchW + ultraW + 1.0e-6f;
        const float cleanMix = cleanW / sum;
        const float crunchMix = crunchW / sum;
        const float ultraMix = ultraW / sum;

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = brightShelf.process(x);
        x = v1.process(v1Miller.process(x) * inScale * bplus.preamp);  // V1A shared input stage (real 12AX7 + Miller)

        // Three voicings off the shared input, blended by the morph (the RS Gain
        // knob sweeps Classic clean -> Crunch -> Ultra). Each is a real 12AX7 cascade.
        float clean = cleanBody.process(x);
        clean = cleanCouple.process(clean, 0.55f + 0.85f * classicGain);
        clean = vClean.process(cleanMiller.process(clean) * (0.7f + 0.8f * m) * bplus.preamp);

        float crunch = crunchBody.process(x);
        crunch = crunchCouple.process(crunch, 0.75f + 3.2f * classicGain + 1.4f * crunchMix);
        crunch = vCrunch.process(crunchMiller.process(crunch) * (1.7f + 5.8f * m) * bplus.preamp);

        float ultra = ultraTight.process(x);
        ultra = ultraBite.process(ultra);
        ultra = vUltraA.process(ultraAMiller.process(ultra) * (2.9f + 8.4f * m) * bplus.preamp);
        ultra = ultraCoupleAB.process(ultra, 0.95f + 5.6f * ultraGain + 1.8f * ultraMix);
        ultra = vUltraB.process(ultraBMiller.process(ultra) * (1.8f + 5.4f * m) * bplus.preamp);

        float y = clean * cleanMix + crunch * crunchMix + ultra * ultraMix;
        y = interHp.process(y);
        y = interLp.process(y);

        // extra cascade stage for the hottest Ultra region
        const float extraCascade = smoothstepRange(0.48f, 0.90f, m);
        const float cascadeIn = ultraCoupleToCascade.process(y, 0.70f + 3.6f * m + 1.2f * ultraMix);
        const float cascaded = vCascade.process(cascadeMiller.process(cascadeIn) *
                                                (1.4f + 4.7f * m + 3.0f * ultraMix) * bplus.preamp);
        y = y * (1.0f - 0.55f * extraCascade) + cascaded * (0.55f * extraCascade);

        // real Marshall JCM2000 tone stack (Yeh) + insertion-loss makeup + Tone Shift
        y = tone.process(y) * toneMk;
        y = toneShiftMid.process(y);
        y = toneShiftBite.process(y);
        y = phaseHp.process(y);
        y = phaseLp.process(y);

        // Channel volume sets how hard the preamp drives the power amp.
        const float chDrive = 0.66f + 0.78f * chVol;
        y *= chDrive;
        y = coupleToPi.process(y, 1.0f + 0.16f * ultraA);
        lastPreampLoad = 0.13f * std::fabs(y) + 0.05f * m;
        y = phaseInverter.process(y * bplus.preamp);
        lastPowerLoad = 0.86f * std::fabs(y) + 0.20f * ultraA;
        lastScreenLoad = 0.54f * std::fabs(y) + 0.11f * m;

        // 2x 6V6 power amp -- REAL pentode table + OT. The smaller DSL15 supply
        // compresses via the B+ scales above. NFB is the Presence/Deep shelves below.
        y = power.process(y * bplus.power * bplus.screen);

        y = presenceShelf.process(y);
        y = deepShelf.process(y);
        y = deepPeak.process(y);
        y = dcBlock.process(y);

        float cab = speakerHp.process(y);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizzNotch.process(cab);
        cab = speakerLp.process(cab);
        y += cabSim * (cab - y);

        // Loudness normalization (keeps multitone RMS ~constant across the gain range
        // so the shared kLvl output stage stays calibrated) + channel volume trim.
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.011f * std::fabs((presence - 0.5f) * 16.0f);
        // NO cleanMakeup: it inverts the crest curve by folding the clean tone into the
        // output soft-clip. The low-gain region simply sits a touch quieter, which is
        // real (the clean DSL voice IS quieter than the cranked lead).
        const float level = (0.74f + 0.12f * (1.0f - m)) /
            ((1.0f + 0.32f * m + 0.64f * ultraMix) * toneEnergy * chDrive);

        // gentle final OT soft-clip (the only tanh in the chain)
        return softClip(y * level) * 0.97f;
    }
};

class Dsl15Plugin : public Plugin
{
    Dsl15Core left;
    Dsl15Core right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;          // 2x anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        for (int i = 0; i < kParamCount; ++i)
        {
            left.setParam(i, params[i]);
            right.setParam(i, params[i]);
        }
    }

public:
    Dsl15Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDsl15Def[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarstenDSL15"; }
    const char* getDescription() const override { return "Marsten DSL15 style amp (15W, 2 channels)"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', 's', '1', '5'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDsl15Names[index];
        parameter.symbol = kDsl15Symbols[index];
        parameter.ranges.min = kDsl15Min[index];
        parameter.ranges.max = kDsl15Max[index];
        parameter.ranges.def = kDsl15Def[index];
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
        left.setParam((int)index, params[index]);
        right.setParam((int)index, params[index]);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate(kOS * (float)newSampleRate);
        right.setSampleRate(kOS * (float)newSampleRate);
        osL.reset();
        osR.reset();
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            float ubL[kOS], ubR[kOS];
            osL.upsample(3.2f * inL[i], ubL);
            osR.upsample(3.2f * inR[i], ubR);
            for (int k = 0; k < kOS; ++k)
            {
                ubL[k] = rbAmpLvl(0.560f * left.process(ubL[k]));
                ubR[k] = rbAmpLvl(0.560f * right.process(ubR[k]));
            }
            outL[i] = osL.downsample(ubL);
            outR[i] = osR.downsample(ubR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dsl15Plugin)
};

Plugin* createPlugin()
{
    return new Dsl15Plugin();
}

END_NAMESPACE_DISTRHO
