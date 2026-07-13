/*
 * LOVOLT DR504 - Hiwatt DR504 "Custom Hiwatt 50" for the game's Amp_HG500.
 * Parody brand "Lovolt" (Hiwatt = high watt -> Lovolt = low volt; same brand as
 * the Lovolt 100). The in-app face must never read "Hiwatt".
 *
 * Local reference (modelled component-by-component):
 *   amps/Hiwatt DR504 (HG500)/DR504_Complete.pdf
 *
 * Full DR504 panel, 1:1 (see Dr504Params.h): a high-headroom, clean-and-loud
 * EL34 amp (3x ECC83 + ECC81 PI + 2x EL34 ~50W). Two jumperable channels —
 * NORMAL and BRILLIANT (bright cap) — sum into a shared tone stack (Bass 500K,
 * Treble 250K, Middle 100K -> the strong Hiwatt mids) -> MASTER VOLUME -> the
 * EL34 power amp. PRESENCE taps the NFB. Unlike a Plexi it stays clean far
 * longer; breakup comes mostly from cranking the MASTER.
 *
 * the game: RS Gain -> BRILLIANT VOL (breakup driver); Treble/Bass/Mid -> tone
 * stack, Pres -> Presence. See rs_knob_to_vst_param.json (input pinned BOTH).
 */
#include "DistrhoPlugin.hpp"
#include "Dr504Params.h"
#include "../../_shared/tube_stage.hpp"   // real 12AX7 stages + EL34 PP + Yeh tone stack
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

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float clampFreq(float hz, float sr) { return std::fmax(20.0f, std::fmin(hz, sr * 0.45f)); }
static inline float smoothstep(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }
static inline float smoothstepRange(float e0, float e1, float x) { return smoothstep((x - e0) / (e1 - e0)); }
static inline float softClip(float x) { return std::tanh(x); }
static inline float tonePot(float v) { v = clamp01(v); return v < 0.001f ? 0.001f : (v > 0.999f ? 0.999f : v); }

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

// Hiwatt tone stack (Marshall-style FMV with the Hiwatt values): Treble 250K,
// Bass 500K, Middle 100K (the strong Hiwatt mids), slope R 56K, C 250pF/22nF/22nF.
class HiwattToneStack
{
    float b0=1,b1=0,b2=0,b3=0,a1=0,a2=0,a3=0,x1=0,x2=0,x3=0,y1=0,y2=0,y3=0,sampleRate=48000.0f;
public:
    void reset(){ x1=x2=x3=y1=y2=y3=0.0f; }
    void setSampleRate(float sr){ sampleRate=sr>1000.0f?sr:48000.0f; }
    void update(float treble,float mid,float bass)
    {
        const float t=tonePot(treble),m=tonePot(mid),l=tonePot(bass);
        const float R1=250.0e3f, R2=500.0e3f, R3=100.0e3f, R4=56.0e3f;
        const float C1=250.0e-12f, C2=22.0e-9f, C3=22.0e-9f;
        const float ab0=0.0f;
        const float ab1=t*C1*R1 + m*C3*R3 + l*(C1*R2+C2*R2) + (C1*R3+C2*R3);
        const float ab2=t*(C1*C2*R1*R4+C1*C3*R1*R4) - m*m*(C1*C3*R3*R3+C2*C3*R3*R3)
                      + m*(C1*C3*R1*R3+C1*C3*R3*R3+C2*C3*R3*R3) + l*(C1*C2*R1*R2+C1*C2*R2*R4+C1*C3*R2*R4)
                      + l*m*(C1*C3*R2*R3+C2*C3*R2*R3) + (C1*C2*R1*R3+C1*C2*R3*R4+C1*C3*R3*R4);
        const float ab3=l*m*(C1*C2*C3*R1*R2*R3+C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4)
                      + m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4) + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                      + t*l*C1*C2*C3*R1*R2*R4;
        const float aa0=1.0f;
        const float aa1=(C1*R1+C1*R3+C2*R3+C2*R4+C3*R4) + m*C3*R3 + l*(C1*R2+C2*R2);
        const float aa2=m*(C1*C3*R1*R3-C2*C3*R3*R4+C1*C3*R3*R3+C2*C3*R3*R3) - m*m*(C1*C3*R3*R3+C2*C3*R3*R3)
                      + l*m*(C1*C3*R2*R3+C2*C3*R2*R3) + l*(C1*C2*R2*R4+C1*C2*R1*R2+C1*C3*R2*R4+C2*C3*R2*R4)
                      + (C1*C2*R1*R4+C1*C3*R1*R4+C1*C2*R3*R4+C1*C2*R1*R3+C1*C3*R3*R4+C2*C3*R3*R4);
        const float aa3=l*m*(C1*C2*C3*R1*R2*R3+C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4)
                      + m*(C1*C2*C3*R3*R3*R4+C1*C2*C3*R1*R3*R3-C1*C2*C3*R1*R3*R4) + l*(C1*C2*C3*R1*R2*R4) + C1*C2*C3*R1*R3*R4;
        const float c=2.0f*sampleRate, c2=c*c, c3=c2*c;
        const float nb0=-ab0-ab1*c-ab2*c2-ab3*c3, nb1=-3.0f*ab0-ab1*c+ab2*c2+3.0f*ab3*c3,
                    nb2=-3.0f*ab0+ab1*c+ab2*c2-3.0f*ab3*c3, nb3=-ab0+ab1*c-ab2*c2+ab3*c3;
        const float na0=-aa0-aa1*c-aa2*c2-aa3*c3, na1=-3.0f*aa0-aa1*c+aa2*c2+3.0f*aa3*c3,
                    na2=-3.0f*aa0+aa1*c+aa2*c2-3.0f*aa3*c3, na3=-aa0+aa1*c-aa2*c2+aa3*c3;
        if(std::fabs(na0)<1.0e-30f){ b0=1.0f; b1=b2=b3=a1=a2=a3=0.0f; return; }
        const float i=1.0f/na0; b0=nb0*i; b1=nb1*i; b2=nb2*i; b3=nb3*i; a1=na1*i; a2=na2*i; a3=na3*i;
    }
    float process(float x){ const float y=b0*x+b1*x1+b2*x2+b3*x3-a1*y1-a2*y2-a3*y3;
        x3=x2; x2=x1; x1=x; y3=y2; y2=y1; y1=y; return y; }
};

class DcBlock
{
    float x1=0.0f,y1=0.0f;
public:
    void reset(){ x1=y1=0.0f; }
    float process(float x){ const float y=x-x1+0.995f*y1; x1=x; y1=y; return y; }
};

} // namespace

class Dr504Core
{
    float sampleRate = 48000.0f;
    float normalVol = kDr504Def[kNormalVol];
    float brightVol = kDr504Def[kBrightVol];
    float bass   = kDr504Def[kBass];
    float treble = kDr504Def[kTreble];
    float mid    = kDr504Def[kMiddle];
    float pres   = kDr504Def[kPresence];
    float master = kDr504Def[kMaster];
    float input  = kDr504Def[kInput];
    float cabSim = kDr504Def[kCabSim];

    // derived
    float brightG = 1.0f, normalG = 1.0f;
    float preDrive = 0.5f;

    Biquad inputHp, pickupLoad, brightCapShelf, brightBody, normalBody;
    Biquad interstageHp, cathodeLp;
    rbtube::ToneStackYeh toneStack;     // real Hiwatt TMB (double — stable at 192k)
    Biquad stackMakeupLow, stackMakeupBody, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    // ── real circuit (Koren tubes) replacing the tanh asymTube ──
    rbtube::TubeStage    vBright, vNormal, vRecovery;   // 12AX7 stages
    rbtube::Miller12AX7  brightMiller, normalMiller, recoveryMiller;
    rbtube::CouplingCapGridLeak coupleToPi;              // master -> PI grid
    rbtube::PhaseInverterLTP12AT7 phaseInverter;         // real ECC81/12AT7 long-tail pair
    rbtube::MultiNodeBPlus supply;                       // diode rectifier + stiff Hiwatt B+ nodes
    rbtube::PowerAmpEL34 power;                          // 2x EL34 (~50W)
    float inScale = 1.15f, toneMk = 13.0f;
    float sag = 0.0f;
    float lastPowerLoad = 0.0f;
    float lastScreenLoad = 0.0f;
    float lastPreampLoad = 0.0f;

    static float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }

    void setupTubes()
    {
        vBright.set(sampleRate, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
        vNormal.set(sampleRate, 1, 250.0f, 40.0f, 22.0f, 1500.0f);
        vRecovery.set(sampleRate, 1, 250.0f, 40.0f, 40.0f, 1500.0f);
        brightMiller.set(sampleRate, 68000.0f, 55.0f, 8.0f);
        normalMiller.set(sampleRate, 68000.0f, 55.0f, 8.0f);
        recoveryMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
    }

    void updateFilters()
    {
        // Input cable: Normal(<0.25) / Both(jumpered, 0.25-0.75) / Brilliant(>=0.75).
        normalG = (input < 0.75f) ? 1.0f : 0.0f;
        brightG = (input >= 0.25f) ? 1.0f : 0.0f;
        // Preamp drive (the volumes); the Hiwatt preamp stays clean -> a gentle proxy.
        preDrive = clamp01(brightG * brightVol + normalG * normalVol * 0.80f);
        const float g = smoothstep(preDrive);
        const float pushed = smoothstepRange(0.28f, 0.88f, preDrive);
        const float bright = clamp01(0.32f * treble + 0.20f * pres + 0.45f * (1.0f - brightVol));

        inputHp.setHighPass(sampleRate, 40.0f + 30.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 17500.0f - 1200.0f * pushed + 800.0f * treble, 0.64f);
        brightCapShelf.setHighShelf(sampleRate, 1500.0f + 1100.0f * treble, 0.70f, -0.8f + 5.2f * bright + 1.6f * pres);
        brightBody.setPeaking(sampleRate, 700.0f + 250.0f * mid, 0.60f, -0.1f + 0.9f * mid);   // gentler (was -0.4..+1.8, boxy)
        normalBody.setPeaking(sampleRate, 120.0f + 40.0f * bass, 0.70f, 3.0f + 2.2f * bass - 0.8f * pushed);   // Normal channel = fuller lows than Brilliant

        interstageHp.setHighPass(sampleRate, 52.0f + 50.0f * pushed, 0.70f);
        cathodeLp.setLowPass(sampleRate, 14000.0f + 1500.0f * treble - 1100.0f * pushed, 0.64f);
        // Hiwatt tone stack — CIRCUIT-REAL (Yeh, real R/C from the hwpre1 schematic):
        // Treble 250k/250pF, Bass 500k/22nF, Mid 100k/22nF (the big Hiwatt mid), slope 56k.
        toneStack.setComponents(250e3, 500e3, 47e3, 56e3, 250e-12, 22e-9, 22e-9);   // Mid pot 100k->47k: tame the over-wide/boxy Middle
        toneStack.update(sampleRate, treble, 0.28f + 0.46f * mid, bass);            // compress the Middle knob range (was full 0..1 = filters/boosts too much)
        stackMakeupLow.setLowShelf(sampleRate, 120.0f + 30.0f * bass, 0.72f, eqDb(bass, 1.5f));
        // the Hiwatt mids — a GENTLE upper-mid push (was -0.6..+4.0 dB = the boxy
        // "en lata" resonant ~700 Hz peak the user heard; now ±~1.5 dB, wider Q).
        stackMakeupBody.setPeaking(sampleRate, 640.0f + 160.0f * mid, 0.50f, -0.3f + 1.8f * mid);
        phaseLp.setLowPass(sampleRate, 14000.0f + 1500.0f * treble + 900.0f * pres - 2000.0f * pushed, 0.64f);
        presenceShelf.setHighShelf(sampleRate, 2600.0f + 900.0f * pres, 0.78f, -3.6f + 8.4f * pres + 0.9f * treble);

        // Hiwatt 4x12 (Fane) voicing — TIGHT lows + BRIGHT/present top (the Hiwatt family is the
        // tightest + brightest; the DR103 reference confirms it). Was too dark/full.
        speakerHp.setHighPass(sampleRate, 100.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 120.0f, 0.84f, -1.5f + 1.6f * bass);
        speakerLowMid.setPeaking(sampleRate, 420.0f + 90.0f * mid, 0.74f, 1.0f + 2.2f * mid);
        // De-screeched top end (pass6) — shares the DR103 cab voicing. The old
        // +2 dB bite peak + +4.5 dB (up to +6.5) high shelf put the post-power-
        // amp EQ ~+6 dB across 4–8 kHz: fine on leads, icy/screechy on distorted
        // tones whose high harmonics land in that shelf. Trim toward ~+3.5 dB:
        // bite base 2.0->1.2, fizz base 4.5->2.2 (corner 3800->4000 Hz), and roll
        // the extreme-highs LP down slightly so it's air, not hiss.
        speakerBite.setPeaking(sampleRate, 2400.0f + 500.0f * treble, 0.78f, 1.2f + 1.4f * treble + 0.8f * pres - 0.5f * pushed);
        speakerFizz.setHighShelf(sampleRate, 4000.0f, 0.70f, 2.2f + 1.4f * treble + 1.1f * pres - 1.0f * pushed);
        speakerLp.setLowPass(sampleRate, 14500.0f + 1500.0f * treble + 700.0f * pres - 1500.0f * pushed, 0.66f);

        // 2x EL34 (~50W) — slightly less headroom than the DR103 (a touch more sag/
        // drive) but still the high-headroom Hiwatt; breaks up when the Master is up.
        const float mPush = smoothstep(master);
        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 100000.0f,
                       0.16f, 0.28f, 0.75f);
        // Hiwatt uses an ECC81/12AT7 LTP with 47k-ish plate loads and minimal imbalance.
        phaseInverter.setComponents(sampleRate, 0.62f + 1.05f * mPush + 0.35f * pushed, 0.78f,
                                    310.0f, 47000.0f, 47000.0f, 10000.0f, 18.0f, 0.020f);
        supply.set(sampleRate,
                   22.0f, 100.0f,
                   1000.0f, 50.0f,
                   10000.0f, 32.0f,
                   0.07f + 0.03f * pushed,
                   0.055f + 0.025f * pushed,
                   0.035f + 0.015f * preDrive,
                   0.14f);
        // REAL Koren EL34 power amp, driven GENTLY so it stays clean and only breaks up cranked.
        // DR504 = 2x EL34, schematic bias −36V; a touch more drive than the DR103 (50W clips earlier).
        power.set(sampleRate, 0.55f + 3.4f * mPush + 2.3f * pushed, -36.0f, 0.07f, 50.0f, 12000.0f);
        power.out = 0.011f;
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); brightCapShelf.reset(); brightBody.reset(); normalBody.reset();
        interstageHp.reset(); cathodeLp.reset();
        toneStack.reset(); stackMakeupLow.reset(); stackMakeupBody.reset(); phaseLp.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset(); speakerFizz.reset(); speakerLp.reset();
        dcBlock.reset(); sag = 0.0f;
        brightMiller.reset(); normalMiller.reset(); recoveryMiller.reset();
        toneStack.reset(); vBright.reset(); vNormal.reset(); vRecovery.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
        setupTubes();
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kNormalVol: normalVol = v; break;
            case kBrightVol: brightVol = v; break;
            case kBass:      bass = v; break;
            case kTreble:    treble = v; break;
            case kMiddle:    mid = v; break;
            case kPresence:  pres = v; break;
            case kMaster:    master = v; break;
            case kInput:     input = v; break;
            case kCabSim:    cabSim = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kDr504Def[i]); }

    float process(float in)
    {
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        const float pushed = smoothstepRange(0.28f, 0.88f, preDrive);
        const float mPush = smoothstep(master);
        // roar = the CRANKED top of the sweep. The clean-headroom fixes (pass3-5)
        // overshot and flattened the whole gain sweep — maxed volumes no longer
        // distorted at all (a cranked DR504 genuinely roars: that's the sound).
        // Extra drive ramps in ONLY past ~55%, low/normal keep the clean Hiwatt.
        const float roar = smoothstepRange(0.45f, 1.0f, preDrive);

        float x = inputHp.process(in * 1.8f);  // pass5: global input boost cut hard (4.5->1.8) — tube grids saw too many volts, still distorting
        x = pickupLoad.process(x);
        x *= inScale;

        // BRILLIANT channel (bright cap + body) + NORMAL channel — real ECC83 V1A/V1B.
        // Gentle drive: the Hiwatt has huge headroom, so it stays clean until cranked.
        float bch = brightCapShelf.process(brightBody.process(x));
        bch = vBright.process(brightMiller.process(bch) *
                              (0.35f + 3.3f * brightVol
                               + 7.5f * smoothstepRange(0.45f, 1.0f, brightVol)) * bplus.preamp);   // pass3 + roar (was 0.6+14.0 orig)
        float nch = normalBody.process(x);
        nch = vNormal.process(normalMiller.process(nch) *
                              (0.3f + 2.3f * normalVol
                               + 4.5f * smoothstepRange(0.45f, 1.0f, normalVol)) * bplus.preamp);   // pass3 + roar (was 0.5+9.0 orig)

        // jumpered mix
        float y = brightG * (0.34f + 0.66f * brightVol) * bch
                + normalG * (0.30f + 0.62f * normalVol) * nch;

        // recovery (ECC83) into the tone stack
        y = interstageHp.process(y);
        y = vRecovery.process(recoveryMiller.process(y) *
                              (0.5f + 2.8f * preDrive + 5.0f * roar) * bplus.preamp);   // pass4 + roar: back toward the original 7.7x, only when cranked
        y = cathodeLp.process(y);

        y = toneStack.process(y) * toneMk;
        y = stackMakeupLow.process(y);
        y = stackMakeupBody.process(y);
        y = phaseLp.process(y);

        // MASTER VOLUME into the power amp (breakup driver; keeps the 50W EL34 clean at low/normal).
        y *= 0.45f + 1.05f * master;
        lastPreampLoad = 0.08f * std::fabs(y) + 0.03f * preDrive;
        // CLEAN LINEAR phase-inverter — the Koren LTP was THE GATE (cut small signals to −240 dBFS
        // silence at low drive; see the DR103 fix). Linear here = no gating; the real Koren EL34 power
        // amp below keeps the authentic breakup. Modest gain so the 50W EL34 stays clean until cranked.
        y = y * bplus.screen * (0.42f + 0.30f * roar * mPush);
        lastPowerLoad = 0.65f * std::fabs(y) + 0.12f * pushed;
        lastScreenLoad = 0.42f * std::fabs(y) + 0.06f * preDrive;

        // 2x EL34 (~50W) — REAL pentode table + OT. The stiff Hiwatt supply is
        // injected through the B+ scales above.
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

        // Loudness normalization: the channel volumes are the gain (no gain knob),
        // so cleanMakeup keeps the RS Gain (-> Brilliant Vol) sweep ~flat; MASTER
        // is the volume -> a mild swing in the denominator. (~-14 dBFS reference.)
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.010f * std::fabs((pres - 0.5f) * 16.0f);
        const float cleanMakeup = 1.0f + 1.0f * std::exp(-preDrive / 0.30f);
        const float level = (0.28f + 0.09f * (1.0f - preDrive)) * cleanMakeup /
            ((1.0f + 0.40f * mPush + 0.20f * pushed + 1.05f * roar) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class Dr504Plugin : public Plugin
{
    Dr504Core left;
    Dr504Core right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    Dr504Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kDr504Def[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "LovoltDR504"; }
    const char* getDescription() const override { return "Hiwatt DR504 Custom 50 style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,0,1); }
    int64_t getUniqueId() const override { return d_cconst('L', 'v', '5', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDr504Names[index];
        parameter.symbol = kDr504Symbols[index];
        parameter.ranges.min = kDr504Min[index];
        parameter.ranges.max = kDr504Max[index];
        parameter.ranges.def = kDr504Def[index];
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
            osL.upsample(inL[i], ubL);
            osR.upsample(inR[i], ubR);
            for (int k = 0; k < kOS; ++k)
            {
                ubL[k] = rbAmpLvl(0.888f * left.process(ubL[k]));
                ubR[k] = rbAmpLvl(0.888f * right.process(ubR[k]));
            }
            outL[i] = osL.downsample(ubL);
            outR[i] = osR.downsample(ubR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dr504Plugin)
};

Plugin* createPlugin() { return new Dr504Plugin(); }

END_NAMESPACE_DISTRHO
