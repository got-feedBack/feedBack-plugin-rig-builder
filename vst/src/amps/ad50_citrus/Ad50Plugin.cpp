/*
 * CITRUS AD50 = Orange AD50 (Custom Shop), single-channel ~50W EL34 head (parody
 * "Citrus"). DSP via the OWN citrus::CitrusCore<EL34> — a deep cascade that
 * generates the real Orange high-order grind/fizz (the shared core's 1-stage clip
 * compressed but had almost no upper harmonics = "lacks gain"). Panel: Gain · Bass ·
 * Treble · Presence · Master · Sustain (boost) · Class A · Cab. Wires the DEAD
 * Sustain / Class A / Cab Sim + a fallback Orange cab. Voiced vs the AD30 reference.
 * RS Gain/Bass/Treble/Presence.
 */
#include "DistrhoPlugin.hpp"
#include "Ad50Params.h"
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

class Ad50Plugin : public Plugin {
    citrus::CitrusCore<rbtube::PowerAmpEL34> core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    float osr = 96000.0f;
    Bq cabHP, cabLowShelf, cabPresence, cabTopRoll;
    bool cabOn = true;

    void cfg(){
        core.setTone(250e3, 470e3, 25e3, 33e3, 250e-12, 22e-9, 22e-9);   // Orange-ish
        core.cfgDirtySpan = 8.0f; core.cfgNStages = 3; core.cfgHpDirty = 90.0f;
        core.cfgOt = 13000.0f; core.cfgTilt = 7.0f; core.cfgScoop = -2.0f;
        core.cfgBias = -38.0f; core.cfgPwDrive = 3.0f; core.cfgMkDirty = -0.5f;
    }
    void applyAll(){
        const bool sustain = fP[kSustain] >= 0.5f;   // EQ-bypass gain/sustain boost
        const bool classA  = fP[kClassA]  >= 0.5f;   // 30W Class A = earlier breakup
        core.dirty = true;
        core.pGain=fP[kGain]; core.pBass=fP[kBass]; core.pMid=0.5f; core.pTreble=fP[kTreble];
        core.pPres=fP[kPresence]; core.pVol=fP[kMaster];
        core.inBoost = (sustain ? 1.6f : 1.0f) * (classA ? 1.25f : 1.0f);
        core.cfgMkDirty = -5.0f - (sustain ? 1.5f : 0.0f) - (classA ? 1.5f : 0.0f);  // Sustain = a boost
        core.cfgOt = classA ? 10000.0f : 13000.0f;   // Class A = softer/darker top
        core.recalc();

        cabOn = fP[kCabSim] >= 0.5f;
        cabHP.highpass(osr, 90.0f, 0.70f);
        cabLowShelf.lowShelf(osr, 320.0f, -4.0f);
        cabPresence.peak(osr, 2600.0f, 4.0f, 0.55f);
        cabTopRoll.lowpass(osr, classA ? 6500.0f : 8500.0f, 0.70f);
    }
public:
    Ad50Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kAd50Def[i];
        osr=kOS*(float)getSampleRate(); core.setSampleRate(osr); cfg(); applyAll(); }
protected:
    const char* getLabel() const override { return "CitrusAD50"; }
    const char* getDescription() const override { return "Orange AD50 style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('A','d','5','0'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kSustain||i==(uint32_t)kClassA||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kAd50Names[i]; p.symbol=kAd50Symbols[i]; p.ranges.min=kAd50Min[i]; p.ranges.max=kAd50Max[i]; p.ranges.def=kAd50Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float val) override { if(i<(uint32_t)kParamCount){fP[i]=val; applyAll();} }
    void sampleRateChanged(double r) override { osr=kOS*(float)r; core.setSampleRate(osr); os.reset(); cfg(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k){ float s=core.process(ub[k]);
                if(cabOn){ s=cabHP.process(s); s=cabLowShelf.process(s); s=cabPresence.process(s); s=cabTopRoll.process(s); }
                ub[k]=rbAmpLvl(0.50f*s); }
            const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ad50Plugin)
};
Plugin* createPlugin(){ return new Ad50Plugin(); }
END_NAMESPACE_DISTRHO
