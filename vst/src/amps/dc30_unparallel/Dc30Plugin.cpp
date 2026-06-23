/*
 * UNPARALLEL DC30 — Matchless DC30 for the game's Amp_BT30. Parody brand; the
 * in-app face must never read "Matchless".
 *
 * CIRCUIT-REAL rebuild (Guitarix-style, our Koren/Yeh physics — NOT GPL source),
 * from the hand-traced 4-page DC30 schematic. Mirrors the en30 (BOX AC30 = AC30,
 * same EL84/class-A family) circuit-real template: real cathode-biased 12AX7 stages
 * (rbtube::TubeStage), real Vox top-boost tone network (rbtube::ToneStackYeh), real
 * class-A push-pull EL84 power amp with NO NFB + supply sag (rbtube::PowerAmpPP),
 * 2x oversampling. NO tanh/asymTube stand-ins anywhere (only the final OT soft-clip).
 *
 * SCHEMATIC (the real DC30, two channels into a shared 4xEL84 class-A power amp):
 *
 *   CHANNEL 1 "Brilliant" (schematic p.1): two 12AX7 triode stages.
 *     - Input grid: 68k + 68k stoppers, 1M grid-leak  -> Ri 68k table on V1.
 *     - V1 (pin5): 220k plate, cathode 1k5 + 25uF/25V bypass (fck ~4.2 Hz, fully
 *       bypassed = full gain/low end — the Matchless trait), B+ .1uF/400V filtered.
 *     - coupling 560pF + 180pF in series (~136pF) -> 500kA VOLUME pot. That small
 *       series cap into 500k = ~2.3 kHz HP = the glassy/bright top-boost coupling.
 *     - V2 (pin4): 100k plate, cathode 1k5 + 25uF/25V bypass.
 *     - VOX TOP-BOOST stack after V2: TREBLE 220k pot + 560pF treble cap, BASS 1M
 *       pot, slope 56k, 10k+100k dividers, two .022uF caps. (NO mid control.)
 *       Cross-check Guitarix ts.ac30: R1=1M R2=1M R3=10k R4=100k C1=50pF C2=22nF
 *       C3=22nF — same .022uF/10k/100k/~1M architecture; the DC30 uses a 220k
 *       treble pot + 560pF treble cap (its own top-boost voicing), used here.
 *
 *   CHANNEL 2 "EF86" (schematic p.2): one EF86/6267 pentode, higher gain, fatter.
 *     - Input 68k+68k, 1M grid-leak. Plate 330k to B+, screen 2M2, cathode 2k2 +
 *       25uF/25V (fck ~2.9 Hz). Modeled with the EF86 Koren pentode table generated
 *       from the local EF86.pdf + the DC30 330k plate load, with negligible pentode
 *       Miller (Cag1 max 0.05pF) instead of the old 12AX7 stand-in.
 *     - 6-position TONE rotary (caps 360pF..0.01uF into 1M5) -> a real one-pole LP
 *       sweep dark(295 Hz)->bright. Then 180pF -> 1MA VOLUME.
 *
 *   PHASE INVERTER + POWER (schematic p.3): a 12AX7 long-tail PI fed by both
 *     channels, with the CUT control (250kA + .022 across the PI) = a post-PI
 *     treble cut (higher = darker). 4x EL84, shared 1k5 cathode + 25uF bias (class-A,
 *     cathode-biased), 100R grid stoppers, 220k grid-leaks, OT (WTI 9356), Hi/Lo
 *     power, NO global negative feedback. Tube rectifier (SV4/SAR4) -> supply sag.
 *     Power-supply B+ ~320-350V (measured on the sheet).
 *
 * GAME MAP (Dc30Params.h, unchanged): RS Gain -> Ch1 Volume (drives the EL84
 * breakup), Channel pinned to Ch1 Brilliant via _static; Bass/Treble -> Ch1 top-boost
 * stack; Ch2 Volume/Tone/Cut/Master via _static. The DSP keeps the full panel and
 * morphs Ch1<->Ch2 on the Channel param.
 *
 * STEREO I/O, two mono cores (matches the existing wrapper).
 */
#include "DistrhoPlugin.hpp"
#include "Dc30Params.h"
#include "../../_shared/tube_stage.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): transparent below
// +/-0.90, saturates to a +/-0.99 ceiling so EQ boosts never hard-clip. This is the
// ONLY soft-clip in the chain (final OT/output limiter), per the circuit-real rules.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

namespace {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float smoothstep(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }

// RBJ biquad (peaking / shelves / low/high-pass) — voicing only, sits between the
// real tube stages as ordinary stable filters (same role as en30's Biquad).
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void norm(float a0){ b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0; }
    void peaking(float sr,float f,float Q,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=1+al*A;b1=-2*c;b2=1-al*A; float a0=1+al/A;a1=-2*c;a2=1-al/A; norm(a0); }
    void highShelf(float sr,float f,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)+(A-1)*c+2*rA*al); b1=-2*A*((A-1)+(A+1)*c); b2=A*((A+1)+(A-1)*c-2*rA*al);
        float a0=(A+1)-(A-1)*c+2*rA*al; a1=2*((A-1)-(A+1)*c); a2=(A+1)-(A-1)*c-2*rA*al; norm(a0); }
    void lowpass(float sr,float f,float Q){ f=fminf(f,sr*0.49f); float w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=(1-c)/2;b1=1-c;b2=(1-c)/2; float a0=1+al;a1=-2*c;a2=1-al; norm(a0); }
    void highpass(float sr,float f,float Q){ f=fminf(f,sr*0.49f); float w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=(1+c)/2;b1=-(1+c);b2=(1+c)/2; float a0=1+al;a1=-2*c;a2=1-al; norm(a0); }
};

} // namespace

// ---------------------------------------------------------------------------
// Dc30Core — circuit-real DC30. One mono core (the amp is a mono device).
// ---------------------------------------------------------------------------
class Dc30Core
{
    float sr = 48000.0f;

    // params (0..1)
    float pCh1Vol = kDc30Def[kCh1Volume];
    float pBass   = kDc30Def[kBass];
    float pTreble = kDc30Def[kTreble];
    float pCh2Vol = kDc30Def[kCh2Volume];
    float pTone   = kDc30Def[kTone];
    float pCut    = kDc30Def[kCut];
    float pMaster = kDc30Def[kMaster];
    float pChannel= kDc30Def[kChannel];
    float pCabSim = kDc30Def[kCabSim];

    // --- input / shared ---
    rbtube::HP1 inputCoupling;                  // input grid-leak coupling (~12 Hz)
    Biquad pickupLoad;                          // cable HF load before the tube grids

    // --- CHANNEL 1 "Brilliant": two real 12AX7 stages + Vox top-boost ---
    rbtube::TubeStage c1v1, c1v2;               // V1 (220k/1k5/25uF), V2 (100k/1k5/25uF)
    rbtube::Miller12AX7 c1InputMiller;          // 68k input stopper + V1 Miller capacitance
    rbtube::Miller12AX7 c1V2Miller;             // Volume/coupling source -> V2 Miller capacitance
    rbtube::CouplingCapGridLeak c1Couple;       // 560pF+180pF series into 500k vol + grid blocking
    rbtube::ToneStackYeh topBoost;              // real Vox top-boost R/C network (Yeh)

    // --- CHANNEL 2 "EF86": one real small-signal pentode + tone rotary ---
    rbtube::TubeStageEF86 c2v1;                 // EF86/6267, 330k plate, cathode 2k2/25uF
    rbtube::MillerEF86 c2InputMiller;           // EF86 Cg1/Cag1 Miller from datasheet
    Biquad ef86Body;                            // pentode body bump (330k/2M2 plate)
    rbtube::LP1 ef86ToneLP;                     // 6-pos TONE rotary as a one-pole LP (dark->bright)
    Biquad ef86Couple;                          // 180pF -> 1MA vol coupling

    // --- shared phase inverter Cut + power + voicing ---
    Biquad cutLP;                               // CUT in the PI (post, treble cut, higher=darker)
    rbtube::CouplingCapGridLeak coupleToPi;     // channel mix/master -> PI grid blocking
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;              // rectifier + B+ nodes
    rbtube::PowerAmpPP power;                    // real class-A push-pull EL84, NO NFB + sag
    Biquad spkBody, spkPres, spkRoll;           // mild PRE-CAB 2x12 voicing (cab IR adds the cab)

    // derived gain staging (copied from en30 numbers, calibrated for the real chain)
    float chan = 0.0f;                          // 0 = Ch1 Brilliant, 1 = Ch2 EF86
    float inGain = 1, inScale = 2, preGain = 1, gainOut = 1, outLevel = 0.5f;
    float lastPowerLoad = 0.0f, lastScreenLoad = 0.0f, lastPreampLoad = 0.0f;

    void recalc()
    {
        chan = smoothstep(pChannel);

        // --- input network (68k+68k grid stoppers + 1M leak, cable load) ---
        inputCoupling.set(sr, 12.0f);
        pickupLoad.lowpass(sr, 13500.0f, 0.62f);
        c1InputMiller.set(sr,  68000.0f, 55.0f, 8.0f);      // channel 1 input Miller, ~25 kHz
        c1V2Miller.set(sr,    220000.0f, 52.0f, 8.0f);      // 500k volume source + V2 Miller, ~8 kHz

        // ===== CHANNEL 1 "Brilliant": two cathode-biased 12AX7 stages =====
        // Real schematic values. V1: 220k plate, Rk 1k5 + 25uF/25V (fck 4.2 Hz, fully
        // bypassed -> full gain), grid-leak 68k -> Ri tab 0. V2: 100k plate, Rk 1k5 +
        // 25uF. Same Koren table / divider / B+ as en30 (the AC30 sibling). Each
        // self-biases (Vk0 solved) and saturates on its own load line.
        c1v1.set(sr, 0, 250.0f, 40.0f, 4.2f, 1500.0f);     // V1 (68k grid-leak, 1k5/25uF)
        c1v2.set(sr, 1, 250.0f, 40.0f, 4.2f, 1500.0f);     // V2 (250k grid-leak, 1k5/25uF)
        // 560pF+180pF series (~136pF) into the 500kA Volume = ~2.3 kHz bright
        // coupling. Use the component block instead of a static HP so the V2 grid
        // can shift/recover when the Brilliant channel is cranked.
        c1Couple.set(sr, 500000.0f, 136.0e-12f, 220000.0f,
                     0.11f, 0.40f, 0.95f);
        // Vox top-boost stack = the REAL R/C network (Yeh), DC30 values from the
        // schematic (Treble 220k pot + 560pF cap, Bass 1M pot, slope 56k, .022uF caps;
        // architecture cross-checked vs Guitarix ts.ac30). No Mid knob: Treble->t,
        // Bass->m (the active body node at Vox values), l fixed at 0.5.
        topBoost.setComponents(220e3, 1.0e6, 10e3, 56e3, 560e-12, 22e-9, 22e-9);
        topBoost.update(sr, pTreble, 0.15f + 0.55f * pBass, 0.5f);

        // ===== CHANNEL 2 "EF86": real EF86 pentode stage + tone rotary =====
        // Local EF86.pdf: Va 250V, Vg2 140V, Vg1 -2.2V, Ia 3mA, gm 2.2mA/V,
        // Cg1(all except anode)=3.8pF, Cag1<=0.05pF. The generated table uses the
        // DC30's 330k plate load; this setWithPlate keeps the cathode feedback solve
        // aligned with that load line. Cathode 2k2 + 25uF/25V => fck ~2.9 Hz.
        c2v1.setWithPlate(sr, 0, 250.0f, 28.0f, 2.9f, 2200.0f, 330000.0f);
        c2InputMiller.set(sr, 68000.0f, 130.0f, 8.0f);      // EF86 pentode Miller is small but real
        ef86Body.peaking(sr, 520.0f, 0.80f, 2.4f);          // thick midrange (330k/2M2 plate)
        // 6-position TONE rotary: caps 360pF..0.01uF into 1M5 -> one-pole LP, the real
        // dark(295 Hz)->bright sweep. tone=0 dark, tone=1 bright.
        ef86ToneLP.set(sr, 600.0f + 9000.0f * pTone);
        ef86Couple.highpass(sr, 95.0f, 0.70f);              // 180pF -> 1MA vol coupling

        // ===== gain staging (en30 numbers, RS Gain = Ch1 Volume cooks the cascade) =====
        // Ch1 Volume is the linear inter-stage pre-gain (Guitarix `*(preamp)`), so
        // turning it up drives V1->V2 clean->crunch->plateau with no ad-hoc curve.
        float drv  = (1.0f - chan) * pCh1Vol + chan * pCh2Vol;   // active channel's volume
        float vol  = std::pow(drv, 1.1f);                        // audio taper
        inGain     = 0.40f + 1.6f * (1.0f - chan) * 0.5f + 1.6f * chan * 0.3f; // mild input drive
        inScale    = 2.0f * (0.7f + 0.6f * drv);                 // audio -> grid volts into V1 (keep V1 cleaner)
        preGain    = 0.35f + 3.5f * vol;                         // inter-stage pre-gain (clean floor + hot slope)
        gainOut    = 0.60f + 0.55f * vol;                        // post-preamp level into the power amp

        // ===== shared CUT (in the phase inverter; higher = darker) =====
        cutLP.lowpass(sr, 900.0f + 4200.0f * (1.0f - pCut), 0.7f);
        coupleToPi.set(sr, 1000000.0f, 22.0e-9f, 100000.0f, 0.12f, 0.55f, 1.35f);
        phaseInverter.setVoxAc30(sr, 0.95f + 1.35f * vol + 0.45f * pMaster + 0.30f * chan, 0.88f);
        supply.setGZ34Ac30(sr, 0.55f + 0.45f * vol);

        // ===== 4x EL84 class-A push-pull power amp (real pentode, NO NFB) =====
        // The DC30 has no preamp master in the classic sense; the channel VOLUME cooks
        // the power amp (cathode-biased class A, tube-rectifier sag). Master adds a mild
        // extra push. Bias at the class-A point; sag/cathode handle the bloom/compression.
        power.set(sr, 3.5f + 16.0f * vol + 2.5f * pMaster, -7.5f, 0.14f);
        power.out = 0.0075f;                                     // plate-volt differential -> signal

        // ===== mild PRE-CAB 2x12 AC30/"blue" voicing (cab IR adds the real cab) =====
        spkBody.peaking(sr, 110.0f, 0.8f, 2.0f + 1.5f * pBass);  // gentle low body
        spkPres.highShelf(sr, 2500.0f, -4.0f + 9.0f * pTreble);  // Treble shapes the chime region
        spkRoll.highShelf(sr, 4000.0f, 4.0f - 1.0f * chan);      // top chime (EF86 a touch darker)

        // output level comp: keep loudness roughly constant across the gain sweep
        outLevel = 0.5f * (1.0f - 0.40f * drv);
    }

public:
    void reset()
    {
        inputCoupling.reset(); pickupLoad.reset();
        c1InputMiller.reset(); c1v1.reset(); c1V2Miller.reset(); c1v2.reset(); c1Couple.reset(); topBoost.reset();
        c2InputMiller.reset(); c2v1.reset(); ef86Body.reset(); ef86ToneLP.reset(); ef86Couple.reset();
        cutLP.reset(); coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        spkBody.reset(); spkPres.reset(); spkRoll.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
    }

    void setSampleRate(float s) { sr = s > 1000.0f ? s : 48000.0f; recalc(); reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kCh1Volume: pCh1Vol = v; break;
            case kBass:      pBass = v; break;
            case kTreble:    pTreble = v; break;
            case kCh2Volume: pCh2Vol = v; break;
            case kTone:      pTone = v; break;
            case kCut:       pCut = v; break;
            case kMaster:    pMaster = v; break;
            case kChannel:   pChannel = v; break;
            case kCabSim:    pCabSim = v; break;
            default: break;
        }
        recalc();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kDc30Def[i]); }

    inline float process(float x)
    {
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        x = inputCoupling.process(x * inGain);
        x = pickupLoad.process(x);

        // ---- CHANNEL 1 "Brilliant": V1 -> top-boost -> coupling -> V2 ----
        float c1 = c1v1.process(c1InputMiller.process(x) * inScale * bplus.preamp); // audio -> Miller-loaded V1 grid
        c1 = topBoost.process(c1);                           // real Vox top-boost between V1 and V2
        c1 = c1Couple.process(c1, preGain);                  // bright coupling + Volume pre-gain/blocking
        c1 = c1v2.process(c1V2Miller.process(c1) * bplus.preamp);           // Miller-loaded V2

        // ---- CHANNEL 2 "EF86": one hot stage -> body -> tone rotary ----
        float c2 = c2v1.process(c2InputMiller.process(x) * inScale * 1.10f * bplus.preamp); // real EF86 pentode, Miller-loaded
        c2 = ef86Body.process(c2);
        c2 = ef86ToneLP.process(c2);
        c2 = ef86Couple.process(c2) * (0.6f + 0.8f * preGain);

        // channel-select morph (RS pins Ch1; the DSP still supports the full panel)
        float y = chan * c2 + (1.0f - chan) * c1;

        y *= gainOut;
        y = coupleToPi.process(y, 1.0f + 0.12f * preGain);
        lastPreampLoad = 0.10f * std::fabs(y) + 0.04f * (1.0f - chan + pCh2Vol * chan);
        y = phaseInverter.process(y * bplus.preamp);
        y = cutLP.process(y);                                // CUT in the PI treble path
        lastPowerLoad = 0.72f * std::fabs(y) + 0.18f * pMaster + 0.20f * preGain;
        lastScreenLoad = 0.46f * std::fabs(y) + 0.10f * preGain;
        y = power.process(y * bplus.power * bplus.screen);   // class-A EL84 PP, no NFB + B+ sag
        float cab = spkRoll.process(spkPres.process(spkBody.process(y)));  // mild pre-cab voicing
        y += pCabSim * (cab - y);
        return y * outLevel;
    }
};

class Dc30Plugin : public Plugin
{
    Dc30Core left;
    Dc30Core right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;            // 2x anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    Dc30Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kDc30Def[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "UnparallelDC30"; }
    const char* getDescription() const override { return "Unparallel DC30 - class-A EL84 boutique combo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('U', 'd', '3', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDc30Names[index];
        parameter.symbol = kDc30Symbols[index];
        parameter.ranges.min = kDc30Min[index];
        parameter.ranges.max = kDc30Max[index];
        parameter.ranges.def = kDc30Def[index];
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
            for (int k = 0; k < kOS; ++k)              // core + output soft-clip at 2x
            {
                ubL[k] = rbAmpLvl(0.891f * left.process(ubL[k]));
                ubR[k] = rbAmpLvl(0.891f * right.process(ubR[k]));
            }
            outL[i] = osL.downsample(ubL);
            outR[i] = osR.downsample(ubR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dc30Plugin)
};

Plugin* createPlugin() { return new Dc30Plugin(); }

END_NAMESPACE_DISTRHO
