/*
 * CITRUS RUMBLEVERB 50 = Orange Rockerverb 50 MkII, 2-channel 2x EL34 head + valve
 * spring reverb (parody "Citrus"). DSP via the OWN citrus::CitrusCore<EL34>: the
 * DIRTY channel is a deep cascade + driven power amp (the real Orange high-gain
 * grind the shared core lacked = "needs more gain vs the reference"); CLEAN is the
 * natural channel. All panel controls wired (per-channel tone stacks, Volume,
 * Output, the spring Reverb) + a fallback 4x12. RS Gain/Bass/Mid/Treble -> DIRTY.
 */
#include "DistrhoPlugin.hpp"
#include "RumbleverbParams.h"
#include "../../_shared/citrus_core.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static constexpr float kPi = 3.14159265358979f;
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

struct Bq {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=y; return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void lowpass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al;
        b0=(1-c)*0.5f/a0; b1=(1-c)/a0; b2=(1-c)*0.5f/a0; a1=-2*c/a0; a2=(1-al)/a0; }
    void highpass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al;
        b0=(1+c)*0.5f/a0; b1=-(1+c)/a0; b2=(1+c)*0.5f/a0; a1=-2*c/a0; a2=(1-al)/a0; }
    void peak(float sr,float f,float dB,float Q){ if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} if(f>sr*0.49f)f=sr*0.49f;
        float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al/A;
        b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
    void lowShelf(float sr,float f,float dB){ if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} if(f<10)f=10;
        float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)+(A-1)*c+t; b0=A*((A+1)-(A-1)*c+t)/a0; b1=2*A*((A-1)-(A+1)*c)/a0; b2=A*((A+1)-(A-1)*c-t)/a0; a1=-2*((A-1)+(A+1)*c)/a0; a2=((A+1)+(A-1)*c-t)/a0; }
};

struct Comb { float buf[4096]={0}; int len=1116, idx=0; float store=0, damp=0.25f, fb=0.84f;
    inline float process(float x){ float y=buf[idx]; store=y*(1.0f-damp)+store*damp; buf[idx]=x+store*fb; if(++idx>=len)idx=0; return y; }
    void reset(){ for(int i=0;i<4096;++i)buf[i]=0; store=0; idx=0; } };
struct Allpass { float buf[2048]={0}; int len=556, idx=0;
    inline float process(float x){ float y=buf[idx]; float out=-x+y; buf[idx]=x+y*0.5f; if(++idx>=len)idx=0; return out; }
    void reset(){ for(int i=0;i<2048;++i)buf[i]=0; idx=0; } };
struct Reverb {
    Comb c0,c1,c2,c3; Allpass a0,a1;
    void setSR(float sr){ float s=sr/44100.f; auto cl=[](int n){ return n<1?1:(n>4095?4095:n); }; auto al=[](int n){ return n<1?1:(n>2047?2047:n); };
        c0.len=cl((int)(1116*s)); c1.len=cl((int)(1277*s)); c2.len=cl((int)(1422*s)); c3.len=cl((int)(1557*s));
        a0.len=al((int)(556*s)); a1.len=al((int)(341*s)); reset(); }
    void reset(){ c0.reset();c1.reset();c2.reset();c3.reset();a0.reset();a1.reset(); }
    inline float process(float x){ float in=x*0.18f; float y=c0.process(in)+c1.process(in)+c2.process(in)+c3.process(in); y=a0.process(y); y=a1.process(y); return y; }
};

class RumbleverbPlugin : public Plugin {
    citrus::CitrusCore<rbtube::PowerAmpEL34> core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    float osr = 96000.0f, baseSr = 48000.0f;
    float chanVol = 1.0f, rvMix = 0.2f;
    Bq cabHP, cabLowShelf, cabPresence, cabTopRoll;
    Reverb reverb;
    bool cabOn = true;

    void cfg(){
        core.setTone(250e3, 470e3, 25e3, 33e3, 250e-12, 22e-9, 22e-9);
        core.cfgDirtySpan = 9.0f; core.cfgNStages = 3; core.cfgHpDirty = 80.0f; core.cfgHpClean = 55.0f;
        core.cfgBias = -38.0f; core.cfgPwDrive = 3.0f; core.cfgTilt = 6.0f; core.cfgScoop = -2.0f;
    }
    void applyAll(){
        const bool dty = fP[kChannel] >= 0.5f;
        core.dirty = dty;
        if (dty) {
            core.pGain=fP[kGain]; core.pBass=fP[kBass]; core.pMid=fP[kMiddle]; core.pTreble=fP[kTreble];
            core.cfgMkDirty = -5.0f; core.cfgOt = 13000.0f;
            chanVol = 0.4f + 1.0f*fP[kVolume];
        } else {
            core.pGain=fP[kCleanVolume]; core.pBass=fP[kCleanBass]; core.pMid=0.5f; core.pTreble=fP[kCleanTreble];
            core.cfgMkClean = 14.0f; core.cfgOt = 13000.0f;
            chanVol = 1.0f;
        }
        core.pPres=0.5f; core.pVol=fP[kOutput];
        core.recalc();
        rvMix = fP[kReverb];

        cabOn = fP[kCabSim] >= 0.5f;
        cabHP.highpass(osr, 85.0f, 0.70f);
        cabLowShelf.lowShelf(osr, 240.0f, -4.0f);
        cabPresence.peak(osr, 3000.0f, 2.0f, 0.6f);
        cabTopRoll.lowpass(osr, 5200.0f, 0.70f);     // darker -> ref rockerverb_gain_7 hi ~-11
    }
public:
    RumbleverbPlugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kRumbleverbDef[i];
        baseSr=(float)getSampleRate(); osr=kOS*baseSr; core.setSampleRate(osr); reverb.setSR(baseSr); cfg(); applyAll(); }
protected:
    const char* getLabel() const override { return "CitrusRumbleverb50"; }
    const char* getDescription() const override { return "CitrusRumbleverb50 — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,2,0); }
    int64_t getUniqueId() const override { return d_cconst('R','B','R','V'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kChannel||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kRumbleverbNames[i]; p.symbol=kRumbleverbSymbols[i]; p.ranges.min=kRumbleverbMin[i]; p.ranges.max=kRumbleverbMax[i]; p.ranges.def=kRumbleverbDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { baseSr=(float)r; osr=kOS*baseSr; core.setSampleRate(osr); os.reset(); reverb.setSR(baseSr); cfg(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k){ float s=core.process(ub[k])*chanVol;
                if(cabOn){ s=cabHP.process(s); s=cabLowShelf.process(s); s=cabPresence.process(s); s=cabTopRoll.process(s); }
                ub[k]=rbAmpLvl(0.50f*s); }
            float y=os.downsample(ub);
            if(rvMix>0.001f){ float wet=reverb.process(y); y=y*(1.0f-0.5f*rvMix)+wet*rvMix; }
            oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleverbPlugin)
};
Plugin* createPlugin(){ return new RumbleverbPlugin(); }
END_NAMESPACE_DISTRHO
