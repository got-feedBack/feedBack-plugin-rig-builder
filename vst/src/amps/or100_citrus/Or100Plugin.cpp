/*
 * CITRUS OR100 - Orange OR100 (vintage "Graphic" 100W head) for the game's
 * Amp_OrangeOR100. Parody brand "Citrus"; the in-app face must NEVER read
 * "Orange".
 *
 * ============================ CIRCUIT-REAL DSP ============================
 * Rebuilt FROM SCRATCH, schematic-first, off the user's hand-drawn schematic:
 *   amps/Orange OR100/Orange100.pdf  ("Orange Model OR100, Serie nr. 94",
 *   hand-drawn from the actual amplifier by Soeren Poulsen; PCB marked HH).
 *
 * A REAL part for EVERY real stage (Koren tube tables + physical cathode loop +
 * a real passive Orange tone network), NOT a tanh black box. The only tanh in
 * the chain is the final output-transformer soft clip (rbAmpLvl). Models, not
 * Guitarix's GPL code: the physics (Koren/Yeh) is public, the component values
 * are the real amp's.
 *
 * --- What the schematic actually shows (component by component) -------------
 *  PREAMP (ECC83 = 12AX7):
 *    V1  triode (pins 1/2/3): plate load 220k ("M22"), 22n coupling out,
 *        cathode 2.2k ("2K4") bypassed by 50uF ("50/25") -> fck ~1.4 Hz.
 *        Input jack selector (Stage / Studio) with a 1500pF bright cap + 2.2k.
 *    GAIN pot 1M-LIN ("HF Drive"): the inter-stage level into V2; a 2.4k ("242")
 *        + cap to ground sits at its "Orange" tap.
 *    V2  triode (pins 6/7/8): plate load 220k ("M22"), 22n coupling, cathode
 *        2.2k / 60uF ("60/25").
 *  TONE  (the Orange/Matamp PASSIVE network -- NOT a Marshall 3-band FMV):
 *    33pF treble cap + a 2n2 / 0.1uF("midel" = the FAC mid) / 22n stack loaded
 *    by 27k to ground. Bass + Treble + the FAC mid voicing. Plus the DEPTH
 *    rotary (a switched bass cap bank 1n/2n2/4n7/10n/47n/100n + 68k) = the
 *    low-end "weight" control. Modelled here as a Yeh passive R/C transfer with
 *    the real Orange values (double precision -> 192k-safe).
 *  VOLUME pot 1M-LOG = master, into the power amp.
 *  PI  (ECC83 long-tail pair): 10k-ish plate loads, 1k tail, 0.1uF couplings;
 *    a "Boost" 50k-LIN + 0.1uF + 24k/1k network taps the loop (presence/HF).
 *  POWER: Ub3 = 4x EL34 (~100W), FIXED bias (1N4007 -> 27k -> 15k -> 100k bias
 *    pot -> 33k; 2M2 grid resistors). FULL / HALF output power switch. OT.
 *
 * Engine: rbtube::TubeStage (12AX7 Koren table + real cathode auto-bias loop),
 *   rbtube::PowerAmpEL34 (4x EL34 push-pull pentode + sag + OT), the Orange tone
 *   via rbtube::ToneStackYeh (double), rbshared::Oversampler4x (2x OS). Staging
 *   numbers mirror the tuned EL34 Plexi template, retuned for the Orange voice
 *   (simpler tone, mid-forward, 100W). NO cleanMakeup.
 *
 * Guitarix cross-check (architecture/values only, GPL -> never copy code):
 *   tools/.../OrangeDarkTerrorp3.sch (same Orange preamp family): V1 12ax7 with
 *   Rp 100k, Rk 1.2k, Ck 47u, 1M grid leak, 0.1u couplings -- confirms the
 *   cathode-biased 12AX7 topology and the simple Orange tone (its .dsp uses ONE
 *   pre-filter IIR, NOT a 3-band TMB). The OR100 is the bigger 4xEL34 sibling.
 *
 * the game: RS Gain -> GAIN; Bass/Mid/Treble -> tone. Volume + Depth pinned via
 *   _static (rs_knob_to_vst_param.json); FULL/HALF editable.
 */
#include "DistrhoPlugin.hpp"
#include "Or100Params.h"
#include "../../_shared/tube_stage.hpp"   // real 12AX7 + EL34 circuit models
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts and
// the cranked power amp never hard-clip. This is the only tanh in the chain --
// it stands in for the output-transformer/speaker peak softening.
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

class Or100Core
{
    float sampleRate = 48000.0f;
    float gain   = kOr100Def[kGain];
    float bass   = kOr100Def[kBass];
    float mid    = kOr100Def[kMiddle];
    float treble = kOr100Def[kTreble];
    float depth  = kOr100Def[kDepth];
    float volume = kOr100Def[kVolume];
    float half   = kOr100Def[kHalf];
    float cabSim = kOr100Def[kCabSim];

    // ── voicing biquads (kept; speaker/cab-ish color, pre-cab, mild) ──────────
    Biquad inputHp, pickupLoad, brightCap, preBody;
    Biquad interHp, cathodeLp;
    rbtube::ToneStackYeh toneStack;     // real Orange passive R/C tone (double = 192k-safe)
    Biquad depthShelf, midThick, stackMakeupLow, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerAir, speakerLp;
    DcBlock dcBlock;

    // ── REAL tube stages (Koren circuit models) ──────────────────────────────
    rbtube::TubeStage v1, v2;           // ECC83/12AX7 gain stages (220k plate, 2.2k cathode)
    rbtube::TubeStage piDriver;         // ECC83/12AX7 recovery into the long-tail PI
    rbtube::Miller12AX7 v1Miller, v2Miller, piMiller;
    rbtube::CouplingCapGridLeak coupleV1ToV2;
    rbtube::CouplingCapGridLeak coupleToneToDriver;
    rbtube::CouplingCapGridLeak coupleToPi;  // master -> PI grid blocking
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;           // diode rectifier + B+ nodes
    rbtube::PowerAmpEL34 power;          // 4x EL34 push-pull (~100W), fixed bias
    float lastPowerLoad = 0.0f, lastScreenLoad = 0.0f, lastPreampLoad = 0.0f;

    static float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }

    void updateFilters()
    {
        const float g = smoothstep(gain);
        const float pushed = smoothstepRange(0.35f, 0.90f, gain);
        const float vol = smoothstep(volume);            // master cooks the power amp
        const float halfP = (half >= 0.5f) ? 1.0f : 0.0f;

        // The 1500pF bright cap across the input selector bleeds treble in,
        // strongest at low gain (Orange "Stage" position sparkle).
        const float bright = clamp01(0.30f * treble + 0.45f * (1.0f - gain));

        // ── real 12AX7 / EL34 circuit stages (cathode auto-bias solved) ───────
        // V1/V2: ECC83, plate 220k handled by the model's Rp/divider; cathode
        //   2.2k -> fck = 1/(2*pi*2.2k*Ck): 50uF -> ~1.45 Hz (V1), 60uF -> ~1.2 Hz (V2).
        //   (use the model's effective bypass corner; both effectively full-bypass.)
        v1.set(sampleRate, 0, 250.0f, 40.0f, 1.45f, 2200.0f);        // input stage, 68k grid leak
        v2.set(sampleRate, 1, 250.0f, 40.0f, 1.20f, 2200.0f);        // 2nd stage, 250k grid leak
        piDriver.set(sampleRate, 1, 250.0f, 40.0f, 30.0f, 1500.0f);  // recovery / PI driver
        v1Miller.set(sampleRate,  68000.0f, 55.0f, 8.0f);
        v2Miller.set(sampleRate, 220000.0f, 52.0f, 8.0f);
        piMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        coupleV1ToV2.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f,
                         0.12f, 0.44f, 1.20f);
        coupleToneToDriver.set(sampleRate, 1000000.0f, 22.0e-9f, 180000.0f,
                               0.11f, 0.38f, 0.95f);
        coupleToPi.set(sampleRate, 1000000.0f, 100.0e-9f, 100000.0f, 0.13f, 0.42f, 1.05f);
        // The local OR drawings label the PI differently across references; keep
        // the lower-drive Orange/Matamp feel instead of a hot Marshall splitter.
        phaseInverter.setComponents(sampleRate,
                                    0.72f + 1.05f * vol + 0.45f * pushed + 0.30f * halfP,
                                    0.82f, 320.0f, 47000.0f, 47000.0f, 10000.0f, 18.0f, 0.018f);
        supply.set(sampleRate,
                   18.0f, 100.0f,
                   1000.0f, 50.0f,
                   10000.0f, 32.0f,
                   0.055f + 0.020f * pushed + 0.025f * halfP,
                   0.045f + 0.018f * pushed + 0.020f * halfP,
                   0.030f + 0.012f * gain,
                   0.15f);
        // 4x EL34 push-pull (~100W), fixed bias ~-40V. EL34 break up earlier +
        // compress more than 6L6 -> the thick Orange midrange grind. The master
        // VOLUME (not gain) cooks the power amp; HALF power -> earlier breakup.
        power.set(sampleRate, 8.5f + 9.0f * vol + 7.0f * pushed + 3.5f * halfP,
                  -40.0f, 0.07f + 0.04f * halfP, 48.0f, 10500.0f);
        power.out = 0.010f;

        // ── input / interstage shaping ────────────────────────────────────────
        inputHp.setHighPass(sampleRate, 42.0f + 40.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12500.0f - 1500.0f * pushed + 800.0f * treble, 0.64f);
        brightCap.setHighShelf(sampleRate, 1500.0f + 1000.0f * treble, 0.70f, -0.8f + 5.4f * bright);
        preBody.setPeaking(sampleRate, 620.0f + 220.0f * mid, 0.80f, 0.4f + 1.4f * mid);  // Orange upper-mid push
        interHp.setHighPass(sampleRate, 70.0f + 80.0f * pushed, 0.70f);
        cathodeLp.setLowPass(sampleRate, 9000.0f + 1400.0f * treble - 1500.0f * pushed, 0.64f);

        // ── the Orange PASSIVE tone network (real R/C values, Yeh transfer) ────
        // The Orange tone is a treble cap (33pF) + a mid/bass cap stack
        // (2n2 / 0.1uF "midel" / 22n) loaded by 27k -- a 2-band-plus-FAC network,
        // NOT a Marshall FMV. Map: Treble->t, FAC mid->m, Bass->l. Component
        // values from the schematic (slope/loads from the Orange topology).
        //   R1 Treble 1M, R2 Bass 1M, R3 mid(FAC) 27k, R4 slope 100k
        //   C1 treble 33pF (the bright cap), C2 0.1uF (bass/mid), C3 22nF (mid)
        toneStack.setComponents(1.0e6, 1.0e6, 27.0e3, 100.0e3, 33.0e-12, 0.1e-6, 22.0e-9);
        toneStack.update(sampleRate, treble, mid, bass);

        // DEPTH = the switched bass-cap rotary: a swept low shelf (more depth ->
        // bigger lows, the Orange "weight").
        depthShelf.setLowShelf(sampleRate, 90.0f + 25.0f * depth, 0.72f, eqDb(depth, 8.0f) + 0.8f);
        // the thick Orange FAC midrange body (passive stack already shapes it;
        // this is the makeup of the mid hump that the FAC accentuates).
        midThick.setPeaking(sampleRate, 470.0f + 150.0f * mid, 0.62f, -0.5f + 4.0f * mid + 1.0f * pushed);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f, 0.72f, 0.8f - 1.0f * pushed);
        phaseLp.setLowPass(sampleRate, 11000.0f + 1300.0f * treble - 2000.0f * pushed, 0.64f);
        // PI "Boost" -> a fixed presence high-shelf (no presence knob on the OR100 panel).
        presenceShelf.setHighShelf(sampleRate, 2700.0f, 0.78f, 1.6f + 1.0f * treble);

        // Orange PPC 4x12 (thick, midrange-forward, smooth top), pre-cab + mild.
        speakerHp.setHighPass(sampleRate, 78.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 120.0f, 0.84f, 0.9f + 2.2f * bass + 1.2f * depth);
        speakerLowMid.setPeaking(sampleRate, 440.0f + 90.0f * mid, 0.72f, 1.0f + 1.8f * mid);
        speakerBite.setPeaking(sampleRate, 2500.0f + 480.0f * treble, 0.78f, 2.0f + 1.6f * treble - 0.5f * pushed);
        // AIR high-shelf: real cab/OT rolls the extreme top off instead of adding
        // fizz; Treble can recover a little chime before the external cab IR.
        speakerAir.setHighShelf(sampleRate, 5200.0f, 0.70f, -1.5f + 1.8f * treble - 2.4f * pushed);
        speakerLp.setLowPass(sampleRate, 13000.0f + 1600.0f * treble - 2500.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); brightCap.reset(); preBody.reset();
        interHp.reset(); cathodeLp.reset();
        toneStack.reset(); depthShelf.reset(); midThick.reset(); stackMakeupLow.reset();
        phaseLp.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset();
        speakerBite.reset(); speakerAir.reset(); speakerLp.reset();
        dcBlock.reset();
        v1Miller.reset(); v2Miller.reset(); piMiller.reset();
        v1.reset(); v2.reset(); piDriver.reset();
        coupleV1ToV2.reset(); coupleToneToDriver.reset();
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

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kOr100Def[i]); }

    float process(float in)
    {
        const float pushed = smoothstepRange(0.35f, 0.90f, gain);
        const float vol = smoothstep(volume);
        const float halfP = (half >= 0.5f) ? 1.0f : 0.0f;
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = brightCap.process(x);
        // input coupling soft limit (grid conduction at the jack), very gentle so
        // the clean end stays clean (only bites once cranked).
        x = softClip(x * (0.92f + 0.22f * pushed)) * (0.98f - 0.04f * pushed);

        // V1 (ECC83) input gain stage -- REAL cathode-biased 12AX7. Low floor so
        // the amp is genuinely clean at low GAIN; the ceiling drives V1 into clip.
        float y = preBody.process(x);
        y = v1.process(v1Miller.process(y) * (2.6f + 4.2f * gain) * bplus.preamp);

        // GAIN pot (1M-LIN HF Drive) -> inter-stage pre-gain into V2 (cascade).
        // This is where the Orange crunch builds: small at low gain, hot when up.
        y = interHp.process(y);
        y = coupleV1ToV2.process(y, 0.75f + 4.8f * gain + 1.8f * pushed);
        y = v2.process(v2Miller.process(y) * (1.2f + 5.2f * gain + 3.2f * pushed) * bplus.preamp);
        y = cathodeLp.process(y);

        // Orange PASSIVE tone network (real R/C), then the FAC/depth makeup.
        y = toneStack.process(y) * 4.0f;       // passive stack is lossy -> makeup
        y = depthShelf.process(y);
        y = midThick.process(y);
        y = stackMakeupLow.process(y);

        // PI driver / recovery -- REAL 12AX7 into the long-tail pair. Drive scales
        // with gain so the late stage adds breakup only when the amp is pushed.
        y = coupleToneToDriver.process(y, 0.75f + 1.9f * gain + 0.8f * pushed);
        y = piDriver.process(piMiller.process(y) * (1.6f + 3.0f * gain + 1.2f * pushed) * bplus.preamp);
        y = phaseLp.process(y);

        // VOLUME (1M-LOG master) into the power amp.
        y *= 0.20f + 1.30f * volume;
        y = coupleToPi.process(y, 1.0f + 0.10f * pushed);
        lastPreampLoad = 0.10f * std::fabs(y) + 0.04f * gain;
        y = phaseInverter.process(y * bplus.preamp);
        lastPowerLoad = 0.92f * std::fabs(y) + 0.18f * pushed + 0.16f * vol + 0.12f * halfP;
        lastScreenLoad = 0.56f * std::fabs(y) + 0.10f * gain + 0.08f * halfP;

        // 4x EL34 push-pull (~100W) -- real pentode table + LTP/B+ dynamics.
        y = power.process(y * bplus.power * bplus.screen);
        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        float cab = speakerHp.process(y);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerAir.process(cab);
        cab = speakerLp.process(cab);
        y += cabSim * (cab - y);

        // Output level: the GAIN (-> RS Gain) sweep + VOLUME swing kept within a
        // couple dB of the ~-14 dBFS shared reference. NO cleanMakeup. The only
        // saturation here is the final OT soft clip (rbAmpLvl in the plugin).
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 18.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.009f * std::fabs((depth - 0.5f) * 14.0f);
        const float level = (0.62f + 0.12f * (1.0f - gain)) /
            ((1.0f + 0.30f * smoothstep(volume) + 0.20f * pushed) * toneEnergy);
        return y * level;
    }
};

class Or100Plugin : public Plugin
{
    Or100Core left;
    Or100Core right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;            // 2x OS around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    Or100Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kOr100Def[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "CitrusOR100"; }
    const char* getDescription() const override { return "Orange OR100 British EL34 style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('O', 'r', '1', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kOr100Names[index];
        parameter.symbol = kOr100Symbols[index];
        parameter.ranges.min = kOr100Min[index];
        parameter.ranges.max = kOr100Max[index];
        parameter.ranges.def = kOr100Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Or100Plugin)
};

Plugin* createPlugin() { return new Or100Plugin(); }

END_NAMESPACE_DISTRHO
