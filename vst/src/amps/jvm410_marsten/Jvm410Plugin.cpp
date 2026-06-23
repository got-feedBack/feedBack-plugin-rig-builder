/*
 * MARSTEN JVM410 - Marshall JVM410H for the game's Amp_MarshallJVM410H. Parody
 * brand "Marsten" (Marshall -> Marsten; same family as the Marsten DSL100 /
 * JCM800 / Bluesbreaker copies). The in-app face must never read "Marshall".
 *
 * ===========================================================================
 * CIRCUIT-REAL DSP (schematic-first, component-by-component). Reference:
 *   amps/Marshall JVM410/Marshall_jvm410_sch.pdf
 *     SHT1 PRE AMP  (JVM410-60-02 sht1) : input + the long ECC83 cascade
 *                    (V6/V7/V8/V9 = 4x ECC83, relay-tapped per channel/mode),
 *                    FX loop, op-amp reverb (IC4), NFB takeoff.
 *     SHT2 POWER AMP (JVM410-60-02 sht2): V5 ECC83 long-tail phase inverter,
 *                    BIAS_1/2 fixed bias, 4x EL34 (V1..V4) push-pull ~100W,
 *                    screen grids R33/36/39/42 (5K6), grid stoppers 1K8.
 *     FRONT PANEL 1 (JVM410-61-02 sht1): the four channel strips. TWO tone
 *                    stacks (read off the board):
 *        CLEAN  : Fender/Bassman-type — Treble VR209 B200K / 220pF (C209),
 *                 Bass VR207 A200K / 100nF (C207), Mid VR208 B20K / 47nF
 *                 (C208), slope R207 56K.  Clean Gain VR210 A1M (+220pF C219).
 *        CRUNCH : Marshall TMB — Treble VR219 B200K / 470pF (C217), Bass
 *                 VR217 A1M, Mid VR218 B20K / 22nF (C227) / 22nF (C228),
 *                 slope R239 33K. Crunch Gain VR220 A1M (470K/470pF bright).
 *        OD1    : same Marshall TMB (VR204/202/203) ; OD1 Gain VR205 A1M.
 *        OD2    : same Marshall TMB (VR214/212/213) ; OD2 Gain VR215 A1M.
 *     FRONT PANEL 2 (JVM410-61-02 sht2): green/orange/red relay logic per
 *                    channel, PRESENCE (VR326) + RESONANCE (VR305) NFB, MASTER.
 *   amps/Marshall JVM410/2.jpg — the front panel (for the canvas).
 *
 * Signal chain (mono core @ 2x oversampling):
 *   in -> input HP/bright -> V6a (shared 1st stage, real 12AX7)
 *      -> [CLEAN tone stack | MARSHALL TMB] depending on channel
 *      -> per-channel ECC83 cascade (1 stage clean .. 4 stages OD2), the
 *         channel + mode being a GAIN-MORPH that drives more signal into more
 *         stages (more cascade clip) — like the Marsten DSL100.
 *      -> Master -> 4x EL34 push-pull (real pentode table + sag/OT)
 *      -> Presence/Resonance NFB shelves -> 4x12 voicing -> reverb -> out.
 *
 * The real amp stores all four channels; the game plays one sound at a time,
 * so this models the SELECTED channel + mode (the documented simplification).
 * kChannel = Clean(0..0.25)/Crunch(.25..0.5)/OD1(.5..0.75)/OD2(.75..1) with
 * increasing cascade gain; kMode (green 0 / orange 0.5 / red 1) adds preamp
 * gain+saturation within the channel.
 *
 * the game: RS Gain -> GAIN (Channel pinned to OD1 + Mode orange via the song
 * mapping); Bass/Mid/Treble -> tone stack; Pres -> Presence. See
 * rs_knob_to_vst_param.json.
 */
#include "DistrhoPlugin.hpp"
#include "Jvm410Params.h"
#include "../../_shared/tube_stage.hpp"   // real ECC83/12AX7 stages + EL34 PP + Yeh tone stack
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts
// never hard-clip.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

namespace {

static constexpr float kPi = 3.14159265359f;
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float clampFreq(float hz, float sr) { return std::fmax(20.0f, std::fmin(hz, sr * 0.45f)); }
static inline float smoothstep(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }
static inline float smoothstepRange(float e0, float e1, float x) { return smoothstep((x - e0) / (e1 - e0)); }
static inline float softClip(float x) { return std::tanh(x); }

class Biquad
{
    float b0=1.0f,b1=0.0f,b2=0.0f,a1=0.0f,a2=0.0f,z1=0.0f,z2=0.0f;
    void set(float nb0,float nb1,float nb2,float na0,float na1,float na2)
    { if(std::fabs(na0)<1.0e-12f) na0=1.0f; const float i=1.0f/na0;
      b0=nb0*i; b1=nb1*i; b2=nb2*i; a1=na1*i; a2=na2*i; }
public:
    void reset(){ z1=z2=0.0f; }
    float process(float x){ const float y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; }
    void setHighPass(float sr,float hz,float q){ hz=clampFreq(hz,sr); const float w=2.0f*kPi*hz/sr,c=std::cos(w),al=std::sin(w)/(2.0f*q);
        set((1.0f+c)*0.5f,-(1.0f+c),(1.0f+c)*0.5f,1.0f+al,-2.0f*c,1.0f-al); }
    void setLowPass(float sr,float hz,float q){ hz=clampFreq(hz,sr); const float w=2.0f*kPi*hz/sr,c=std::cos(w),al=std::sin(w)/(2.0f*q);
        set((1.0f-c)*0.5f,1.0f-c,(1.0f-c)*0.5f,1.0f+al,-2.0f*c,1.0f-al); }
    void setPeaking(float sr,float hz,float q,float dB){ hz=clampFreq(hz,sr); const float a=std::pow(10.0f,dB/40.0f),w=2.0f*kPi*hz/sr,c=std::cos(w),al=std::sin(w)/(2.0f*q);
        set(1.0f+al*a,-2.0f*c,1.0f-al*a,1.0f+al/a,-2.0f*c,1.0f-al/a); }
    void setHighShelf(float sr,float hz,float sl,float dB){ hz=clampFreq(hz,sr); const float a=std::pow(10.0f,dB/40.0f),w=2.0f*kPi*hz/sr,c=std::cos(w),sn=std::sin(w),ra=std::sqrt(a),al=sn*0.5f*std::sqrt((a+1.0f/a)*(1.0f/sl-1.0f)+2.0f);
        set(a*((a+1.0f)+(a-1.0f)*c+2.0f*ra*al),-2.0f*a*((a-1.0f)+(a+1.0f)*c),a*((a+1.0f)+(a-1.0f)*c-2.0f*ra*al),
            (a+1.0f)-(a-1.0f)*c+2.0f*ra*al,2.0f*((a-1.0f)-(a+1.0f)*c),(a+1.0f)-(a-1.0f)*c-2.0f*ra*al); }
    void setLowShelf(float sr,float hz,float sl,float dB){ hz=clampFreq(hz,sr); const float a=std::pow(10.0f,dB/40.0f),w=2.0f*kPi*hz/sr,c=std::cos(w),sn=std::sin(w),ra=std::sqrt(a),al=sn*0.5f*std::sqrt((a+1.0f/a)*(1.0f/sl-1.0f)+2.0f);
        set(a*((a+1.0f)-(a-1.0f)*c+2.0f*ra*al),2.0f*a*((a-1.0f)-(a+1.0f)*c),a*((a+1.0f)-(a-1.0f)*c-2.0f*ra*al),
            (a+1.0f)+(a-1.0f)*c+2.0f*ra*al,-2.0f*((a-1.0f)+(a+1.0f)*c),(a+1.0f)+(a-1.0f)*c-2.0f*ra*al); }
};

class DcBlock
{
    float x1=0.0f,y1=0.0f;
public:
    void reset(){ x1=y1=0.0f; }
    float process(float x){ const float y=x-x1+0.995f*y1; x1=x; y1=y; return y; }
};

// --- compact digital reverb (3 allpass diffusers + 2 damped combs). The JVM's
//     reverb is an op-amp-driven digital tank (CD4052 + RC4558 on JVM410-60/61);
//     off when REVERB = 0 (RS pins it to 0). Same topology as the Marsten DSL100. ---
class DigiReverb
{
    float ap0[1024], ap1[1024], ap2[1024];
    float cb0[3600], cb1[3600];
    int p0 = 0, p1 = 0, p2 = 0, c0 = 0, c1 = 0;
    int n0 = 281, n1 = 401, n2 = 487, nc0 = 1801, nc1 = 2143;
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
        n0 = (int)(281 * s); n1 = (int)(401 * s); n2 = (int)(487 * s);
        nc0 = (int)(1801 * s); nc1 = (int)(2143 * s);
        if (nc0 > 3599) nc0 = 3599; if (nc1 > 3599) nc1 = 3599;
        inHp.setHighPass(sr, 180.0f, 0.7f);
        inLp.setLowPass(sr, 5200.0f, 0.7f);
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
        const float o0 = cb0[c0]; damp0 += 0.38f * (o0 - damp0); cb0[c0] = x + damp0 * 0.74f; if (++c0 >= nc0) c0 = 0;
        const float o1 = cb1[c1]; damp1 += 0.38f * (o1 - damp1); cb1[c1] = x + damp1 * 0.72f; if (++c1 >= nc1) c1 = 0;
        return (o0 + o1) * 0.5f;
    }
};

} // namespace

class Jvm410Core
{
    float sampleRate = 48000.0f;
    float channel   = kJvm410Def[kChannel];
    float mode      = kJvm410Def[kMode];
    float gain      = kJvm410Def[kGain];
    float volume    = kJvm410Def[kVolume];
    float bass      = kJvm410Def[kBass];
    float mid       = kJvm410Def[kMiddle];
    float treble    = kJvm410Def[kTreble];
    float presence  = kJvm410Def[kPresence];
    float resonance = kJvm410Def[kResonance];
    float master    = kJvm410Def[kMaster];
    float reverb    = kJvm410Def[kReverb];
    float cabSim    = kJvm410Def[kCabSim];

    // derived (recomputed in updateFilters)
    float m = 0.6f;       // total preamp drive morph 0..1 (channel cascade + mode + gain)
    float gDrive = 0.5f;  // GAIN pot cascade drive 0..1 (wide on dirty channels)
    float chS = 0.66f;    // smoothed channel position (0 Clean .. 1 OD2)
    float modeA = 0.5f;   // smoothed mode (0 green .. 1 red)
    float cleanW=0, crunchW=0, od1W=0, od2W=0;  // channel mix weights
    float dirtW = 0.6f;   // 1 - cleanW: how "dirty"/cascaded the voice is
    float deep = 0.5f;    // resonance/deep amount

    Biquad inputHp, pickupLoad, brightCap;
    Biquad cleanBody, crunchBody, od1Tight, od1Bite, od2Tight, od2Bite;
    Biquad interHp, interLp, cathodeLp;
    // Two real tone stacks (Yeh, double-precision — 3rd-order TMB MUST be double or
    // it NaNs at 192k): the Clean channel's Bassman-type stack and the shared
    // Marshall TMB for Crunch/OD1/OD2 (different active nodes -> different feel).
    rbtube::ToneStackYeh toneClean;             // Treble 200K/220pF · Bass 200K/100nF · Mid 20K/47nF · slope 56K
    rbtube::ToneStackYeh toneCrunch, toneOd1, toneOd2;  // shared Marshall TMB (one instance per
                                                // voice so the stateful IIRs don't cross-talk;
                                                // only one channel is audible at a time)
    Biquad stackMakeupLow, phaseHp, phaseLp, presenceShelf, resonanceShelf, resonancePeak;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    DigiReverb rev;

    // ── REAL tube stages (Koren ECC83/12AX7 circuit models) ──
    //   V6a  = shared input/recovery stage (Rk=2.7K, Ck=680nF -> fck~87Hz, off the board).
    //   vClean = clean recovery; the crunch/OD cascades keep separate state per
    //   voice because the DSP evaluates crossfaded voices in parallel.
    //   fck climbs along the cascade (tighter low end deeper in the chain).
    rbtube::TubeStage v6a, vClean, vCrunch1, vOd1a, vOd1b, vOd2a, vOd2b, vOd2c;
    rbtube::Miller12AX7 v6aMiller, cleanMiller, crunchMiller, od1aMiller, od1bMiller, od2aMiller, od2bMiller, od2cMiller;
    rbtube::CouplingCapGridLeak cleanCouple;
    rbtube::CouplingCapGridLeak crunchCouple;
    rbtube::CouplingCapGridLeak od1CoupleAB;
    rbtube::CouplingCapGridLeak od2CoupleAB;
    rbtube::CouplingCapGridLeak od2CoupleBC;
    rbtube::CouplingCapGridLeak coupleToPi;      // channel volume/master -> LTP grid
    rbtube::PhaseInverterLTP12AX7 phaseInverter; // ECC83 long-tail pair
    rbtube::MultiNodeBPlus supply;               // diode rectifier + B+ nodes
    rbtube::PowerAmpEL34 power;        // 4x EL34 push-pull (~100W)
    float lastPowerLoad = 0.0f;
    float lastScreenLoad = 0.0f;
    float lastPreampLoad = 0.0f;

    void setupTubes()
    {
        // Shared 1st stage V6a: 220K plate, 2.7K cathode, 680nF bypass (fck~87Hz),
        // 250V supply, /40 output. Self-bias solved by the stage.
        v6a.set(sampleRate, 1, 250.0f, 40.0f, 87.0f, 2700.0f);
        // Clean recovery (one extra gentle stage so the clean channel has body but
        // stays clean): standard 12AX7, low fck.
        vClean.set(sampleRate, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
        // Crunch/OD cascades (V7b -> V7a -> V8b -> V9a), state-separated per voice.
        vCrunch1.set(sampleRate, 1, 250.0f, 40.0f, 35.0f, 1500.0f);
        vOd1a.set(sampleRate,    1, 250.0f, 40.0f, 35.0f, 1500.0f);
        vOd1b.set(sampleRate,    1, 250.0f, 40.0f, 45.0f, 1500.0f);
        vOd2a.set(sampleRate,    1, 250.0f, 40.0f, 35.0f, 1500.0f);
        vOd2b.set(sampleRate,    1, 250.0f, 40.0f, 55.0f, 1500.0f);
        vOd2c.set(sampleRate,    1, 250.0f, 40.0f, 70.0f, 1500.0f);
        v6aMiller.set(sampleRate,      68000.0f, 55.0f, 8.0f);
        cleanMiller.set(sampleRate,   180000.0f, 52.0f, 8.0f);
        crunchMiller.set(sampleRate,  180000.0f, 52.0f, 8.0f);
        od1aMiller.set(sampleRate,    180000.0f, 55.0f, 8.0f);
        od1bMiller.set(sampleRate,    150000.0f, 55.0f, 8.0f);
        od2aMiller.set(sampleRate,    180000.0f, 55.0f, 8.0f);
        od2bMiller.set(sampleRate,    150000.0f, 55.0f, 8.0f);
        od2cMiller.set(sampleRate,    150000.0f, 55.0f, 8.0f);
        // JVM relay/gain-pot coupling between cascade nodes. The real amp does not
        // teleport V7/V8/V9 plates into the next grid: each channel/mode carries
        // coupling-cap charge and blocking that changes the feel at high gain.
        cleanCouple.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f,
                        0.10f, 0.28f, 0.65f);
        crunchCouple.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f,
                         0.13f, 0.42f, 1.20f);
        od1CoupleAB.set(sampleRate, 470000.0f, 22.0e-9f, 180000.0f,
                        0.14f, 0.54f, 1.55f);
        od2CoupleAB.set(sampleRate, 470000.0f, 22.0e-9f, 180000.0f,
                        0.15f, 0.58f, 1.70f);
        od2CoupleBC.set(sampleRate, 470000.0f, 22.0e-9f, 150000.0f,
                        0.14f, 0.55f, 1.60f);
    }

    void updateFilters()
    {
        chS = clamp01(channel);
        modeA = clamp01(mode);

        // Channel mix weights (four overlapping crossfades across kChannel).
        cleanW  = 1.0f - smoothstepRange(0.18f, 0.34f, chS);
        const float crunchUp = smoothstepRange(0.18f, 0.34f, chS);
        const float crunchDn = smoothstepRange(0.42f, 0.60f, chS);
        crunchW = crunchUp * (1.0f - crunchDn);
        const float od1Up = smoothstepRange(0.42f, 0.60f, chS);
        const float od1Dn = smoothstepRange(0.68f, 0.86f, chS);
        od1W = od1Up * (1.0f - od1Dn);
        od2W = smoothstepRange(0.68f, 0.86f, chS);
        const float sum = cleanW + crunchW + od1W + od2W + 1.0e-6f;
        cleanW/=sum; crunchW/=sum; od1W/=sum; od2W/=sum;
        dirtW = 1.0f - cleanW;

        // Overall preamp drive morph (drives the EQ voicing): Clean lowest .. OD2
        // highest, + mode + gain. mode green/orange/red and the GAIN pot push more
        // signal into the cascade.
        const float chBase = 0.06f * cleanW + 0.40f * crunchW + 0.66f * od1W + 0.90f * od2W;
        const float modeGain = 0.16f * modeA;
        m = clamp01(chBase + modeGain * (0.4f + 0.6f * dirtW) + 0.28f * gain * (0.4f + 0.6f * dirtW));

        // gDrive = the GAIN pot's actual cascade drive amount (0..1). On the dirty
        // channels the JVM Gain pot has a WIDE range (cleans up low, slams at 10);
        // on Clean it barely moves. This (not the gentle `m`) is what makes RS Gain
        // sweep the distortion, so the crest curve is monotonic (dirtier with gain).
        gDrive = clamp01((0.10f + 0.90f * smoothstep(gain)) * (0.30f + 0.70f * dirtW)
                         + 0.30f * modeA * dirtW);

        const float g = smoothstep(m);
        const float pushed = smoothstepRange(0.40f, 0.92f, m);
        deep = smoothstep(resonance);

        setupTubes();

        inputHp.setHighPass(sampleRate, 30.0f + 22.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12800.0f - 1600.0f * pushed + 700.0f * treble, 0.64f);
        // bright cap across the gain pot — more shimmer at low gain (clean/green)
        brightCap.setHighShelf(sampleRate, 1900.0f, 0.72f, (2.6f + 1.4f * cleanW) * (1.0f - g));

        // per-channel voicing bodies (selected by the mix weights)
        cleanBody.setPeaking(sampleRate, 430.0f + 130.0f * mid, 0.74f, -0.6f + 2.4f * mid + 1.2f * bass);
        crunchBody.setPeaking(sampleRate, 720.0f + 220.0f * mid, 0.80f, -1.4f + 4.4f * mid + 1.6f * crunchW);
        od1Tight.setHighPass(sampleRate, 80.0f + 70.0f * od1W + 30.0f * (1.0f - bass), 0.71f);
        od1Bite.setPeaking(sampleRate, 1750.0f + 560.0f * treble, 0.82f, 0.4f + 3.0f * treble + 1.8f * od1W);
        od2Tight.setLowShelf(sampleRate, 150.0f + 30.0f * bass, 0.76f, -3.6f * od2W + 3.0f * bass + 1.0f * deep);
        od2Bite.setPeaking(sampleRate, 2050.0f + 620.0f * treble, 0.84f, 0.6f + 3.4f * treble + 2.2f * od2W + 1.0f * modeA);

        interHp.setHighPass(sampleRate, 110.0f + 150.0f * pushed + 50.0f * dirtW, 0.70f);
        interLp.setLowPass(sampleRate, 9800.0f + 1200.0f * treble - 1800.0f * pushed, 0.64f);
        cathodeLp.setLowPass(sampleRate, 9400.0f + 1300.0f * treble - 1500.0f * pushed, 0.64f);

        // ── CIRCUIT-REAL tone stacks (Yeh, real R/C from FRONT PANEL 1) ──
        // Clean: Bassman/Fender-type (own stack on the schematic). Map Bass->l,
        // Mid->m, Treble->t (the standard FMV mapping).
        toneClean.setComponents(200e3, 200e3, 20e3, 56e3, 220e-12, 100e-9, 47e-9);
        toneClean.update(sampleRate, treble, mid, bass);
        // Crunch/OD1/OD2: shared Marshall TMB (same R/C, one instance per voice).
        toneCrunch.setComponents(200e3, 1.0e6, 20e3, 33e3, 470e-12, 22e-9, 22e-9);
        toneOd1.setComponents(200e3, 1.0e6, 20e3, 33e3, 470e-12, 22e-9, 22e-9);
        toneOd2.setComponents(200e3, 1.0e6, 20e3, 33e3, 470e-12, 22e-9, 22e-9);
        toneCrunch.update(sampleRate, treble, mid, bass);
        toneOd1.update(sampleRate, treble, mid, bass);
        toneOd2.update(sampleRate, treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f, 0.72f, 1.0f - 1.0f * pushed);

        // 4x EL34 power amp (~100W), fixed bias (~-40V). The preamp already delivers
        // a hot, compressed signal to the PI, so the power drive is moderate and the
        // sag depth is LOW (the JVM has a stiff modern supply — high sag would pump
        // and EXPAND the crest, undoing the preamp distortion). MASTER + morph add
        // a little more cook on the cranked lead voices. NFB = Presence/Resonance.
        const float mPush = smoothstep(master);
        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f,
                       0.14f, 0.45f, 1.00f);
        phaseInverter.setMarshall(sampleRate, 0.95f + 1.30f * m + 0.65f * pushed + 0.35f * mPush, 0.90f);
        supply.set(sampleRate,
                   16.0f, 100.0f,
                   1000.0f, 50.0f,
                   10000.0f, 22.0f,
                   0.05f + 0.025f * pushed,
                   0.04f + 0.025f * pushed,
                   0.025f + 0.020f * dirtW,
                   0.13f);
        power.set(sampleRate, 1.3f + 1.6f * mPush + 3.0f * m + 2.0f * pushed, -40.0f,
                  0.07f, 55.0f, 11200.0f);
        power.out = 0.011f;

        // power-amp NFB: Presence (HF) + Resonance (LF) + phase inverter rolloff
        phaseHp.setHighPass(sampleRate, 70.0f + 28.0f * dirtW, 0.72f);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1400.0f * treble + 600.0f * presence - 2000.0f * pushed, 0.64f);
        presenceShelf.setHighShelf(sampleRate, 2600.0f + 800.0f * presence, 0.78f, -1.2f + 8.0f * presence + 1.0f * treble);
        resonanceShelf.setLowShelf(sampleRate, 95.0f + 38.0f * resonance, 0.78f, -2.0f + 7.0f * deep + 1.6f * dirtW);
        resonancePeak.setPeaking(sampleRate, 116.0f + 28.0f * resonance, 0.92f, 0.4f + 4.4f * deep + 1.2f * bass);

        // Marshall 4x12 voicing — a real cab ROLLS OFF the top (no +9 dB fizz shelf
        // that inflates crest without distorting); gentle HF cut + LP.
        speakerHp.setHighPass(sampleRate, 82.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 122.0f, 0.84f, 0.8f + 2.0f * bass + 1.6f * deep);
        speakerLowMid.setPeaking(sampleRate, 420.0f + 90.0f * mid, 0.74f, -1.2f + 1.6f * mid);   // the Marshall mid dip
        speakerBite.setPeaking(sampleRate, 2700.0f + 480.0f * treble, 0.78f, 2.3f + 2.0f * treble + 0.8f * presence - 0.5f * pushed);
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, -1.8f + 2.0f * treble + 2.0f * presence - 1.6f * pushed);
        speakerLp.setLowPass(sampleRate, 13000.0f + 1800.0f * treble - 2600.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); brightCap.reset();
        cleanBody.reset(); crunchBody.reset(); od1Tight.reset(); od1Bite.reset(); od2Tight.reset(); od2Bite.reset();
        interHp.reset(); interLp.reset(); cathodeLp.reset();
        toneClean.reset(); toneCrunch.reset(); toneOd1.reset(); toneOd2.reset(); stackMakeupLow.reset();
        phaseHp.reset(); phaseLp.reset(); presenceShelf.reset(); resonanceShelf.reset(); resonancePeak.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset(); speakerFizz.reset(); speakerLp.reset();
        dcBlock.reset(); rev.clear();
        v6aMiller.reset(); cleanMiller.reset(); crunchMiller.reset();
        od1aMiller.reset(); od1bMiller.reset(); od2aMiller.reset(); od2bMiller.reset(); od2cMiller.reset();
        v6a.reset(); vClean.reset(); vCrunch1.reset(); vOd1a.reset(); vOd1b.reset(); vOd2a.reset(); vOd2b.reset(); vOd2c.reset();
        cleanCouple.reset(); crunchCouple.reset();
        od1CoupleAB.reset(); od2CoupleAB.reset(); od2CoupleBC.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
        setupTubes();
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; rev.setSampleRate(sampleRate); reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kChannel:   channel = v; break;
            case kMode:      mode = v; break;
            case kGain:      gain = v; break;
            case kVolume:    volume = v; break;
            case kBass:      bass = v; break;
            case kMiddle:    mid = v; break;
            case kTreble:    treble = v; break;
            case kPresence:  presence = v; break;
            case kResonance: resonance = v; break;
            case kMaster:    master = v; break;
            case kReverb:    reverb = v; break;
            case kCabSim:    cabSim = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kJvm410Def[i]); }

    float process(float in)
    {
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        const float pushed = smoothstepRange(0.40f, 0.92f, m);
        const float mPush = smoothstep(master);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = brightCap.process(x);
        // V6a shared input stage (REAL 12AX7), driven a touch harder on dirty voices.
        x = v6a.process(v6aMiller.process(x) *
                        (1.30f + 0.60f * m + 0.20f * dirtW) * bplus.preamp);

        // --- four channel voices off the shared V6a, mixed by weight. Like a real
        //     Marshall the GAIN pot + cascade triodes CLIP FIRST, then the passive
        //     tone stack shapes (the TMB/Bassman stack is post-preamp). gDrive (the
        //     GAIN pot) sets how hard each stage is slammed -> more cascade clip. ---

        // CLEAN: ONE gentle recovery stage then its own Bassman-type stack -> clean.
        float clean = cleanBody.process(x);
        clean = cleanCouple.process(clean, 0.52f + 0.80f * gain);
        clean = vClean.process(cleanMiller.process(clean) * (0.8f + 1.2f * gDrive) * bplus.preamp);
        clean = (float)toneClean.process(clean) * 5.0f;     // own stack + insertion-loss makeup

        // CRUNCH: ONE hot cascade stage then Marshall TMB (the classic crunch). The
        // GAIN pot span is wide so it cleans up low and crunches hard at 10.
        float crunch = crunchBody.process(x);
        crunch = crunchCouple.process(crunch, 0.70f + 4.6f * gDrive + 1.2f * modeA);
        crunch = vCrunch1.process(crunchMiller.process(crunch) *
                                  (2.2f + 16.0f * gDrive + 2.0f * modeA) * bplus.preamp);
        crunch = (float)toneCrunch.process(crunch) * 3.2f;

        // OD1: TWO cascaded stages then TMB (the JVM OD1 lead voice). Lower floor so
        // RS Gain has travel (cleans up a bit low, sings at 10).
        float od1 = od1Tight.process(x);
        od1 = od1Bite.process(od1);
        od1 = vOd1a.process(od1aMiller.process(od1) *
                            (1.8f + 22.0f * gDrive + 2.6f * modeA) * bplus.preamp);
        od1 = od1CoupleAB.process(od1, 0.95f + 6.6f * gDrive + 1.8f * modeA);
        od1 = vOd1b.process(od1bMiller.process(od1) *
                            (1.6f + 10.0f * gDrive) * bplus.preamp);
        od1 = (float)toneOd1.process(od1) * 3.2f;

        // OD2: THREE-stage cascade then TMB (hottest, extra red-mode saturation).
        float od2 = od2Tight.process(x);
        od2 = od2Bite.process(od2);
        od2 = vOd2a.process(od2aMiller.process(od2) *
                            (2.4f + 26.0f * gDrive + 3.4f * modeA) * bplus.preamp);
        od2 = od2CoupleAB.process(od2, 1.05f + 7.4f * gDrive + 2.2f * modeA);
        od2 = vOd2b.process(od2bMiller.process(od2) *
                            (2.0f + 13.0f * gDrive) * bplus.preamp);
        od2 = od2CoupleBC.process(od2, 0.95f + 5.6f * gDrive + 1.8f * modeA);
        od2 = vOd2c.process(od2cMiller.process(od2) *
                            (1.8f + 9.0f * gDrive + 1.8f * modeA) * bplus.preamp);
        od2 = (float)toneOd2.process(od2) * 3.2f;

        float y = clean * cleanW + crunch * crunchW + od1 * od1W + od2 * od2W;

        y = interHp.process(y);
        y = interLp.process(y);
        y = cathodeLp.process(y);
        y = stackMakeupLow.process(y);

        // VOLUME (selected channel volume) sets how hard the preamp drives the PI.
        const float chDrive = 0.66f + 0.78f * volume;
        y *= chDrive;

        y = phaseHp.process(y);
        y = phaseLp.process(y);
        y = coupleToPi.process(y, 1.0f + 0.14f * pushed);
        lastPreampLoad = 0.10f * std::fabs(y) + 0.05f * m;
        y = phaseInverter.process(y * bplus.preamp);
        lastPowerLoad = 0.85f * std::fabs(y) + 0.20f * pushed;
        lastScreenLoad = 0.52f * std::fabs(y) + 0.10f * m;

        // 4x EL34 power amp (~100W) — REAL pentode table + OT. The JVM's modern
        // diode supply stays stiff via the B+ scales above.
        y = power.process(y * bplus.power * bplus.screen);

        y = presenceShelf.process(y);
        y = resonanceShelf.process(y);
        y = resonancePeak.process(y);
        y = dcBlock.process(y);

        float cab = speakerHp.process(y);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizz.process(cab);
        cab = speakerLp.process(cab);
        y += cabSim * (cab - y);

        // op-amp digital reverb (parallel send), off when REVERB = 0
        if (reverb > 0.0005f)
        {
            const float wet = rev.process(y);
            y += wet * reverb * 0.55f;
        }

        // Loudness normalization: keeps the multitone RMS ~constant across the
        // GAIN/channel sweep so the shared output stage stays calibrated. The real
        // tubes + EL34 do the distortion; the output tanh is gentle OT saturation.
        // NO cleanMakeup (it inverts the crest curve by slamming clean tones into
        // the clip — the DSL100/JCM800 lesson).
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.011f * std::fabs((presence - 0.5f) * 16.0f)
            + 0.010f * std::fabs((resonance - 0.5f) * 16.0f);
        const float level = (0.86f + 0.10f * (1.0f - m)) /
            ((1.0f + 0.34f * mPush + 0.55f * pushed) * toneEnergy * chDrive);

        // MASTER volume. Centred at 0.5 = unity so RS songs that leave it at the
        // musical default keep the calibrated loudness.
        const float masterGain = 0.55f + 0.90f * master;

        // loudness flattening vs the Clean->OD2 morph (clean post-output makeup; ~0 dB
        // mid-sweep) so one output calibration spans the whole RS Gain range.
        float gcDb = 4.6f - 8.5f * m;
        if (gcDb > 14.0f) gcDb = 14.0f; else if (gcDb < -12.0f) gcDb = -12.0f;
        return softClip(y * level * masterGain) * 0.97f * std::pow(10.0f, 0.05f * gcDb);
    }
};

class Jvm410Plugin : public Plugin
{
    Jvm410Core left;
    Jvm410Core right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;          // 2x anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    Jvm410Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kJvm410Def[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarstenJVM410"; }
    const char* getDescription() const override { return "Marsten JVM410 style amp (4 channels)"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('J', 'v', '4', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kJvm410Names[index];
        parameter.symbol = kJvm410Symbols[index];
        parameter.ranges.min = kJvm410Min[index];
        parameter.ranges.max = kJvm410Max[index];
        parameter.ranges.def = kJvm410Def[index];
    }

    float getParameterValue(uint32_t index) const override { return index < (uint32_t)kParamCount ? params[index] : 0.0f; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount) return;
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Jvm410Plugin)
};

Plugin* createPlugin() { return new Jvm410Plugin(); }

END_NAMESPACE_DISTRHO
