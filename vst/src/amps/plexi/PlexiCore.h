#ifndef PLEXI_CORE_H
#define PLEXI_CORE_H
//
// PlexiCore - Marshall 1959 Super Lead 100W "Plexi"/JMP (parody "Marsten Plexi"),
// REBUILT to the advanced circuit-real framework used by BoxDC30Core (the AC30).
// Every real part of the 1959SLP-01 schematic (amps/Marshall Plexi/1959-01-60-02.pdf)
// is modelled with a physical block:
//
//   * each ECC83/12AX7 triode (V1 High-Treble + Normal, V2 recovery+cathode-follower)
//     is a rbtube::TubeStage with the real 100k plate / 820R cathode values: 680n
//     bypass for the bright/recovery stages and 330uF for the normal channel,
//     self-biasing on its own load line (no tanh/asymTube stand-ins);
//   * rbtube::Miller12AX7 gives each stage its true Cgk+Cgp*(1+Av) HF loss from the
//     driving source impedance (68k grid stoppers, A1M Loudness wiper, 470k mix R);
//   * rbtube::CouplingCapGridLeak models the BLOCKING DISTORTION at the V2 grid
//     and PI grid through the real 22n coupling caps and grid-leak paths;
//   * the Marshall FMV tone stack (R11 33k slope, Treble 220k / Bass 1M / Middle 22k,
//     C21 220p / C19 22n / C20 22n) is rbtube::ToneStackYeh, double precision;
//   * rbtube::PhaseInverterLTP12AX7 (setMarshall) is the real V3 12AX7 long-tail pair
//     with the unequal 82k/100k plate loads + 220k/220k grids -- it clips/unbalances
//     BEFORE the EL34s;
//   * rbtube::PowerAmpEL34 is the 4x EL34 fixed-bias push-pull (~100W, ~3.4k OT);
//   * rbtube::MultiNodeBPlus drives per-node B+ sag (power/screen/preamp) per sample
//     from the lastPowerLoad/lastScreenLoad/lastPreampLoad feedback. The 1959SLP-01
//     has a SOLID-STATE bridge rectifier (D2-D5) + 50u+50u reservoir, so the supply
//     is set STIFF (much less sag than the GZ34 AC30);
//   * the pots use real tapers (Loudness I/II A1M audio + wiper source impedance,
//     FMV Treble/Bass/Mid, Presence 22k) in the `plexipot` namespace -- no linear
//     knob->param;
//   * PlexiOutputTransformer is a reactive OT (finite primary L + leakage + a gentle
//     core bend) and PlexiFallbackSpeaker is a Cab Sim controlled fallback 4x12 voice.
//
// Tubes use OUR Koren tables (public model). Existing panel params keep their
// order; Cab Sim is appended as a host-controlled fallback speaker toggle.
//
#include "../../_shared/tube_stage.hpp"
#include "PlexiParams.h"
#include <cmath>

namespace plexi {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// ── Real Plexi front-panel pots + tapers, from the 1959SLP-01 schematic ──
// Loudness I / II are A1M (1M audio). Tone stack: Treble 220k, Bass 1M,
// Middle 22k. Presence is 22k. The Treble cap is C21 220p; slope R11 33k.
namespace plexipot {
static constexpr float kAudioExp   = 1.50f;      // A1M taper calibrated to the plugin's guitar-level range
static constexpr float kLoudness   = 1000000.0f; // Loudness I/II A1M
static constexpr float kTrebleR    = 220000.0f;  // FMV Treble pot (VR3 in schematic)
static constexpr float kBassR      = 1000000.0f; // FMV Bass pot   (R2 in Yeh)
static constexpr float kMidR       = 22000.0f;   // FMV Middle pot (VR4 in schematic)
static constexpr float kSlopeR     = 33000.0f;   // R11 slope resistor
static constexpr float kTrebleCap  = 220.0e-12f; // C21
static constexpr float kStackCapM  = 22.0e-9f;   // C19
static constexpr float kStackCapB  = 22.0e-9f;   // C20
static constexpr float kMix470k    = 470000.0f;  // R9/R10 channel-mix resistors
static constexpr float kBrightCap  = 4700.0e-12f;// dominant High-Treble bright network cap

static inline float audioA(float v) { return std::pow(clamp01(v), kAudioExp); }
static inline float linearB(float v) { return clamp01(v); }

// Source impedance seen looking back into a pot wiper at electrical position e
// (the parallel combination of the upper and lower legs).
static inline float wiperSourceOhms(float potOhms, float electricalPos) {
    const float e = 0.001f + 0.998f * clamp01(electricalPos);
    const float upper = potOhms * (1.0f - e);
    const float lower = potOhms * e;
    return (upper * lower) / std::fmax(1.0f, upper + lower);
}
} // namespace plexipot

// RBJ biquad (peaking / shelves / low-pass) — used only for the fixed passive
// networks the schematic has BETWEEN the tube stages (bright cap, presence NFB
// tap, fallback 4x12). Same struct as BoxDC30Core uses.
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void norm(float a0){ b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0; }
    void peaking(float sr,float f,float Q,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=1+al*A;b1=-2*c;b2=1-al*A; float a0=1+al/A;a1=-2*c;a2=1-al/A; norm(a0); }
    void lowShelf(float sr,float f,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)-(A-1)*c+2*rA*al); b1=2*A*((A-1)-(A+1)*c); b2=A*((A+1)-(A-1)*c-2*rA*al);
        float a0=(A+1)+(A-1)*c+2*rA*al; a1=-2*((A-1)+(A+1)*c); a2=(A+1)+(A-1)*c-2*rA*al; norm(a0); }
    void highShelf(float sr,float f,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)+(A-1)*c+2*rA*al); b1=-2*A*((A-1)+(A+1)*c); b2=A*((A+1)+(A-1)*c-2*rA*al);
        float a0=(A+1)-(A-1)*c+2*rA*al; a1=2*((A-1)-(A+1)*c); a2=(A+1)-(A-1)*c-2*rA*al; norm(a0); }
    void lowpass(float sr,float f,float Q){ f=fminf(f,sr*0.49f); float w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=(1-c)/2;b1=1-c;b2=(1-c)/2; float a0=1+al;a1=-2*c;a2=1-al; norm(a0); }
    void highpass(float sr,float f,float Q){ f=fminf(f,sr*0.49f); float w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=(1+c)/2;b1=-(1+c);b2=(1+c)/2; float a0=1+al;a1=-2*c;a2=1-al; norm(a0); }
};

// Reactive output transformer for the 100W EL34 quad (mirrors en30's
// Ac30OutputTransformer, voiced for the bigger Marshall OT): finite primary
// inductance low rolloff, leakage/iron-loss high rolloff, a low-frequency core
// flux memory and a gentle core bend (the only allowed tanh — a soft OT ceiling,
// not a hard limiter).
struct PlexiOutputTransformer {
    rbtube::HP1 otHp;
    rbtube::LP1 otLeakLp, fluxLp;
    float coreDrive = 1.0f;
    float coreMix = 0.10f;

    void set(float sr, float hot) {
        otHp.set(sr, 32.0f);                  // big OT, primary L lower than the AC30
        otLeakLp.set(sr, 19000.0f);           // leakage/iron loss only (no speaker rolloff)
        fluxLp.set(sr, 22.0f);                // low-frequency core flux memory
        coreDrive = 1.0f + 0.12f * hot;
        coreMix = 0.09f + 0.06f * hot;
    }

    inline float process(float x) {
        float y = otHp.process(x);
        const float flux = fluxLp.process(y);
        const float core = std::tanh((y - 0.16f * flux) * coreDrive);
        y = (1.0f - coreMix) * y + coreMix * core;
        return otLeakLp.process(y);
    }

    void reset() { otHp.reset(); otLeakLp.reset(); fluxLp.reset(); }
};

// Bypassable 4x12 fallback speaker (mirrors en30's Ac30FallbackSpeaker, voiced for
// a closed-back Marshall greenback-style 4x12 instead of an open-back AC30). Not
// fitted to or copied from any captured IR. This is a fallback audition voice, not
// a replacement for the user's external cabinet/IR chain.
struct PlexiFallbackSpeaker {
    rbtube::HP1 hp;
    Biquad coneRes, lowMidScoop, presenceBite, fizzShelf, upperDamp, coneLp;

    void set(float sr, float treble, float pres, float hot) {
        hp.set(sr, 78.0f);                                         // closed-back 4x12 rolloff
        coneRes.peaking(sr, 95.0f, 0.90f, 1.6f + 0.6f * hot);      // greenback cone resonance
        lowMidScoop.peaking(sr, 430.0f, 0.80f, -1.8f);            // 4x12 low-mid dip
        presenceBite.peaking(sr, 2600.0f, 0.80f, 4.6f + 1.8f * treble + 1.6f * pres - 0.6f * hot); // Marshall crunch bite
        fizzShelf.highShelf(sr, 5200.0f, -1.4f + 2.2f * treble + 1.4f * pres - 2.4f * hot);
        upperDamp.peaking(sr, 7200.0f, 0.90f, -1.4f - 0.6f * hot);
        coneLp.lowpass(sr, 10500.0f + 2200.0f * treble + 1200.0f * pres - 800.0f * hot, 0.66f);
    }

    inline float process(float x) {
        float y = hp.process(x);
        y = coneRes.process(y);
        y = lowMidScoop.process(y);
        y = presenceBite.process(y);
        y = fizzShelf.process(y);
        y = upperDamp.process(y);
        return coneLp.process(y);
    }

    void reset() { hp.reset(); coneRes.reset(); lowMidScoop.reset();
        presenceBite.reset(); fizzShelf.reset(); upperDamp.reset(); coneLp.reset(); }
};

// Loudness-flattening quadratic in pLoud1 (RS Gain), calibrated off the offline
// harness so a clean low setting and a cranked roar land near the same reference.
static constexpr float kPlexiGcA =   14.000f;   // moderate makeup: clean remains audible without slamming the limiter
static constexpr float kPlexiGcB =  -19.000f;
static constexpr float kPlexiGcC =    5.000f;

struct PlexiCore {
    float sr = 48000.0f;

    rbtube::HP1 inputCoupling;                       // 68k grid-leak + input cap
    rbtube::TubeStage brightTube, normalTube;        // V1 High-Treble + Normal triodes (ECC83)
    rbtube::TubeStage recoveryTube;                  // V2 recovery / cathode follower (ECC83)
    rbtube::Miller12AX7 brightMiller, normalMiller, recoveryMiller;
    rbtube::CouplingCapGridLeak coupleToRecovery;    // C6 470p + 1M grid leak -> V2 grid
    rbtube::CouplingCapGridLeak coupleToPi;          // C13 100n + 1M grid leak -> V3 (PI) grid
    rbtube::PhaseInverterLTP12AX7 phaseInverter;     // V3 12AX7 long-tail pair (82k/100k)
    rbtube::MultiNodeBPlus supply;                   // SS bridge + reservoir/screen/preamp nodes
    rbtube::PowerAmpEL34 power;                       // 4x EL34 fixed-bias push-pull (~100W)
    rbtube::ToneStackYeh tonestack;                  // Marshall FMV tone stack (Yeh)
    Biquad brightBypass;                            // High-Treble bright cap around Loudness I
    Biquad normalBody;                              // Normal channel darker body
    Biquad presence;                                 // Presence NFB high-shelf (B5k tap)
    PlexiOutputTransformer outputTransformer;
    PlexiFallbackSpeaker fallbackSpeaker;

    // params (0..1) — identical layout to PlexiParams.h
    float pPresence = kPlexiDef[kPresence];
    float pBass     = kPlexiDef[kBass];
    float pMid      = kPlexiDef[kMiddle];
    float pTreble   = kPlexiDef[kTreble];
    float pLoud1    = kPlexiDef[kLoudness1];
    float pLoud2    = kPlexiDef[kLoudness2];
    float pInput    = kPlexiDef[kInput];
    float pCabSim   = kPlexiDef[kCabSim];

    // derived
    float brightG = 1.0f, normalG = 0.0f;
    float effDrive = 0.5f;
    float inScale = 1.0f, brightDrive = 1.0f, normalDrive = 1.0f, recoveryDrive = 1.0f;
    float piDrive = 1.0f, powerDrive = 1.0f, outLevel = 1.0f;
    float lastPowerLoad = 0, lastScreenLoad = 0, lastPreampLoad = 0;

    void setSampleRate(float s){ sr = s > 1000.0f ? s : 48000.0f; recalc(); reset(); }

    void reset(){
        inputCoupling.reset();
        brightTube.reset(); normalTube.reset(); recoveryTube.reset();
        brightMiller.reset(); normalMiller.reset(); recoveryMiller.reset();
        coupleToRecovery.reset(); coupleToPi.reset();
        phaseInverter.reset(); supply.reset(); power.reset();
        tonestack.reset(); brightBypass.reset(); normalBody.reset(); presence.reset();
        outputTransformer.reset(); fallbackSpeaker.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0;
    }

    void recalc(){
        // Input cable select (matches the old core's panel behaviour):
        //   Bright (<0.75) / Both-jumpered (0.25..0.75) / Normal (>=0.25).
        brightG = (pInput < 0.75f) ? 1.0f : 0.0f;
        normalG = (pInput >= 0.25f) ? 1.0f : 0.0f;

        const float vol1 = plexipot::audioA(pLoud1);   // Loudness I (A1M) electrical wiper
        const float vol2 = plexipot::audioA(pLoud2);   // Loudness II (A1M) electrical wiper
        // The Loudness pots ARE the gain on a non-master amp; jumpering both drives
        // the amp harder. The High-Treble channel is the primary voice.
        effDrive = clamp01(brightG * vol1 + normalG * vol2 * 0.80f);
        // A gentler LINEAR-ish drive proxy off the raw knobs (not the steep ^2.4 taper)
        // for the level makeup and the V2 drive: this keeps low Loudness settings
        // audible (the steep taper would otherwise crush them below tube conduction)
        // and matches the way a real cranked 1959 already grinds at noon. Same role as
        // en30's `driveVol` calibration term.
        const float driveLin = clamp01(brightG * std::sqrt(plexipot::linearB(pLoud1))
                                     + normalG * 0.80f * std::sqrt(plexipot::linearB(pLoud2)));

        const float treble = plexipot::linearB(pTreble); // 220k linear
        const float bass   = plexipot::linearB(pBass);   // 1M
        const float mid    = plexipot::linearB(pMid);    // 22k
        const float pres   = plexipot::linearB(pPresence);

        inputCoupling.set(sr, 12.0f);                    // 68k grid-leak + input cap

        // ── V1 + V2 12AX7 triodes (ECC83, real values) ──
        // 100k plate loads, 820R cathodes. The NORMAL channel has C1 330uF, so it is
        // effectively fully bypassed and fuller; the HIGH TREBLE and V2 recovery
        // stages use 680nF for the familiar tight Marshall mid/treble emphasis.
        brightTube.setWithPlate(sr, 1, 250.0f, 42.0f, 285.0f, 820.0f, 100000.0f);   // V1B High-Treble, R2/C2
        normalTube.setWithPlate(sr, 1, 250.0f, 42.0f,   0.6f, 820.0f, 100000.0f);   // V1A Normal, R1/C1 330uF
        recoveryTube.setWithPlate(sr, 1, 250.0f, 42.0f, 285.0f, 820.0f, 100000.0f); // V2A recovery, R9/C6

        // Drive topology of a NON-master 1959: the guitar feeds V1 at a FIXED level
        // (just the 68k grid stoppers + input cap); the Loudness pots are voltage
        // dividers on V1's OUTPUT (applied once, in process(), as vol1/vol2). What the
        // Loudness pots therefore control is how hard V2 (recovery) and the PI/EL34s
        // are driven — that is where the grind lives. So inScale into V1 is constant,
        // and the post-Loudness gain into V2 rises with the wiper.
        inScale       = 2.55f;                         // guitar -> V1 grid volts
        brightDrive   = 1.0f;                          // V1 grids see the raw input
        normalDrive   = 1.0f;
        // NON-MASTER topology: in a real 1959 the Loudness pot is the ONLY drive control
        // and it is an ATTENUATOR between V1 and V2 (applied as vol1/vol2 in process()).
        // V2/PI/EL34 are FIXED-gain circuit stages — the saturation comes from how much
        // signal the Loudness pot lets through, NOT from changing these drives. The first
        // port tied them to the knob too (driveLin), double-counting the pot: at low
        // Loudness the attenuator killed the signal AND the drive dropped → dead silence;
        // at high Loudness both compounded → the coupling cap gated. Fixed gains fix both.
        recoveryDrive = 13.50f;                        // V1->V2 coupling/stage gain (fixed)
        piDrive       = 1.55f;                         // V2/CF -> PI grid gain (fixed)
        powerDrive    = 20.0f + 12.0f * driveLin;      // PI -> 4x EL34 fixed-bias gain
        // PI/power feedback weights & a small headroom comp by the dominant Loudness.
        const float dom = std::fmax(brightG * driveLin, normalG * driveLin);

        // ── 12AX7 Miller loading (real Cgk+Cgp*(1+Av) from the source impedance) ──
        // V1 grids: 68k input grid stoppers (R3..R6). V2 grid: A1M Loudness wiper in
        // parallel with the 470k mix resistors. Recovery -> PI: V2 plate / 100n.
        const float loud1Src = plexipot::wiperSourceOhms(plexipot::kLoudness, vol1);
        const float loud2Src = plexipot::wiperSourceOhms(plexipot::kLoudness, vol2);
        brightMiller.set(sr, 68000.0f, 55.0f, 10.0f);
        normalMiller.set(sr, 68000.0f, 55.0f, 10.0f);
        const float recSrc = 0.5f * (loud1Src + loud2Src) + plexipot::kMix470k;
        recoveryMiller.set(sr, std::fmin(220000.0f, recSrc), 52.0f, 6.0f);

        // ── Coupling caps + grid-leak charging (blocking distortion) ──
        // The real 1959 inter-stage coupling caps are 0.022uF (C6 -> V2 grid, C13 -> PI
        // grid), each with a 1M grid leak — identical role to the AC30's coupleToV2.
        // ⚠ The first port wrote 470pF here, which is the BRIGHT/treble cap, not the
        // coupling cap: 470p+1M sets a 339 Hz HPF (thins everything) AND a fast-charge/
        // slow-release blocking cap (40us charge, 10ms leak) that, driven hard, parks
        // the V2 grid in cutoff after every transient → the audible "entrecortado".
        coupleToRecovery.set(sr, 1000000.0f, 22.0e-9f, 220000.0f, 0.18f, 0.34f, 0.85f);
        coupleToPi.set(sr, 1000000.0f, 22.0e-9f, 100000.0f, 0.20f, 0.30f, 0.75f);

        // ── High-Treble bright network across Loudness I ──
        // This belongs around the post-V1 volume pot, not in front of V1. The bypass
        // path vanishes at 0 and 10 like the real pot/cap geometry, and is strongest
        // in the low-mid part of the sweep where Plexis get that sharp bright-channel
        // edge.
        const float brightHz = 1.0f / (2.0f * kPi * std::fmax(33000.0f, loud1Src) * plexipot::kBrightCap);
        brightBypass.highpass(sr, std::fmax(700.0f, std::fmin(5200.0f, brightHz)), 0.70f);
        normalBody.lowpass(sr, 5200.0f + 1600.0f * treble, 0.68f);

        // ── Marshall FMV tone stack (Yeh, double precision) ──
        // R1 Treble 220k, R2 Bass 1M, R3 Mid 22k, R4 slope 33k; C1 220p, C2/C3 22n.
        tonestack.setComponents(plexipot::kTrebleR, plexipot::kBassR, plexipot::kMidR,
                                plexipot::kSlopeR, plexipot::kTrebleCap,
                                plexipot::kStackCapM, plexipot::kStackCapB);
        tonestack.update(sr, treble, mid, bass);

        // ── SS bridge supply + LTP PI + 4x EL34 fixed-bias push-pull ──
        // 1959SLP-01 uses a solid-state bridge (D2-D5) with 50u+50u reservoir -> a
        // STIFF supply (low series R, small depth). Screen drop = R26/R27 10k; the
        // preamp is fed through a 10k/100k chain (R14/R15 etc.).
        supply.set(sr,
                   40.0f, 100.0f,          // SS bridge + 50u+50u reservoir (stiff)
                   10000.0f, 50.0f,        // screen node (R26/R27 10k + 50u)
                   33000.0f, 50.0f,        // preamp node (dropping R + 50u)
                   0.08f + 0.04f * effDrive,   // power-node depth (small — SS)
                   0.06f + 0.03f * effDrive,   // screen depth
                   0.04f + 0.02f * effDrive,   // preamp depth
                   0.16f);                     // release
        // Real Marshall 12AX7 LTP: 82k/100k plates, ~10k tail (220k/220k grids), small
        // imbalance. It clips/unbalances before the EL34s.
        phaseInverter.setMarshall(sr, 1.15f + 2.25f * dom + 0.45f * effDrive, 0.88f);
        // 4x EL34, FIXED bias. A real 1959 idles the EL34s around -34..-38V (≈70% Pdiss,
        // ~35mA), NOT -50V: at -50V on our Koren plate table the idle is too cold and the
        // small-signal transconductance is ~2.5x lower, so the power amp barely conducts
        // at low Loudness (dead/quiet) and adds crossover rasp on soft notes. -36V puts
        // it in proper class-AB1. EL34 still break up earlier + compress harder than 6L6
        // -> the aggressive Marshall grind. Sag is modest (solid-state rectifier).
        power.set(sr, powerDrive, -36.0f, 0.18f, 42.0f, 13500.0f);
        power.out = 0.0085f;

        // ── Presence: tap on the power-amp NFB (B5k + C12 100n). More presence =
        // less HF feedback = an HF lift after the power amp. ──
        presence.highShelf(sr, 2400.0f + 900.0f * pres, -3.5f + 9.0f * pres + 1.0f * treble);

        // Output transformer (always part of the amp) + bypassable 4x12 fallback voice.
        outputTransformer.set(sr, driveLin);
        fallbackSpeaker.set(sr, treble, pres, driveLin);

        // Plugin-level loudness flattening (NOT part of the circuit): the Loudness-as-
        // gain means low settings are quieter than cranked ones. Mirror the AC30 — a
        // moderate constant outLevel here + a knob-position gcDb polynomial in process()
        // (see below). NO exp-based cleanMakeup: with real tubes that inverts the crest
        // curve (it lifts the cleanest settings hardest, exactly backwards).
        outLevel = 2.10f;    // global make-up: leaves the plugin output knee for real peaks
    }

    void setParam(int idx, float v){
        v = clamp01(v);
        switch (idx){
            case kPresence:  pPresence = v; break;
            case kBass:      pBass = v; break;
            case kMiddle:    pMid = v; break;
            case kTreble:    pTreble = v; break;
            case kLoudness1: pLoud1 = v; break;
            case kLoudness2: pLoud2 = v; break;
            case kInput:     pInput = v; break;
            case kCabSim:    pCabSim = v; break;
            default: break;
        }
        recalc();
    }

    void initDefaults(){
        for (int i = 0; i < kParamCount; ++i) setParam(i, kPlexiDef[i]);
    }

    inline float process(float x){
        const rbtube::SupplyScales bplus = supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);

        x = inputCoupling.process(x * inScale);

        // HIGH TREBLE (bright) channel: real V1 triode first; the bright cap sits
        // around the Loudness I pot after this stage.
        float bch = brightTube.process(brightMiller.process(x) * brightDrive * bplus.preamp);

        // NORMAL channel: darker, real V1 triode (fixed drive).
        float nch = normalTube.process(normalMiller.process(x) * normalDrive * bplus.preamp);

        // The Loudness pots (A1M audio) are dividers on each V1 output, applied ONCE
        // here (vol1/vol2), then summed at the jumpered mix node (470k mix Rs set the
        // 0.92 relative weight of the Normal channel). This sum is the gain into V2.
        const float vol1 = plexipot::audioA(pLoud1);
        const float vol2 = plexipot::audioA(pLoud2);
        const float brightCapMix = (1.0f - vol1) * std::sqrt(std::fmax(0.0f, vol1)) *
                                   (0.23f + 0.22f * plexipot::linearB(pTreble));
        const float brightOut = vol1 * bch + brightCapMix * brightBypass.process(bch);
        nch = normalBody.process(nch);
        float y = brightG * brightOut + normalG * vol2 * 0.92f * nch;

        // V2 recovery + cathode follower into the tone stack (the grind stage). The
        // coupling cap C6 (+ 1M grid leak) gives the recovery grid its blocking
        // distortion when the channels drive it hard.
        y = recoveryTube.process(coupleToRecovery.process(recoveryMiller.process(y),
                                                           recoveryDrive * bplus.preamp));

        // Marshall FMV tone stack (between the recovery/CF and the PI).
        y = tonestack.process(y);

        // V3 12AX7 long-tail-pair PI (C13 100n coupling + 1M grid leak feeds its grid).
        y = coupleToPi.process(y * piDrive, 1.0f);
        lastPreampLoad = std::fabs(y) * (0.20f + 0.40f * effDrive);
        y = phaseInverter.process(y * bplus.screen);
        lastScreenLoad = std::fabs(y) * (0.35f + 0.60f * effDrive);

        // 4x EL34 push-pull (~100W) — real pentode table + sag + OT.
        y = power.process(y * bplus.power * bplus.screen);
        lastPowerLoad = std::fabs(y) * (0.55f + 0.95f * effDrive);

        // Presence taps the power-amp NFB (post power amp HF lift).
        y = presence.process(y);

        // Output transformer (reactive) + bypassable 4x12 fallback speaker.
        y = outputTransformer.process(y);
        const float cab = fallbackSpeaker.process(y);
        y += pCabSim * (cab - y);

        // Loudness flattening by PANEL POSITION (Loudness I is the RS Gain knob), AC30-
        // style: a calibrated quadratic in pLoud1 so a clean low setting and a cranked
        // roar both land near the shared ~-19 dBFS reference. Calibrated off the harness.
        float gcDb = kPlexiGcA + kPlexiGcB * pLoud1 + kPlexiGcC * pLoud1 * pLoud1;
        if (gcDb > 14.0f) gcDb = 14.0f; else if (gcDb < -4.0f) gcDb = -4.0f;
        return y * outLevel * std::pow(10.0f, 0.05f * gcDb);
    }
};

} // namespace plexi
#endif // PLEXI_CORE_H
