/*
 * MR. Y EMS - Dr. Z EMS (a Marshall JCM800/JTM50-style master-volume head) for
 * the game's Amp_GB50. Parody brand "Mr. Y"; the in-app face must never read
 * "Dr. Z". Reference: the EMS is a JCM800 2204 circuit + a HI/LO gain switch.
 *
 * Cascaded 12AX7 preamp (GAIN pot between the two stages, the JCM800 drive) ->
 * Marshall TMB tone stack -> cathode follower -> MASTER volume -> 2x EL34 (~50W)
 * with a presence NFB. HI = full JCM800 cascade; LO drops gain to JTM50 levels
 * (input divider + lifted 2nd-stage cathode bypass = less low-mid gain).
 *
 * RS: Gain -> GAIN, Bass/Mid/Treble -> tone stack, Pres -> Presence. Master +
 * HI/LO pinned via _static.
 */
#include "DistrhoPlugin.hpp"
#include "EmsParams.h"
#include "../../_shared/tube_stage.hpp"   // real 12AX7 stages + EL34 PP + Yeh tone stack
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
        const double R1=220.0e3, R2=1.0e6, R3=25.0e3, R4=33.0e3;
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

class EmsCore
{
    float sampleRate = 48000.0f;
    float gain = kEmsDef[kGain];
    float bass = kEmsDef[kBass];
    float mid  = kEmsDef[kMiddle];
    float treble = kEmsDef[kTreble];
    float presence = kEmsDef[kPresence];
    float volume = kEmsDef[kVolume];
    float hilo = kEmsDef[kHiLo];
    float cabSim = kEmsDef[kCabSim];

    Biquad inputHp, pickupLoad, brightCap, interHp, cathodeLp;
    Biquad stackMakeupLow, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    float sag = 0.0f;
    // ── real circuit (Koren tube tables) replacing the tanh asymTube ──
    rbtube::TubeStage    v1a, v1b, v2;     // 3x 12AX7 cascade (JCM800 2204: V1a -> GAIN -> V1b -> V2)
    rbtube::Miller12AX7  v1aMiller, v1bMiller, v2Miller;
    rbtube::CouplingCapGridLeak coupleGainToV1b, coupleV1bToV2, coupleToPi;
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;          // diode rectifier + B+ filter nodes
    rbtube::PowerAmpEL34 power;            // 2x EL34 (~50W)
    rbtube::ToneStackYeh tone;             // real Marshall TMB (double)
    float inScale = 1.0f, toneMk = 13.0f;
    float lastPowerLoad = 0.0f, lastScreenLoad = 0.0f, lastPreampLoad = 0.0f;

    void setupTubes()
    {
        v1a.set(sampleRate, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v1b.set(sampleRate, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v2.set(sampleRate, 1, 250.0f, 40.0f, 55.0f, 1500.0f);   // JCM800 2204 fck values
        v1aMiller.set(sampleRate,  68000.0f, 55.0f, 8.0f);
        v1bMiller.set(sampleRate, 220000.0f, 52.0f, 8.0f);
        v2Miller.set(sampleRate,  180000.0f, 52.0f, 8.0f);
        coupleGainToV1b.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f, 0.13f, 0.52f, 1.55f);
        coupleV1bToV2.set(sampleRate,   470000.0f, 22.0e-9f, 180000.0f, 0.13f, 0.54f, 1.65f);
    }

    void updateFilters()
    {
        const float lo = (hilo >= 0.5f) ? 1.0f : 0.0f;          // 1 = LO (JTM50-ish)
        const float g = smoothstep(gain);
        const float pushed = smoothstepRange(0.40f, 0.92f, gain);
        const float mPush = smoothstep(volume);

        inputHp.setHighPass(sampleRate, 30.0f + 18.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12500.0f - 1400.0f * pushed + 700.0f * treble, 0.64f);
        // JCM800 bright cap across the gain pot (more shimmer at low gain)
        brightCap.setHighShelf(sampleRate, 2000.0f, 0.72f, 3.2f * (1.0f - g));
        // inter-stage coupling; LO lifts the 0.68uF cathode bypass -> tighter, less low-mid gain
        interHp.setHighPass(sampleRate, 120.0f + 150.0f * pushed + 120.0f * lo, 0.70f);
        cathodeLp.setLowPass(sampleRate, 9500.0f + 1200.0f * treble - 1400.0f * pushed, 0.64f);

        tone.setComponents(220e3, 1.0e6, 25e3, 33e3, 470e-12, 22e-9, 22e-9);  // JCM800 2204 Marshall TMB
        tone.update(sampleRate, treble, mid, bass);
        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 100000.0f, 0.14f, 0.45f, 1.05f);
        phaseInverter.setMarshall(sampleRate, 0.95f + 1.55f * mPush + 0.75f * pushed, 0.88f);
        supply.set(sampleRate,
                   20.0f, 100.0f,
                   1000.0f, 50.0f,
                   10000.0f, 22.0f,
                   0.07f + 0.025f * pushed,
                   0.055f + 0.020f * pushed,
                   0.035f + 0.018f * gain,
                   0.14f);
        // 2x EL34 power amp drive (Master + Gain). B+ nodes provide the dynamic sag.
        power.set(sampleRate, 4.4f + 9.8f * mPush + 6.4f * pushed, -40.0f, 0.08f, 55.0f, 11200.0f);
        power.out = 0.013f;
        stackMakeupLow.setLowShelf(sampleRate, 120.0f, 0.72f, 1.0f - 1.0f * pushed - 1.2f * lo);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1400.0f * treble - 2000.0f * pushed, 0.64f);
        // PRESENCE = power-amp NFB high-shelf
        presenceShelf.setHighShelf(sampleRate, 2600.0f, 0.78f, -1.0f + 7.0f * presence);

        // Marshall 4x12 voicing
        speakerHp.setHighPass(sampleRate, 84.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 120.0f, 0.84f, 0.8f + 2.0f * bass);
        speakerLowMid.setPeaking(sampleRate, 420.0f + 90.0f * mid, 0.74f, -1.2f + 1.4f * mid);   // the Marshall mid dip
        speakerBite.setPeaking(sampleRate, 2600.0f + 480.0f * treble, 0.78f, 2.4f + 2.0f * treble - 0.5f * pushed);
        // Mild 4x12 top air. The previous +9.5 dB shelf created sample jumps
        // in the fallback cab while the amp-only path stayed stable.
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, 3.5f + 1.5f * treble - 3.5f * pushed);
        speakerLp.setLowPass(sampleRate, 13200.0f + 1200.0f * treble - 3200.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); brightCap.reset(); interHp.reset(); cathodeLp.reset();
        tone.reset(); stackMakeupLow.reset(); phaseLp.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset(); speakerFizz.reset(); speakerLp.reset();
        v1aMiller.reset(); v1bMiller.reset(); v2Miller.reset();
        coupleGainToV1b.reset(); coupleV1bToV2.reset();
        v1a.reset(); v1b.reset(); v2.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        dcBlock.reset(); sag = 0.0f;
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
        setupTubes(); updateFilters();
    }
    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx) {
            case kGain: gain=v; break; case kBass: bass=v; break; case kMiddle: mid=v; break;
            case kTreble: treble=v; break; case kPresence: presence=v; break; case kVolume: volume=v; break;
            case kHiLo: hilo=v; break; case kCabSim: cabSim=v; break; default: break;
        }
        updateFilters();
    }
    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kEmsDef[i]); }

    float process(float in)
    {
        const float lo = (hilo >= 0.5f) ? 1.0f : 0.0f;
        const float mPush = smoothstep(volume);
        const float gainTrim = lo ? 0.55f : 1.0f;               // LO input divider -> JTM50 gain
        const float pushed = smoothstepRange(0.40f, 0.92f, gain);
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = brightCap.process(x);
        x *= gainTrim;
        // V1a: first gain stage (real 12AX7)
        float y = v1a.process(v1aMiller.process(x) * inScale * bplus.preamp);
        // GAIN pot -> V1b cascade (the JCM800 drive). LO removes some cascade gain.
        const float drv = (1.0f - 0.30f * lo);
        y = coupleGainToV1b.process(y, (0.55f + 16.0f * gain) * drv);
        y = v1b.process(v1bMiller.process(y) * bplus.preamp);
        y = interHp.process(y);
        y = coupleV1bToV2.process(y, (0.65f + 10.0f * gain) * drv);
        y = v2.process(v2Miller.process(y) * bplus.preamp);
        y = cathodeLp.process(y);

        // Marshall tone stack (real Yeh) + cathode follower makeup
        y = tone.process(y) * toneMk;
        y = stackMakeupLow.process(y);

        // MASTER volume into the power amp
        y *= 0.22f + 1.30f * volume;

        y = coupleToPi.process(y, 1.0f + 0.14f * pushed);
        lastPreampLoad = 0.12f * std::fabs(y) + 0.05f * gain;
        y = phaseInverter.process(y * bplus.preamp);
        lastPowerLoad = 0.82f * std::fabs(y) + 0.20f * pushed;
        lastScreenLoad = 0.50f * std::fabs(y) + 0.10f * gain;

        // 2x EL34 (~50W) power amp — real pentode table + LTP/B+ dynamics.
        y = power.process(y * bplus.power * bplus.screen);

        y = phaseLp.process(y);
        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        float cab = speakerHp.process(y);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizz.process(cab);
        cab = speakerLp.process(cab);
        y += cabSim * (0.72f * cab - y);

        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.010f * std::fabs((presence - 0.5f) * 14.0f);
        // NO cleanMakeup — with real tubes it inverts the crest curve (proven on jcm800).
        const float level = (0.88f + 0.10f * lo) / ((1.0f + 0.42f * mPush) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class EmsPlugin : public Plugin
{
    EmsCore left, right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;          // 2x anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }
public:
    EmsPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kEmsDef[i];
        left.setSampleRate(kOS * (float)getSampleRate()); right.setSampleRate(kOS * (float)getSampleRate()); applyAll();
    }
protected:
    const char* getLabel() const override { return "MrYEMS"; }
    const char* getDescription() const override { return "Dr Z EMS JCM800-style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('Y','e','m','s'); }
    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kEmsNames[index]; parameter.symbol = kEmsSymbols[index];
        parameter.ranges.min = kEmsMin[index]; parameter.ranges.max = kEmsMax[index]; parameter.ranges.def = kEmsDef[index];
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
            float ubL[kOS], ubR[kOS];
            osL.upsample(3.2f * inL[i], ubL); osR.upsample(3.2f * inR[i], ubR);
            for (int k=0;k<kOS;++k){ ubL[k]=rbAmpLvl(0.560f*left.process(ubL[k])); ubR[k]=rbAmpLvl(0.560f*right.process(ubR[k])); }
            outL[i]=osL.downsample(ubL); outR[i]=osR.downsample(ubR);
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EmsPlugin)
};
Plugin* createPlugin() { return new EmsPlugin(); }
END_NAMESPACE_DISTRHO
