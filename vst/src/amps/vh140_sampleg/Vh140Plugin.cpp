/*
 * SAMPLEG VH-140C - Ampeg VH-140C for the game's Amp_AT120. Parody brand
 * "Sampleg"; the in-app face must never read "Ampeg".
 *
 * Local reference (modelled component-by-component):
 *   amps/Ampeg VH-140C (AT-120)/Ampeg_VH-140C.pdf  (SLM solid-state schematic)
 *
 * A SOLID-STATE 2x70W stereo guitar head (TL074/JRC4558 op-amps + 1N914 diode
 * clipping, TDA power) — NO tubes. Two channels off one input:
 *   - CHANNEL A (clean): Gain -> light op-amp limit -> Low / Ultra Mid / High
 *     active EQ -> Level.
 *   - CHANNEL B (lead): Gain -> tightened, hard 1N914 diode clip -> Low / Mid /
 *     High active EQ -> Level. The famous tight, aggressive VH-140C distortion.
 * Per-channel spring REVERB + a BBD stereo CHORUS (rate + per-channel depth).
 * A footswitch picks the channel. Solid-state power = high headroom, no sag.
 *
 * the game: RS Gain -> CHANNEL B Gain; Bass/Mid/Treble -> Channel B Low/Mid/High.
 * See rs_knob_to_vst_param.json (Channel pinned B + reverb/chorus OFF via _static).
 */
#include "DistrhoPlugin.hpp"
#include "Vh140Params.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps).
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

namespace {

static constexpr float kPi = 3.14159265359f;
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float clampFreq(float hz, float sr) { return std::fmax(20.0f, std::fmin(hz, sr * 0.45f)); }
static inline float smoothstep(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }
static inline float smoothstepRange(float e0, float e1, float x) { return smoothstep((x - e0) / (e1 - e0)); }
static inline float softClip(float x) { return std::tanh(x); }
static inline float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }
// Hard 1N914 diode clip (symmetric ~±0.62, soft knee) — the VH-140C edge.
static inline float diodeClip(float x)
{
    const float d = 0.62f;
    if (x >  d) return  d + std::tanh((x - d) * 0.7f) * 0.10f;
    if (x < -d) return -d + std::tanh((x + d) * 0.7f) * 0.10f;
    return x;
}

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

// spring reverb (3 allpass + 2 damped combs) — from the JC-90 model.
class SpringReverb
{
    float ap0[1024], ap1[1024], ap2[1024], cb0[3600], cb1[3600];
    int p0=0,p1=0,p2=0,c0=0,c1=0, n0=225,n1=341,n2=441,nc0=1617,nc1=1991;
    float damp0=0.0f, damp1=0.0f; Biquad inHp, inLp;
    static inline float apStep(float* buf,int& p,int n,float in,float g)
    { const float bo=buf[p]; const float v=in+bo*g; buf[p]=v; if(++p>=n)p=0; return bo-v*g; }
public:
    void setSampleRate(float sr){ const float s=(sr>1000.0f?sr:48000.0f)/48000.0f;
        n0=(int)(225*s); n1=(int)(341*s); n2=(int)(441*s); nc0=(int)(1617*s); nc1=(int)(1991*s);
        if(nc0>3599)nc0=3599; if(nc1>3599)nc1=3599; inHp.setHighPass(sr,240.0f,0.7f); inLp.setLowPass(sr,3800.0f,0.7f); clear(); }
    void clear(){ for(int i=0;i<1024;++i) ap0[i]=ap1[i]=ap2[i]=0.0f; for(int i=0;i<3600;++i) cb0[i]=cb1[i]=0.0f;
        p0=p1=p2=c0=c1=0; damp0=damp1=0.0f; }
    float process(float x){ x=inLp.process(inHp.process(x));
        x=apStep(ap0,p0,n0,x,0.6f); x=apStep(ap1,p1,n1,x,0.6f); x=apStep(ap2,p2,n2,x,0.6f);
        const float o0=cb0[c0]; damp0+=0.42f*(o0-damp0); cb0[c0]=x+damp0*0.70f; if(++c0>=nc0)c0=0;
        const float o1=cb1[c1]; damp1+=0.42f*(o1-damp1); cb1[c1]=x+damp1*0.68f; if(++c1>=nc1)c1=0;
        return (o0+o1)*0.5f; }
};

// BBD-style stereo chorus — from the JC-90 model.
class Chorus
{
    float buf[8192]; int w=0; float fs=48000.0f, lfo=0.0f, inc=0.0f; Biquad wetLp;
    static inline int wrap(int i){ return i & 8191; }
    inline float readFrac(float ds){ float rp=(float)w-ds; while(rp<0.f)rp+=8192.f;
        int i0=(int)rp; float fr=rp-(float)i0; int i1=i0+1; return buf[wrap(i0)]+fr*(buf[wrap(i1)]-buf[wrap(i0)]); }
public:
    void setSampleRate(float s){ fs=s>1000.0f?s:48000.0f; wetLp.setLowPass(fs,6500.0f,0.7f); clear(); }
    void clear(){ std::memset(buf,0,sizeof(buf)); w=0; lfo=0.0f; wetLp.reset(); }
    void setRate(float r01){ const float hz=0.10f+7.0f*clamp01(r01); inc=2.0f*kPi*hz/fs; }
    void process(float x,float depthMs,float& wetL,float& wetR){ buf[w]=x;
        const float base=0.0080f*fs, mod=depthMs*0.001f*fs, s=std::sin(lfo);
        wetL=wetLp.process(readFrac(base+mod*(0.5f+0.5f*s)));
        wetR=readFrac(base+mod*(0.5f-0.5f*s));
        lfo+=inc; if(lfo>2.0f*kPi)lfo-=2.0f*kPi; if(++w>=8192)w=0; }
};

} // namespace

class Vh140Core
{
    float sampleRate = 48000.0f;
    float channel = kVh140Def[kChannel];
    float bGain = kVh140Def[kBGain], bLow = kVh140Def[kBLow], bMid = kVh140Def[kBMid], bHigh = kVh140Def[kBHigh], bLevel = kVh140Def[kBLevel];
    float aGain = kVh140Def[kAGain], aLow = kVh140Def[kALow], aMid = kVh140Def[kAUltraMid], aHigh = kVh140Def[kAHigh], aLevel = kVh140Def[kALevel];
    float reverbB = kVh140Def[kReverbB], reverbA = kVh140Def[kReverbA];
    float rate = kVh140Def[kRate], depthB = kVh140Def[kDepthB], depthA = kVh140Def[kDepthA];

    float chan = 1.0f, drv = 0.5f;   // derived: channel + active-channel drive

    Biquad inputHp, inputLp;
    Biquad aLowSh, aMidPk, aHighSh;
    Biquad bTight, bClipLp, bLowSh, bMidPk, bHighSh;
    Biquad speakerHp, speakerLp;
    SpringReverb spring;
    Chorus chorus;
    DcBlock dcBlock;

    void updateFilters()
    {
        chan = smoothstep(channel);                       // 0 = A, 1 = B
        drv  = chan * bGain + (1.0f - chan) * aGain * 0.55f;

        inputHp.setHighPass(sampleRate, 60.0f, 0.70f);
        inputLp.setLowPass(sampleRate, 11000.0f, 0.64f);
        // CHANNEL A active EQ (clean): Low / Ultra Mid / High
        aLowSh.setLowShelf(sampleRate, 110.0f, 0.72f, eqDb(aLow, 9.0f));
        aMidPk.setPeaking(sampleRate, 520.0f, 0.70f, eqDb(aMid, 9.0f));   // "Ultra Mid" = low-mid
        aHighSh.setHighShelf(sampleRate, 3000.0f, 0.72f, eqDb(aHigh, 9.0f));
        // CHANNEL B: tighten lows before the clip (the VH-140C palm-mute tightness),
        // smooth post-clip fizz, then Low / Mid / High active EQ.
        bTight.setHighPass(sampleRate, 90.0f + 120.0f * bGain, 0.70f);
        bClipLp.setLowPass(sampleRate, 7000.0f - 800.0f * bGain, 0.66f);
        bLowSh.setLowShelf(sampleRate, 110.0f, 0.72f, eqDb(bLow, 10.0f));
        bMidPk.setPeaking(sampleRate, 650.0f, 0.80f, eqDb(bMid, 10.0f));
        bHighSh.setHighShelf(sampleRate, 3200.0f, 0.72f, eqDb(bHigh, 11.0f));
        // solid-state cab voicing. Open the cab lowpass for a miked-cab brightness,
        // with a gain-dependent retreat (drv = active-channel drive) so cranked
        // high-gain tones don't fizz.
        speakerHp.setHighPass(sampleRate, 80.0f, 0.72f);
        speakerLp.setLowPass(sampleRate, 14000.0f + 1500.0f * (chan ? bHigh : aHigh) - 3500.0f * drv, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset();
        aLowSh.reset(); aMidPk.reset(); aHighSh.reset();
        bTight.reset(); bClipLp.reset(); bLowSh.reset(); bMidPk.reset(); bHighSh.reset();
        speakerHp.reset(); speakerLp.reset();
        spring.clear(); chorus.clear(); dcBlock.reset();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        spring.setSampleRate(sampleRate); chorus.setSampleRate(sampleRate); chorus.setRate(rate);
        reset();
    }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kChannel: channel = v; break;
            case kBGain: bGain = v; break;  case kBLow: bLow = v; break;  case kBMid: bMid = v; break;
            case kBHigh: bHigh = v; break;  case kBLevel: bLevel = v; break;
            case kAGain: aGain = v; break;  case kALow: aLow = v; break;  case kAUltraMid: aMid = v; break;
            case kAHigh: aHigh = v; break;  case kALevel: aLevel = v; break;
            case kReverbB: reverbB = v; break;  case kReverbA: reverbA = v; break;
            case kRate: rate = v; chorus.setRate(v); break;  case kDepthB: depthB = v; break;  case kDepthA: depthA = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kVh140Def[i]); }

    void process(float inL, float inR, float& outL, float& outR)
    {
        float x = 0.5f * (inL + inR);
        x = inputHp.process(x);
        x = inputLp.process(x);

        // CHANNEL A (clean): a modest op-amp gain stage, soft limit, active EQ.
        float a = softClip(x * (1.0f + 9.0f * aGain)) * (1.0f / (1.0f + 0.4f * aGain));
        a = aLowSh.process(a); a = aMidPk.process(a); a = aHighSh.process(a);
        a *= 1.1f + 1.8f * aLevel;

        // CHANNEL B (lead): big op-amp gain into the hard 1N914 diode clip.
        float b = bTight.process(x);
        b = softClip(b * (2.0f + 18.0f * bGain));           // op-amp gain stage
        b = diodeClip(b * (1.0f + 6.0f * bGain));            // slammed into the diodes
        b = bClipLp.process(b);
        b = bLowSh.process(b); b = bMidPk.process(b); b = bHighSh.process(b);
        b *= 0.40f + 1.0f * bLevel;

        // channel select
        float y = chan * b + (1.0f - chan) * a;

        // spring reverb (per-channel send)
        const float rev = chan * reverbB + (1.0f - chan) * reverbA;
        if (rev > 0.001f) y += spring.process(y) * rev * 0.55f;
        y = dcBlock.process(y);

        // solid-state cab voicing
        y = speakerHp.process(y);
        y = speakerLp.process(y);

        // loudness normalization (the diode clip limits B; cleanMakeup lifts the
        // low-gain end so RS Gain -> B Gain stays ~flat; level keeps ~-14 dBFS).
        const float lvl = chan * bLevel + (1.0f - chan) * aLevel;
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs(((chan ? bLow : aLow) - 0.5f) * 16.0f)
            + 0.012f * std::fabs(((chan ? bMid : aMid) - 0.5f) * 16.0f)
            + 0.012f * std::fabs(((chan ? bHigh : aHigh) - 0.5f) * 16.0f);
        // Channel B clips into the diodes at almost any Gain (near-constant
        // amplitude), so loudness is ~flat vs Gain — a gentle makeup is enough.
        const float cleanMakeup = 1.0f + 1.0f * std::exp(-drv / 0.40f);
        const float level = 0.73f * cleanMakeup / ((1.0f + 0.55f * lvl) * toneEnergy);
        y = softClip(y * level) * 0.97f;

        // BBD stereo chorus (per-channel depth) opens the L/R image.
        const float depth = chan * depthB + (1.0f - chan) * depthA;
        if (depth > 0.004f)
        {
            float wl, wr; chorus.process(y, 1.4f + depth * 5.5f, wl, wr);
            const float cmix = 0.5f * smoothstepRange(0.0f, 0.9f, depth);
            outL = y * (1.0f - cmix) + wl * cmix;
            outR = y * (1.0f - cmix) + wr * cmix;
        }
        else
        {
            // keep the chorus delay line primed so engaging it isn't a click
            float wl, wr; chorus.process(y, 1.4f, wl, wr);
            outL = y; outR = y;
        }
    }
};

class Vh140Plugin : public Plugin
{
    Vh140Core core;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) core.setParam(i, params[i]); }

public:
    Vh140Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kVh140Def[i];
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "SamplegVH140C"; }
    const char* getDescription() const override { return "Ampeg VH-140C solid-state style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('V', 'h', '1', '4'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kVh140Names[index];
        parameter.symbol = kVh140Symbols[index];
        parameter.ranges.min = kVh140Min[index];
        parameter.ranges.max = kVh140Max[index];
        parameter.ranges.def = kVh140Def[index];
    }

    float getParameterValue(uint32_t index) const override { return index < (uint32_t)kParamCount ? params[index] : 0.0f; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount) return;
        params[index] = clamp01(value);
        core.setParam((int)index, params[index]);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        core.setSampleRate((float)newSampleRate);
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
            float oL, oR;
            core.process(3.2f * inL[i], 3.2f * inR[i], oL, oR);
            outL[i] = rbAmpLvl(0.560f * oL);
            outR[i] = rbAmpLvl(0.560f * oR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Vh140Plugin)
};

Plugin* createPlugin() { return new Vh140Plugin(); }

END_NAMESPACE_DISTRHO
