/*
 * CITRUS OR50 - Orange OR50H (vintage "Graphic" single-channel head) for the
 * game's Amp_OrangeOR50H. Parody brand "Citrus" (same as the AD200 / OR100 /
 * Tiny Terror); the in-app face must NEVER read "Orange".
 *
 * CIRCUIT-REAL DSP, rebuilt schematic-first from the local Orange schematics:
 *   amps/Orange OR50/Orange OR15 SCH 115V-230V.pdf  (the modern Orange "OR"
 *       preamp + tone stack, full PCB schematic with EVERY component value — the
 *       OR50 shares this exact preamp/tone topology)
 *   amps/Orange OR50/OR30 preamp.jpg                (hand-drawn 3-triode cascade,
 *       confirms 100K plate loads, 1k5 cathode + fat cap, partial-bypass middle)
 *   amps/Orange OR50/Orange Retro 50 Layout.gif     (12AX7 preamp + 2x EL34 ~50W,
 *       250K Master / 500K Bass / 50K Mid / 500K Treble / 500K Gain, GZ34 supply,
 *       Class A 30W or Class AB 50W via the FULL/HALF rocker)
 *   amps/Orange OR50/1.jpg, 2.jpg                   (front panel: pics-only knobs)
 *
 * Guitarix cross-check (architecture/values only, GPL — never copied):
 *   tools/ampsim/DK/gschem-schematics/OrangeDarkTerrorp3.sch  — confirms the Orange
 *       12AX7 stage: 100K plate (R5), 1.2k + 47u cathode (R1/C5 = full bypass), 0.1u
 *       coupling (C4), 1M grid leaks (R4/R8). Matches the OR15 V1-A exactly.
 *   src/faust/tonestack.dsp  — no Orange entry (Orange uses the simple FMV Bass/Mid/
 *       Treble); the Yeh 3rd-order polynomial there == our rbtube::ToneStackYeh, fed
 *       the REAL OR15 values (R1 250K Treble, R2 500K Bass, R3 25K Mid, R4 47K slope,
 *       C1 235pF = 470p+470p series, C2/C3 22nF). The H&K "groove" entry is the
 *       closest stock match (220k/1M/22k/68k) but we use the measured OR15 values.
 *   PlexiPowerAmpEL34.sch  — EL34 power reference (12AX7 PI, ~400V plate, -35V bias).
 *
 * SIGNAL PATH (every real stage = a real part, NO tanh stand-ins):
 *   IN -> R1 68K + R2 1M grid leak (input HP + pickup-load LP)
 *      -> V1-A 12AX7  (REAL): 100K plate, 1k5 + 10u FULL cathode bypass -> fat
 *      -> C2 1n0 coupling + 470p treble-bleed across the GAIN pot (RV1 A1M)
 *      -> V1-B 12AX7  (REAL, = the GAIN stage): 1k + 10u full bypass
 *      -> C7 2n2 coupling
 *      -> V2-A 12AX7  (REAL): 1k5 + 10u full bypass
 *      -> C10 4n7 coupling
 *      -> Orange FMV TONE STACK (REAL Yeh, double precision) Bass/Mid(FAC)/Treble
 *      -> DEPTH (the bass-cap low-shelf voicing) + the thick Orange FAC mid bump
 *      -> V3-A 12AX7  (REAL recovery into the VOLUME / power-amp PI)
 *      -> VOLUME (master)
 *      -> 2x EL34 push-pull (REAL pentode tables, ~50W; HALF -> earlier breakup/sag)
 *      -> presence voicing (fixed; OR50 has no presence knob) + OT softClip
 *      -> Orange PPC 4x12 speaker voicing
 *
 * the game (Amp_OrangeOR50H, rs_gear_to_vst.json -> CitrusOR50.vst3): RS Gain ->
 * GAIN; Bass/Mid/Treble -> tone stack. VOLUME + DEPTH pinned to musical defaults
 * via _static (all editable, incl. the FULL/HALF switch).
 */
#include "DistrhoPlugin.hpp"
#include "Or50Params.h"
#include "../../_shared/tube_stage.hpp"   // real 12AX7 + EL34 circuit models (Koren/Yeh)
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): matches the amp to
// the common multitone loudness; soft knee transparent below +/-0.90, saturates to
// a +/-0.99 ceiling so EQ boosts never hard-clip.
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

} // namespace

class Or50Core
{
    float sampleRate = 48000.0f;
    float gain   = kOr50Def[kGain];
    float bass   = kOr50Def[kBass];
    float mid    = kOr50Def[kMiddle];
    float treble = kOr50Def[kTreble];
    float depth  = kOr50Def[kDepth];
    float volume = kOr50Def[kVolume];
    float half   = kOr50Def[kHalf];
    float cabSim = kOr50Def[kCabSim];

    // Linear voicing biquads around the real nonlinear stages.
    Biquad inputHp, pickupLoad, preBody, brightCap;
    Biquad stage1Hp, interHp, cathodeLp;
    Biquad depthShelf, midThick, stackMakeupLow, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;

    // ── REAL tube stages (Koren circuit models) — NO tanh stand-ins ──
    rbtube::TubeStage v1a;        // 12AX7 input stage (100K plate, full cathode bypass -> fat)
    rbtube::TubeStage v1b;        // 12AX7 GAIN stage (driven by the A1M gain pot)
    rbtube::TubeStage v2a;        // 12AX7 final preamp stage into the tone stack
    rbtube::TubeStage v3a;        // 12AX7 recovery into the VOLUME / power-amp PI
    rbtube::Miller12AX7 v1aMiller, v1bMiller, v2aMiller, v3aMiller;
    rbtube::CouplingCapGridLeak coupleV1aToV1b;
    rbtube::CouplingCapGridLeak coupleV1bToV2a;
    rbtube::CouplingCapGridLeak coupleToneToV3a;
    rbtube::ToneStackYeh tone;    // Orange FMV Bass/Mid(FAC)/Treble (double precision)
    rbtube::CouplingCapGridLeak coupleToPi;  // master -> PI grid blocking
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;           // GZ34 + B+ filter nodes
    rbtube::PowerAmpEL34 power;   // 2x EL34 push-pull (~50W)
    float lastPowerLoad = 0.0f, lastScreenLoad = 0.0f, lastPreampLoad = 0.0f;

    static float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }

    void updateFilters()
    {
        const float g = smoothstep(gain);
        const float pushed = smoothstepRange(0.40f, 0.92f, gain);
        const float mPush = smoothstep(volume);
        const float halfP = (half >= 0.5f) ? 1.0f : 0.0f;

        // ── REAL 12AX7 cascade (cathode-biased, self-bias solved on the load line) ──
        // All four triodes share the Orange 100K plate / 1k5-ish cathode topology
        // (Ri tab 1 = 250k grid-leak family, vplus ~250V, divider 40). The cathode
        // bypass corner sets the "fat vs honk" voice:
        //   V1-A: 1k5 + 10u FULL bypass -> low corner ~10 Hz -> fat, full gain.
        v1a.set(sampleRate, 1, 250.0f, 40.0f, 10.0f, 1500.0f);
        //   V1-B (GAIN): 1k + 10u FULL bypass -> the big Orange gain.
        v1b.set(sampleRate, 1, 250.0f, 40.0f, 10.0f, 1000.0f);
        //   V2-A: 1k5 + 10u full bypass into the tone stack.
        v2a.set(sampleRate, 1, 250.0f, 40.0f, 10.0f, 1500.0f);
        //   V3-A: recovery (1k5 cathode, partial bypass ~55 Hz like the plexi recovery).
        v3a.set(sampleRate, 1, 250.0f, 40.0f, 55.0f, 1500.0f);
        v1aMiller.set(sampleRate,  68000.0f, 55.0f, 8.0f);
        v1bMiller.set(sampleRate, 220000.0f, 52.0f, 8.0f);
        v2aMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        v3aMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        // Orange coupling caps are part of the sound: tight lows into the gain
        // stage, then slower recovery/blocking as the cascade is driven.
        coupleV1aToV1b.set(sampleRate, 1000000.0f, 1.0e-9f, 220000.0f,
                           0.12f, 0.46f, 1.20f);
        coupleV1bToV2a.set(sampleRate, 470000.0f, 4.7e-9f, 180000.0f,
                           0.13f, 0.50f, 1.40f);
        coupleToneToV3a.set(sampleRate, 1000000.0f, 22.0e-9f, 180000.0f,
                            0.11f, 0.38f, 0.95f);

        // Orange FMV tone stack (the REAL OR15 values, double precision -- mandatory
        // at 192k where a float 3rd-order stack goes NaN).
        //   R1 Treble 250K (RV4 B250K), R2 Bass 500K (RV6 A500K, NOT 1M -> tighter
        //   lows), R3 Mid 25K (RV5 B25K), R4 slope 47K (R17), C1 235pF treble cap
        //   (C11 470p in series with C12 470p), C2 22nF, C3 22nF.
        tone.setComponents(250.0e3, 500.0e3, 25.0e3, 47.0e3, 235.0e-12, 22.0e-9, 22.0e-9);
        tone.update(sampleRate, treble, mid, bass);

        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 100000.0f, 0.13f, 0.46f, 1.10f);
        phaseInverter.setComponents(sampleRate,
                                    0.78f + 1.25f * mPush + 0.55f * pushed + 0.36f * halfP,
                                    0.84f, 310.0f, 47000.0f, 47000.0f, 10000.0f, 18.0f, 0.018f);
        // GZ34/5AR4 supply: softer than the OR100 and more reactive in HALF.
        supply.set(sampleRate,
                   115.0f, 32.0f,
                   1200.0f, 32.0f,
                   10000.0f, 16.0f,
                   0.15f + 0.05f * pushed + 0.06f * halfP,
                   0.11f + 0.04f * pushed + 0.04f * halfP,
                   0.060f + 0.022f * gain,
                   0.20f);
        // 2x EL34 push-pull (~50W) -- half the OR100's iron: less headroom, earlier
        // breakup, more sag, tighter lows. HALF (Class A 30W) -> earlier still.
        // B+ nodes above provide the main sag, so the legacy power sag is reduced.
        power.set(sampleRate,
                  4.0f + 7.0f * mPush + 4.0f * pushed + 3.0f * halfP,   // drive
                  -38.0f,                                               // EL34 fixed bias (~-35..-38V)
                  0.10f + 0.05f * halfP,                                // residual OT/power compression
                  60.0f, 11000.0f);                                     // OT band
        power.out = 0.011f;

        // Input network: R1 68K series + R2 1M grid leak (gentle HP), pickup loading.
        inputHp.setHighPass(sampleRate, 44.0f + 36.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12000.0f - 1400.0f * pushed + 800.0f * treble, 0.64f);
        // The Orange upper-mid push going into V1-A (the body that makes the "chunk").
        preBody.setPeaking(sampleRate, 640.0f + 240.0f * mid, 0.80f, 0.6f + 1.6f * mid);
        // C3 470p treble bypass across the GAIN pot: bleeds highs around the gain
        // control, most audible at lower gain (the Orange "bright" on the gain).
        brightCap.setHighShelf(sampleRate, 1500.0f + 1100.0f * treble, 0.72f,
                               -0.5f + 3.2f * (1.0f - gain) + 1.4f * treble);
        // C2 1n0 coupling V1A->V1B (~150 Hz with the 1M leak): tight lows INTO the gain.
        stage1Hp.setHighPass(sampleRate, 150.0f + 45.0f * pushed, 0.70f);
        // C10 4n7 coupling V2A->stack (~72 Hz into the 470k grid).
        interHp.setHighPass(sampleRate, 72.0f + 50.0f * pushed, 0.70f);
        // Miller / coupling rolloff feeding the tone stack.
        cathodeLp.setLowPass(sampleRate, 8800.0f + 1400.0f * treble - 1500.0f * pushed, 0.64f);

        // DEPTH = the bass-cap rotary: a swept low shelf (more depth = bigger lows).
        depthShelf.setLowShelf(sampleRate, 95.0f + 25.0f * depth, 0.72f, eqDb(depth, 9.0f) + 1.0f);
        // The thick Orange FAC midrange (the mid knob = "FAC" frequency-shaped voice).
        midThick.setPeaking(sampleRate, 480.0f + 160.0f * mid, 0.60f, -0.6f + 4.4f * mid + 1.2f * pushed);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f, 0.72f, 1.0f - 1.0f * pushed);
        phaseLp.setLowPass(sampleRate, 11000.0f + 1300.0f * treble - 2000.0f * pushed, 0.64f);
        // Fixed presence voicing (no presence knob on the OR50; the power-amp NFB is
        // a fixed shelf here).
        presenceShelf.setHighShelf(sampleRate, 2700.0f, 0.78f, 1.8f + 1.0f * treble);

        // Orange PPC 4x12 (thick, midrange-forward, smooth top).
        speakerHp.setHighPass(sampleRate, 86.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 124.0f, 0.84f, 0.9f + 2.1f * bass + 1.1f * depth);
        speakerLowMid.setPeaking(sampleRate, 440.0f + 90.0f * mid, 0.72f, 1.2f + 2.0f * mid);
        speakerBite.setPeaking(sampleRate, 2400.0f + 480.0f * treble, 0.78f, 2.0f + 1.8f * treble - 0.5f * pushed);
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, -2.0f + 1.6f * treble - 2.8f * pushed);
        speakerLp.setLowPass(sampleRate, 12500.0f + 1500.0f * treble - 3000.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); preBody.reset(); brightCap.reset();
        stage1Hp.reset(); interHp.reset(); cathodeLp.reset();
        depthShelf.reset(); midThick.reset(); stackMakeupLow.reset(); phaseLp.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset(); speakerFizz.reset(); speakerLp.reset();
        dcBlock.reset();
        v1aMiller.reset(); v1bMiller.reset(); v2aMiller.reset(); v3aMiller.reset();
        v1a.reset(); v1b.reset(); v2a.reset(); v3a.reset(); tone.reset();
        coupleV1aToV1b.reset(); coupleV1bToV2a.reset(); coupleToneToV3a.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kGain:   gain = v; break;
            case kBass:   bass = v; break;
            case kMiddle: mid = v; break;
            case kTreble: treble = v; break;
            case kDepth:  depth = v; break;
            case kVolume: volume = v; break;
            case kHalf:   half = v; break;
            case kCabSim: cabSim = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kOr50Def[i]); }

    float process(float in)
    {
        const float pushed = smoothstepRange(0.40f, 0.92f, gain);
        const float mPush = smoothstep(volume);
        const float halfP = (half >= 0.5f) ? 1.0f : 0.0f;
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = softClip(x * (1.05f + 0.08f * pushed)) * (0.96f - 0.04f * pushed);

        // V1-A 12AX7 (REAL): input gain stage, fat full-bypass cathode.
        float y = preBody.process(x);
        y = v1a.process(v1aMiller.process(y) * (1.6f + 0.8f * gain) * bplus.preamp);

        // C2 coupling + 470p treble bleed across the GAIN pot, then the pot scales
        // how hard V1-B is driven (RS Gain -> GAIN -> clean->crunch->chunk).
        y = stage1Hp.process(y);
        y = brightCap.process(y);
        y = coupleV1aToV1b.process(y, 0.75f + 4.5f * gain + 1.5f * pushed);
        // V1-B 12AX7 (REAL): the GAIN stage — the source of the Orange crunch.
        y = v1b.process(v1bMiller.process(y) * (1.2f + 3.4f * gain + 1.6f * pushed) * bplus.preamp);

        // C7 coupling -> V2-A 12AX7 (REAL): final preamp gain into the tone stack.
        y = interHp.process(y);
        y = coupleV1bToV2a.process(y, 0.72f + 3.2f * gain + 1.8f * pushed);
        y = v2a.process(v2aMiller.process(y) * (1.0f + 1.8f * gain + 1.2f * pushed) * bplus.preamp);
        y = cathodeLp.process(y);

        // Orange FMV tone stack (REAL Yeh, double) + Depth + FAC mid voicing.
        y = (float)tone.process(y) * 1.70f;
        y = depthShelf.process(y);
        y = midThick.process(y);
        y = stackMakeupLow.process(y);

        // V3-A 12AX7 (REAL): recovery after the lossy tone stack into the PI/volume.
        y = coupleToneToV3a.process(y, 0.80f + 1.8f * gain + 0.8f * pushed);
        y = v3a.process(v3aMiller.process(y) * 2.0f * bplus.preamp);
        y = phaseLp.process(y);

        // VOLUME (master) into the power amp.
        y *= 0.22f + 1.28f * volume;
        y = coupleToPi.process(y, 1.0f + 0.12f * pushed);
        lastPreampLoad = 0.11f * std::fabs(y) + 0.045f * gain;
        y = phaseInverter.process(y * bplus.preamp);
        lastPowerLoad = 0.76f * std::fabs(y) + 0.20f * pushed + 0.14f * mPush + 0.12f * halfP;
        lastScreenLoad = 0.48f * std::fabs(y) + 0.10f * gain + 0.07f * halfP;

        // 2x EL34 push-pull (~50W) — real pentode table + LTP/B+ dynamics.
        y = power.process(y * bplus.power * bplus.screen);

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        float cab = speakerHp.process(y);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizz.process(cab);
        cab = speakerLp.process(cab);
        y += cabSim * (cab - y);

        // Loudness normalization (NO cleanMakeup): VOLUME (master) gives a mild swing,
        // tone-knob energy is gently compensated. ~-14 dBFS reference.
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 18.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.009f * std::fabs((depth - 0.5f) * 14.0f);
        const float level = 0.62f / ((1.0f + 0.42f * mPush + 0.28f * pushed) * toneEnergy);
        // Final OT softClip (the only tanh in the chain): the cranked OR50 squashes
        // its peaks as the power amp/iron saturate.
        const float finalDrive = 1.0f + 1.0f * pushed * pushed;
        return softClip(y * level * finalDrive) / std::tanh(finalDrive) * 0.97f;
    }
};

class Or50Plugin : public Plugin
{
    Or50Core left;
    Or50Core right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;            // anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    Or50Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kOr50Def[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "CitrusOR50"; }
    const char* getDescription() const override { return "Orange OR50 British EL34 style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('O', 'r', '5', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kOr50Names[index];
        parameter.symbol = kOr50Symbols[index];
        parameter.ranges.min = kOr50Min[index];
        parameter.ranges.max = kOr50Max[index];
        parameter.ranges.def = kOr50Def[index];
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
        osL.reset(); osR.reset();
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
            float ub[kOS];
            osL.upsample(3.2f * inL[i], ub);
            for (int k = 0; k < kOS; ++k) ub[k] = rbAmpLvl(0.560f * left.process(ub[k]));
            outL[i] = osL.downsample(ub);
            osR.upsample(3.2f * inR[i], ub);
            for (int k = 0; k < kOS; ++k) ub[k] = rbAmpLvl(0.560f * right.process(ub[k]));
            outR[i] = osR.downsample(ub);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Or50Plugin)
};

Plugin* createPlugin() { return new Or50Plugin(); }

END_NAMESPACE_DISTRHO
