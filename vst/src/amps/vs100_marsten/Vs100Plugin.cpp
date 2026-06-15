/*
 * MARSTEN VS100 - Marshall Valvestate VS100RH for the game's Amp_HG180. Parody
 * brand "Marsten"; the in-app face must never read "Marshall".
 *
 * Local reference (modelled component-by-component):
 *   amps/Marhsall VS100RH (HG-180)/v100-60/61/62-02.pdf
 *
 * HYBRID head: solid-state op-amp preamp + a 12AX7 "Valvestate" warmth stage into
 * a solid-state power amp. Three footswitchable modes:
 *   - CLEAN : Volume + Bass/Middle/Treble.
 *   - OD1   : crunch (Gain + Volume), on the clean EQ.
 *   - OD2   : high-gain lead — Gain + CONTOUR (mid scoop) + Volume + its own
 *             Bass/Middle/Treble (the Valvestate diode-clip lead).
 * Plus FX MIX + Clean/OD reverb.
 *
 * the game: RS Gain -> OD2 Gain; Bass/Mid/Treble -> OD2 Bass/Middle/Treble. See
 * rs_knob_to_vst_param.json (Channel pinned OD2 + reverb/FX off via _static).
 */
#include "DistrhoPlugin.hpp"
#include "Vs100Params.h"
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
static inline float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }
static inline float diodeClip(float x){ const float d=0.66f;
    if(x> d) return  d+std::tanh((x-d)*0.7f)*0.12f; if(x<-d) return -d+std::tanh((x+d)*0.7f)*0.12f; return x; }

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

// spring reverb (from the JC-90 model)
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
        if(nc0>3599)nc0=3599; if(nc1>3599)nc1=3599; inHp.setHighPass(sr,260.0f,0.7f); inLp.setLowPass(sr,3800.0f,0.7f); clear(); }
    void clear(){ for(int i=0;i<1024;++i) ap0[i]=ap1[i]=ap2[i]=0.0f; for(int i=0;i<3600;++i) cb0[i]=cb1[i]=0.0f;
        p0=p1=p2=c0=c1=0; damp0=damp1=0.0f; }
    float process(float x){ x=inLp.process(inHp.process(x));
        x=apStep(ap0,p0,n0,x,0.6f); x=apStep(ap1,p1,n1,x,0.6f); x=apStep(ap2,p2,n2,x,0.6f);
        const float o0=cb0[c0]; damp0+=0.42f*(o0-damp0); cb0[c0]=x+damp0*0.70f; if(++c0>=nc0)c0=0;
        const float o1=cb1[c1]; damp1+=0.42f*(o1-damp1); cb1[c1]=x+damp1*0.68f; if(++c1>=nc1)c1=0;
        return (o0+o1)*0.5f; }
};

} // namespace

class Vs100Core
{
    float sampleRate = 48000.0f;
    float channel = kVs100Def[kChannel];
    float clVol = kVs100Def[kClVolume], clBass = kVs100Def[kClBass], clMid = kVs100Def[kClMid], clTreble = kVs100Def[kClTreble];
    float od1Gain = kVs100Def[kOd1Gain], od1Vol = kVs100Def[kOd1Volume];
    float od2Gain = kVs100Def[kOd2Gain], od2Contour = kVs100Def[kOd2Contour], od2Vol = kVs100Def[kOd2Volume];
    float od2Bass = kVs100Def[kOd2Bass], od2Mid = kVs100Def[kOd2Mid], od2Treble = kVs100Def[kOd2Treble];
    float fxMix = kVs100Def[kFxMix], cleanRev = kVs100Def[kCleanRev], odRev = kVs100Def[kOdRev];

    // derived channel weights + active drive
    float cW=0.f, o1W=0.f, o2W=1.f, drv=0.6f;

    Biquad inputHp, inputLp;
    Biquad clLowSh, clMidPk, clHighSh;
    Biquad od2Tight, od2ClipLp, od2Contr, od2LowSh, od2MidPk, od2HighSh;
    Biquad speakerHp, speakerBite, speakerLp;
    SpringReverb spring;
    DcBlock dcBlock;

    void updateFilters()
    {
        cW = (channel < 0.34f) ? 1.f : 0.f;
        o1W = (channel >= 0.34f && channel < 0.67f) ? 1.f : 0.f;
        o2W = (channel >= 0.67f) ? 1.f : 0.f;
        drv = o2W ? od2Gain : (o1W ? od1Gain : clVol * 0.45f);
        const float pushed = smoothstepRange(0.40f, 0.95f, drv);

        inputHp.setHighPass(sampleRate, 58.0f, 0.70f);
        inputLp.setLowPass(sampleRate, 11500.0f, 0.64f);
        // CLEAN / OD1 EQ
        clLowSh.setLowShelf(sampleRate, 110.0f, 0.72f, eqDb(clBass, 9.0f));
        clMidPk.setPeaking(sampleRate, 600.0f, 0.70f, eqDb(clMid, 8.0f));
        clHighSh.setHighShelf(sampleRate, 3000.0f, 0.72f, eqDb(clTreble, 9.0f));
        // OD2 lead: tighten lows pre-clip, smooth post-clip, Contour scoop, own EQ
        od2Tight.setHighPass(sampleRate, 90.0f + 110.0f * od2Gain, 0.70f);
        od2ClipLp.setLowPass(sampleRate, 7200.0f - 800.0f * od2Gain, 0.66f);
        od2Contr.setPeaking(sampleRate, 620.0f, 0.80f, -10.0f * od2Contour);   // mid scoop (Valvestate Contour)
        od2LowSh.setLowShelf(sampleRate, 110.0f, 0.72f, eqDb(od2Bass, 10.0f));
        od2MidPk.setPeaking(sampleRate, 650.0f, 0.80f, eqDb(od2Mid, 9.0f));
        od2HighSh.setHighShelf(sampleRate, 3200.0f, 0.72f, eqDb(od2Treble, 10.0f));
        // Marshall-ish cab voicing
        speakerHp.setHighPass(sampleRate, 80.0f, 0.72f);
        speakerBite.setPeaking(sampleRate, 2600.0f, 0.78f, 2.4f + 1.6f * (o2W ? od2Treble : clTreble) - 0.5f * pushed);
        speakerLp.setLowPass(sampleRate, 14500.0f + 1600.0f * (o2W ? od2Treble : clTreble) - 3500.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset();
        clLowSh.reset(); clMidPk.reset(); clHighSh.reset();
        od2Tight.reset(); od2ClipLp.reset(); od2Contr.reset(); od2LowSh.reset(); od2MidPk.reset(); od2HighSh.reset();
        speakerHp.reset(); speakerBite.reset(); speakerLp.reset();
        spring.clear(); dcBlock.reset();
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; spring.setSampleRate(sampleRate); reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kChannel: channel = v; break;
            case kClVolume: clVol = v; break; case kClBass: clBass = v; break; case kClMid: clMid = v; break; case kClTreble: clTreble = v; break;
            case kOd1Gain: od1Gain = v; break; case kOd1Volume: od1Vol = v; break;
            case kOd2Gain: od2Gain = v; break; case kOd2Contour: od2Contour = v; break; case kOd2Volume: od2Vol = v; break;
            case kOd2Bass: od2Bass = v; break; case kOd2Mid: od2Mid = v; break; case kOd2Treble: od2Treble = v; break;
            case kFxMix: fxMix = v; break; case kCleanRev: cleanRev = v; break; case kOdRev: odRev = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kVs100Def[i]); }

    float process(float in)
    {
        const float pushed = smoothstepRange(0.40f, 0.95f, drv);
        float x = inputLp.process(inputHp.process(in));

        // CLEAN: a little op-amp limit, the clean EQ, level
        float cl = softClip(x * (1.0f + 2.6f * clVol)) * (1.0f / (1.0f + 0.4f * clVol));
        cl = clLowSh.process(clMidPk.process(clHighSh.process(cl)));
        cl *= 0.45f + 1.2f * clVol;

        // OD1: crunch — op-amp gain + soft clip, on the clean EQ
        float o1 = softClip(x * (1.6f + 9.0f * od1Gain));
        o1 = clLowSh.process(clMidPk.process(clHighSh.process(o1)));
        o1 *= 0.40f + 1.0f * od1Vol;

        // OD2: the Valvestate lead — big op-amp gain into the diode clip, Contour, own EQ
        float o2 = od2Tight.process(x);
        o2 = softClip(o2 * (2.0f + 16.0f * od2Gain));
        o2 = diodeClip(o2 * (1.0f + 5.0f * od2Gain));
        o2 = od2ClipLp.process(o2);
        o2 = od2Contr.process(o2);
        o2 = od2LowSh.process(od2MidPk.process(od2HighSh.process(o2)));
        o2 *= 0.42f + 1.0f * od2Vol;

        float y = cW * cl + o1W * o1 + o2W * o2;

        // reverb (clean mode -> Clean Reverb, OD modes -> OD Reverb)
        const float rev = cW * cleanRev + (o1W + o2W) * odRev;
        if (rev > 0.001f) y += spring.process(y) * rev * 0.55f;
        y = dcBlock.process(y);

        // 12AX7 "Valvestate" warmth + solid-state power (high headroom, gentle)
        y = 0.90f * y + 0.10f * softClip(y * 1.5f);

        y = speakerHp.process(y);
        y = speakerBite.process(y);
        y = speakerLp.process(y);

        // Loudness normalization: the active channel's gain is the drive proxy;
        // cleanMakeup keeps the RS Gain (-> OD2 Gain) sweep ~flat; the active
        // volume gives a mild swing. ~-14 dBFS reference.
        const float lvl = o2W ? od2Vol : (o1W ? od1Vol : clVol);
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs(((o2W ? od2Bass : clBass) - 0.5f) * 16.0f)
            + 0.012f * std::fabs(((o2W ? od2Mid : clMid) - 0.5f) * 16.0f)
            + 0.012f * std::fabs(((o2W ? od2Treble : clTreble) - 0.5f) * 16.0f);
        const float cleanMakeup = 1.0f + 1.4f * std::exp(-drv / 0.34f);
        const float level = 0.74f * cleanMakeup / ((1.0f + 0.55f * lvl) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class Vs100Plugin : public Plugin
{
    Vs100Core left;
    Vs100Core right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    Vs100Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kVs100Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarstenVS100"; }
    const char* getDescription() const override { return "Marshall Valvestate VS100 hybrid style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('V', 's', '1', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kVs100Names[index];
        parameter.symbol = kVs100Symbols[index];
        parameter.ranges.min = kVs100Min[index];
        parameter.ranges.max = kVs100Max[index];
        parameter.ranges.def = kVs100Def[index];
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
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
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
            outL[i] = rbAmpLvl(0.560f * left.process(3.2f * inL[i]));
            outR[i] = rbAmpLvl(0.560f * right.process(3.2f * inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Vs100Plugin)
};

Plugin* createPlugin() { return new Vs100Plugin(); }

END_NAMESPACE_DISTRHO
