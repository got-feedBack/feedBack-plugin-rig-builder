/*
 * MARSTEN JCM800 - Marshall JCM800 2204 master-volume head for the game's
 * Amp_MarshallJCM800. Parody brand "Marsten"; the in-app face must never read
 * "Marshall". Reference: official 2204 preamp/power schematics.
 *
 * Cascaded 12AX7 preamp (GAIN pot between the two stages, the JCM800 drive) ->
 * Marshall TMB tone stack -> cathode follower -> MASTER volume -> 12AX7 long-tail
 * PI -> 2x EL34 (~50W) with presence NFB, stiff diode supply, reactive OT and a
 * bypassable fallback 4x12 Cab Sim.
 *
 * RS: Gain -> GAIN, Bass/Mid/Treble -> tone stack, Pres -> Presence. Master +
 * HI/LO pinned via _static.
 */
#include "DistrhoPlugin.hpp"
#include "Jcm800Params.h"
#include "../../_shared/tube_stage.hpp"   // real 12AX7 + EL34 circuit models
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

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

// Marshall JCM800 TMB tone stack (Treble 220K, Bass 1M, Middle 25K, slope 33K,
// C 470pF / 22nF / 22nF). 3rd-order bilinear; double-precision recursion to keep
// the near-unit poles stable at tone extremes.
class MarshallToneStack
{
    double b0=1,b1=0,b2=0,b3=0,a1=0,a2=0,a3=0,x1=0,x2=0,x3=0,y1=0,y2=0,y3=0; float sampleRate=48000.0f;
public:
    void reset(){ x1=x2=x3=y1=y2=y3=0.0; }
    void setSampleRate(float sr){ sampleRate=sr>1000.0f?sr:48000.0f; }
    void update(float treble,float mid,float bass)
    {
        const double t=tonePot(treble),m=tonePot(mid),l=tonePot(bass);
        const double R1=220.0e3, R2=1.0e6, R3=22.0e3, R4=33.0e3;
        const double C1=470.0e-12, C2=22.0e-9, C3=22.0e-9;
        const double ab1=t*C1*R1 + m*C3*R3 + l*(C1*R2+C2*R2) + (C1*R3+C2*R3);
        const double ab2=t*(C1*C2*R1*R4+C1*C3*R1*R4) - m*m*(C1*C3*R3*R3+C2*C3*R3*R3)
                      + m*(C1*C3*R1*R3+C1*C3*R3*R3+C2*C3*R3*R3) + l*(C1*C2*R1*R2+C1*C2*R2*R4+C1*C3*R2*R4)
                      + l*m*(C1*C3*R2*R3+C2*C3*R2*R3) + (C1*C2*R1*R3+C1*C2*R3*R4+C1*C3*R3*R4);
        const double ab3=l*m*(C1*C2*C3*R1*R2*R3+C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4)
                      + m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4) + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                      + t*l*C1*C2*C3*R1*R2*R4;
        const double aa0=1.0;
        const double aa1=(C1*R1+C1*R3+C2*R3+C2*R4+C3*R4) + m*C3*R3 + l*(C1*R2+C2*R2);
        const double aa2=m*(C1*C3*R1*R3-C2*C3*R3*R4+C1*C3*R3*R3+C2*C3*R3*R3) - m*m*(C1*C3*R3*R3+C2*C3*R3*R3)
                      + l*m*(C1*C3*R2*R3+C2*C3*R2*R3) + l*(C1*C2*R2*R4+C1*C2*R1*R2+C1*C3*R2*R4+C2*C3*R2*R4)
                      + (C1*C2*R1*R4+C1*C3*R1*R4+C1*C2*R3*R4+C1*C2*R1*R3+C1*C3*R3*R4+C2*C3*R3*R4);
        const double aa3=l*m*(C1*C2*C3*R1*R2*R3+C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4)
                      + m*(C1*C2*C3*R3*R3*R4+C1*C2*C3*R1*R3*R3-C1*C2*C3*R1*R3*R4) + l*(C1*C2*C3*R1*R2*R4) + C1*C2*C3*R1*R3*R4;
        const double c=2.0*sampleRate, c2=c*c, c3=c2*c;
        const double nb0=-ab1*c-ab2*c2-ab3*c3, nb1=-ab1*c+ab2*c2+3.0*ab3*c3,
                     nb2= ab1*c+ab2*c2-3.0*ab3*c3, nb3= ab1*c-ab2*c2+ab3*c3;
        const double na0=-aa0-aa1*c-aa2*c2-aa3*c3, na1=-3.0*aa0-aa1*c+aa2*c2+3.0*aa3*c3,
                     na2=-3.0*aa0+aa1*c+aa2*c2-3.0*aa3*c3, na3=-aa0+aa1*c-aa2*c2+aa3*c3;
        if(std::fabs(na0)<1.0e-30){ b0=1.0; b1=b2=b3=a1=a2=a3=0.0; return; }
        const double i=1.0/na0; b0=nb0*i; b1=nb1*i; b2=nb2*i; b3=nb3*i; a1=na1*i; a2=na2*i; a3=na3*i;
    }
    float process(float xin){ const double x=xin; const double y=b0*x+b1*x1+b2*x2+b3*x3-a1*y1-a2*y2-a3*y3;
        x3=x2; x2=x1; x1=x; y3=y2; y2=y1; y1=y; return (float)y; }
};

class DcBlock
{
    float x1=0.0f,y1=0.0f;
public:
    void reset(){ x1=y1=0.0f; }
    float process(float x){ const float y=x-x1+0.995f*y1; x1=x; y1=y; return y; }
};

} // namespace

class Jcm800Core
{
    float sampleRate = 48000.0f;
    float gain = kJcm800Def[kGain];
    float bass = kJcm800Def[kBass];
    float mid  = kJcm800Def[kMiddle];
    float treble = kJcm800Def[kTreble];
    float presence = kJcm800Def[kPresence];
    float volume = kJcm800Def[kVolume];
    float cabSim = kJcm800Def[kCabSim];

    Biquad inputHp, pickupLoad, brightCap, interHp, cathodeLp;
    MarshallToneStack toneStack;
    Biquad stackMakeupLow, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;

    // ── REAL tube stages (Koren circuit models) replacing the tanh asymTube ──
    rbtube::TubeStage v1a, v1b, v2;     // 3x 12AX7 cascade (V1a -> GAIN -> V1b -> V2)
    rbtube::Miller12AX7 v1aMiller, v1bMiller, v2Miller;
    rbtube::CouplingCapGridLeak coupleGainToV1b, coupleV1bToV2, coupleToPi;
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;
    rbtube::PowerAmpEL34 power;          // 2x EL34 push-pull (~50W)
    float lastPowerLoad = 0.0f, lastScreenLoad = 0.0f, lastPreampLoad = 0.0f;

    void updateFilters()
    {
        const float g = smoothstep(gain);
        const float pushed = smoothstepRange(0.40f, 0.92f, gain);
        const float mPush = smoothstep(volume);

        // ── real 12AX7 cascade + EL34 (cathode-biased, self-bias solved) ──
        v1a.set(sampleRate, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v1b.set(sampleRate, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v2.set(sampleRate, 1, 250.0f, 40.0f, 55.0f, 1500.0f);
        v1aMiller.set(sampleRate,  68000.0f, 55.0f, 8.0f);
        v1bMiller.set(sampleRate, 220000.0f, 52.0f, 8.0f);
        v2Miller.set(sampleRate,  180000.0f, 52.0f, 8.0f);
        // 2204 cascade coupling: V1a -> gain pot -> V1b, then V1b -> V2.
        // These caps/grid leaks now charge under positive-grid drive instead of
        // behaving as ideal high-pass filters, so hard palm-mutes recover like a
        // real master-volume Marshall.
        coupleGainToV1b.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f, 0.13f, 0.52f, 1.55f);
        coupleV1bToV2.set(sampleRate,   470000.0f, 22.0e-9f, 180000.0f, 0.13f, 0.54f, 1.65f);
        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 100000.0f, 0.20f, 0.30f, 0.75f);
        phaseInverter.setMarshall(sampleRate, 1.05f + 1.70f * mPush + 0.55f * pushed, 0.88f);
        supply.set(sampleRate,
                   45.0f, 100.0f,          // diode rectifier + 50u+50u reservoir
                   10000.0f, 50.0f,        // screen/PI node
                   10000.0f, 50.0f,        // preamp dropping node
                   0.08f + 0.03f * mPush,
                   0.06f + 0.03f * mPush,
                   0.04f + 0.02f * pushed,
                   0.16f);
        // 2x EL34 (~50W), fixed bias; master/PI drive the output pair.
        power.set(sampleRate, 6.0f + 10.0f * mPush + 6.5f * pushed, -38.0f, 0.18f, 50.0f, 12500.0f);
        power.out = 0.011f;

        inputHp.setHighPass(sampleRate, 30.0f + 18.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12500.0f - 1400.0f * pushed + 700.0f * treble, 0.64f);
        // JCM800 bright cap across the gain pot (more shimmer at low gain)
        brightCap.setHighShelf(sampleRate, 2000.0f, 0.72f, 3.2f * (1.0f - g));
        // inter-stage coupling; LO lifts the 0.68uF cathode bypass -> tighter, less low-mid gain
        interHp.setHighPass(sampleRate, 120.0f + 150.0f * pushed, 0.70f);
        cathodeLp.setLowPass(sampleRate, 9500.0f + 1200.0f * treble - 1400.0f * pushed, 0.64f);

        toneStack.update(treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f, 0.72f, 1.0f - 1.0f * pushed);
        phaseLp.setLowPass(sampleRate, 14000.0f + 1400.0f * treble - 1000.0f * pushed, 0.64f);
        // PRESENCE = power-amp NFB high-shelf
        presenceShelf.setHighShelf(sampleRate, 2600.0f, 0.78f, -1.0f + 7.0f * presence);

        // Marshall 4x12 voicing
        speakerHp.setHighPass(sampleRate, 84.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 120.0f, 0.84f, 0.8f + 2.0f * bass);
        speakerLowMid.setPeaking(sampleRate, 420.0f + 90.0f * mid, 0.74f, -1.2f + 1.4f * mid);   // the Marshall mid dip
        speakerBite.setPeaking(sampleRate, 2900.0f + 480.0f * treble, 0.62f, 3.0f + 1.5f * treble - 0.3f * pushed);
        // a real 4x12 ROLLS OFF the top (it does not boost +16 dB) -> gentle HF cut
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, -4.0f + 2.0f * treble + 2.0f * presence);
        speakerLp.setLowPass(sampleRate, 11000.0f + 1500.0f * treble - 1200.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); brightCap.reset(); interHp.reset(); cathodeLp.reset();
        toneStack.reset(); stackMakeupLow.reset(); phaseLp.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset(); speakerFizz.reset(); speakerLp.reset();
        dcBlock.reset();
        v1aMiller.reset(); v1bMiller.reset(); v2Miller.reset();
        coupleGainToV1b.reset(); coupleV1bToV2.reset(); coupleToPi.reset();
        v1a.reset(); v1b.reset(); v2.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
        updateFilters();
    }
    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; toneStack.setSampleRate(sampleRate); reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx) {
            case kGain: gain=v; break; case kBass: bass=v; break; case kMiddle: mid=v; break;
            case kTreble: treble=v; break; case kPresence: presence=v; break; case kVolume: volume=v; break;
            case kCabSim: cabSim=v; break; default: break;
        }
        updateFilters();
    }
    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kJcm800Def[i]); }

    float process(float in)
    {
        const float mPush = smoothstep(volume);
        const rbtube::SupplyScales bplus = supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = brightCap.process(x);
        // V1a: first gain stage (REAL 12AX7, ~clean — the 2204 GAIN pot is AFTER V1a)
        float y = v1a.process(v1aMiller.process(x) * bplus.preamp);
        // GAIN pot -> V1b cascade (the JCM800 drive) -> V2 (REAL 12AX7). The drive
        // span is wide so the amp cleans up below ~3 and slams hard at 10.
        y = coupleGainToV1b.process(y, 0.55f + 16.0f * gain);
        y = v1b.process(v1bMiller.process(y) * bplus.preamp);
        y = interHp.process(y);
        y = coupleV1bToV2.process(y, 0.65f + 10.0f * gain);
        y = v2.process(v2Miller.process(y) * bplus.preamp);
        y = cathodeLp.process(y);

        // Marshall tone stack + cathode follower makeup
        y = toneStack.process(y) * 2.0f;
        y = stackMakeupLow.process(y);

        // MASTER volume into the LTP/power amp
        y *= 0.22f + 1.30f * volume;
        y = phaseLp.process(y);

        y = coupleToPi.process(y, 1.0f);
        lastPreampLoad = std::fabs(y) * (0.20f + 0.55f * gain);
        y = phaseInverter.process(y * bplus.screen);
        lastScreenLoad = std::fabs(y) * (0.35f + 0.60f * mPush);

        // 2x EL34 (~50W) — real pentode table + supply/screen interaction.
        y = power.process(y * bplus.power * bplus.screen);
        lastPowerLoad = std::fabs(y) * (0.55f + 0.80f * mPush);

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        float cab = speakerHp.process(y);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizz.process(cab);
        cab = speakerLp.process(cab);
        y += cabSim * (cab - y);

        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.010f * std::fabs((presence - 0.5f) * 14.0f);
        // The real tubes + EL34 do the distortion; the output tanh is just gentle
        // output-transformer saturation/safety (NO cleanMakeup — that inverted the
        // crest curve by slamming clean tones into the clip).
        const float level = 0.95f / ((1.0f + 0.42f * mPush) * toneEnergy);
        // loudness flattening vs GAIN (clean post-output makeup; anchored ~0 dB at gain 0.5)
        float gcDb = 5.368f - 13.638f * gain + 4.005f * gain * gain;
        if (gcDb > 20.0f) gcDb = 20.0f; else if (gcDb < -12.0f) gcDb = -12.0f;
        return softClip(y * level) * 0.95f * std::pow(10.0f, 0.05f * gcDb);
    }
};

class Jcm800Plugin : public Plugin
{
    Jcm800Core left, right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;
    static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }
public:
    Jcm800Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kJcm800Def[i];
        left.setSampleRate(kOS * (float)getSampleRate()); right.setSampleRate(kOS * (float)getSampleRate()); applyAll();
    }
protected:
    const char* getLabel() const override { return "MarstenJCM800"; }
    const char* getDescription() const override { return "Marshall JCM800 2204 style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('M','j','8','0'); }
    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kJcm800Names[index]; parameter.symbol = kJcm800Symbols[index];
        parameter.ranges.min = kJcm800Min[index]; parameter.ranges.max = kJcm800Max[index]; parameter.ranges.def = kJcm800Def[index];
    }
    float getParameterValue(uint32_t index) const override { return index < (uint32_t)kParamCount ? params[index] : 0.0f; }
    void setParameterValue(uint32_t index, float value) override
    { if (index >= (uint32_t)kParamCount) return; params[index] = clamp01(value); left.setParam((int)index, params[index]); right.setParam((int)index, params[index]); }
    void sampleRateChanged(double newSampleRate) override
    { left.setSampleRate(kOS * (float)newSampleRate); right.setSampleRate(kOS * (float)newSampleRate); osL.reset(); osR.reset(); applyAll(); }
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL=inputs[0]; const float* inR=inputs[1]; float* outL=outputs[0]; float* outR=outputs[1];
        for (uint32_t i=0;i<frames;++i){
            float ub[kOS];
            osL.upsample(3.2f*inL[i], ub); for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.47f*left.process(ub[k])); outL[i]=osL.downsample(ub);
            osR.upsample(3.2f*inR[i], ub); for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.47f*right.process(ub[k])); outR[i]=osR.downsample(ub);
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Jcm800Plugin)
};
Plugin* createPlugin() { return new Jcm800Plugin(); }
END_NAMESPACE_DISTRHO
