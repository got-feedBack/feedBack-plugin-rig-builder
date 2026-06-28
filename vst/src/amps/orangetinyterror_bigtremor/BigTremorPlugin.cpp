/*
 * CITRUS BIG TREMOR = Orange Tiny Terror, single-channel ~15W EL84 head (parody
 * "Citrus"). DSP via the OWN citrus::CitrusCore<EL84> — a cascade + driven EL84
 * power amp that generates the real Orange grind (the shared core's 1-stage clip
 * compressed but lacked the high harmonics = "needs more gain at max"). Panel:
 * Volume (master) · Tone · Gain (preamp drive) · Half (7W) · Cab Sim.
 * RS Gain->Gain, RS Tone->Tone.
 */
#include "DistrhoPlugin.hpp"
#include "BigTremorParams.h"
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

class BigTremorPlugin : public Plugin {
    citrus::CitrusCore<rbtube::PowerAmpPP> core; float fP[kParamCount];   // 2x EL84
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    float osr = 96000.0f;
    Bq cabHP, cabLowShelf, cabPresence, cabTopRoll;
    bool cabOn = true;

    void cfg(){
        core.setTone(250e3, 470e3, 25e3, 33e3, 250e-12, 22e-9, 22e-9);
        core.cfgDirtySpan = 7.5f; core.cfgNStages = 2; core.cfgHpDirty = 80.0f;
        core.cfgTilt = 6.0f; core.cfgScoop = -1.5f; core.cfgBias = -7.5f;   // EL84 op-point
    }
    void applyAll(){
        const bool half = fP[kHalf] >= 0.5f;     // 7W = one EL84 pair, earlier breakup
        core.dirty = true;
        core.pGain=fP[kGain]; core.pBass=0.5f; core.pMid=0.5f; core.pTreble=fP[kTone];
        core.pPres=0.5f; core.pVol=fP[kVolume];
        core.inBoost   = half ? 1.3f : 1.0f;                 // 7W breaks up earlier
        core.cfgPwDrive = half ? 5.0f : 3.5f;                // 7W slams the EL84 harder
        core.cfgOt      = half ? 9000.0f : 11000.0f;         // 7W softer/darker
        core.cfgMkDirty = (half ? -7.5f : -6.0f);            // ~-16 dBFS (dark cab loses level)
        core.recalc();

        // The REAL Tiny Terror is DARK + heavily mid-scooped + tight (ref tinyterr_gain_*:
        // hm ~-6.5, hi ~-19, lo ~0, crest ~8) — NOT bright. Dark cab + a deep mid scoop.
        cabOn = fP[kCabSim] >= 0.5f;
        cabHP.highpass(osr, 120.0f, 0.70f);                  // tight lows (ref lo ~0)
        cabLowShelf.lowShelf(osr, 300.0f, -7.0f);
        cabPresence.peak(osr, 1600.0f, -4.0f, 0.9f);         // mid SCOOP (ref hm ~-6.5)
        cabTopRoll.lowpass(osr, half ? 2900.0f : 3300.0f, 0.70f);   // DARK (ref hi ~-19)
    }
public:
    BigTremorPlugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kBigTremorDef[i];
        osr=kOS*(float)getSampleRate(); core.setSampleRate(osr); cfg(); applyAll(); }
protected:
    const char* getLabel() const override { return "CitrusBigTremor"; }
    const char* getDescription() const override { return "CitrusBigTremor — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,2,0); }
    int64_t getUniqueId() const override { return d_cconst('R','B','B','T'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kHalf||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kBigTremorNames[i]; p.symbol=kBigTremorSymbols[i]; p.ranges.min=kBigTremorMin[i]; p.ranges.max=kBigTremorMax[i]; p.ranges.def=kBigTremorDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { osr=kOS*(float)r; core.setSampleRate(osr); os.reset(); cfg(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k){ float s=core.process(ub[k]);
                if(cabOn){ s=cabHP.process(s); s=cabLowShelf.process(s); s=cabPresence.process(s); s=cabTopRoll.process(s); }
                ub[k]=rbAmpLvl(0.50f*s); }
            const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BigTremorPlugin)
};
Plugin* createPlugin(){ return new BigTremorPlugin(); }
END_NAMESPACE_DISTRHO
