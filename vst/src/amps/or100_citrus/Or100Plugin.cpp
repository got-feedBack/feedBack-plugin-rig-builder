/*
 * CITRUS OR100 = Orange OR100 (vintage "Graphic" single-channel ~100W, 4x EL34,
 * parody "Citrus"). The OR50's bigger brother. DSP via the OWN citrus::CitrusCore
 * <EL34> — a cascade + driven EL34 power amp for the thick Orange "doom chunk"
 * grind (the shared core's 1-stage clip lacked the high harmonics). Voiced vs the
 * OR120 reference (test logic/remaining/or120_gain_*): thick + boomy + mid-forward.
 * Panel: Gain · Bass · Middle(FAC) · Treble · Depth · Volume · Half. Wires the
 * DEAD Depth / Half / Cab Sim. RS Gain/Bass/Mid/Treble.
 */
#include "DistrhoPlugin.hpp"
#include "Or100Params.h"
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

class Or100Plugin : public Plugin {
    citrus::CitrusCore<rbtube::PowerAmpEL34> core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    float osr = 96000.0f;
    Bq cabHP, cabLowShelf, cabDepth, cabPresence, cabTopRoll;
    bool cabOn = true;

    void cfg(){
        core.setTone(250e3, 470e3, 25e3, 33e3, 250e-12, 22e-9, 22e-9);
        core.cfgDirtySpan = 6.5f; core.cfgNStages = 2; core.cfgHpDirty = 72.0f;   // 100W = fuller lows
        core.cfgBias = -38.0f; core.cfgPwDrive = 4.0f; core.cfgTilt = 6.0f; core.cfgScoop = -1.0f;  // mid-forward
    }
    void applyAll(){
        const bool half = fP[kHalf] >= 0.5f;     // FULL(0) / HALF power
        core.dirty = true;
        core.pGain=fP[kGain]; core.pBass=fP[kBass]; core.pMid=fP[kMiddle]; core.pTreble=fP[kTreble];
        core.pPres=0.5f; core.pVol=fP[kVolume];
        core.inBoost   = half ? 1.3f : 1.0f;
        core.cfgPwDrive = half ? 6.0f : 4.0f;
        core.cfgOt      = half ? 9000.0f : 11000.0f;
        core.cfgMkDirty = (half ? -6.0f : -4.0f);            // ~-16 dBFS
        core.recalc();

        // Thick/boomy OR120 voice -> a gentler low-shelf cut than the OR50; DEPTH
        // (the bass-cap rotary) low shelf from tight (-4) to fat (+4).
        cabOn = fP[kCabSim] >= 0.5f;
        cabHP.highpass(osr, 75.0f, 0.70f);
        cabLowShelf.lowShelf(osr, 300.0f, -3.0f);
        cabDepth.lowShelf(osr, 130.0f, (fP[kDepth]-0.5f)*8.0f);
        cabPresence.peak(osr, 2600.0f, 4.0f, 0.55f);
        cabTopRoll.lowpass(osr, half ? 6500.0f : 8000.0f, 0.70f);
    }
public:
    Or100Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kOr100Def[i];
        osr=kOS*(float)getSampleRate(); core.setSampleRate(osr); cfg(); applyAll(); }
protected:
    const char* getLabel() const override { return "CitrusOR100"; }
    const char* getDescription() const override { return "Orange OR100 style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('O','r','1','0'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kHalf||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kOr100Names[i]; p.symbol=kOr100Symbols[i]; p.ranges.min=kOr100Min[i]; p.ranges.max=kOr100Max[i]; p.ranges.def=kOr100Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float val) override { if(i<(uint32_t)kParamCount){fP[i]=val; applyAll();} }
    void sampleRateChanged(double r) override { osr=kOS*(float)r; core.setSampleRate(osr); os.reset(); cfg(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k){ float s=core.process(ub[k]);
                if(cabOn){ s=cabHP.process(s); s=cabLowShelf.process(s); s=cabDepth.process(s); s=cabPresence.process(s); s=cabTopRoll.process(s); }
                ub[k]=rbAmpLvl(0.50f*s); }
            const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Or100Plugin)
};
Plugin* createPlugin(){ return new Or100Plugin(); }
END_NAMESPACE_DISTRHO
