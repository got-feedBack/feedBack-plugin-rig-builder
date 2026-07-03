/* MARSTEN BLUESBREAKER - Marshall 1962 Bluesbreaker (parody). DSP in
 * BluesbreakerCore.h (circuit-real, JTM45-family own core; KT66/5881, dual Loudness
 * combo, jumpered bright+normal). Adds the previously-DEAD tremolo (Speed/Intensity)
 * + a fallback 2x12 combo cab. Mono core -> dual-mono, 2x OS. */
#include "DistrhoPlugin.hpp"
#include "BluesbreakerParams.h"
#include "BluesbreakerCore.h"
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

class BluesbreakerPlugin : public Plugin {
    bluesbreaker::BluesbreakerCore core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    float osr = 96000.0f, baseSr = 48000.0f;
    Bq cabHP, cabLowShelf, cabPresence, cabTopRoll;
    bool cabOn = true;
    float lfoPhase = 0.0f, lfoInc = 0.0f, tremDepth = 0.0f;   // tremolo (was DEAD)

    void applyAll(){
        core.setPresence(fP[kPresence]); core.setBass(fP[kBass]); core.setMiddle(fP[kMiddle]);
        core.setTreble(fP[kTreble]); core.setLoudness1(fP[kLoudness1]); core.setLoudness2(fP[kLoudness2]); core.setInput(fP[kInput]);
        // Tremolo: Speed VR8 -> ~2..9 Hz, Intensity VR7 -> depth (0 = off, the default).
        lfoInc = (2.0f + 7.0f*fP[kSpeed]) / baseSr;
        tremDepth = fP[kIntensity];
        // Fallback 2x12 combo (CabSim): low cut ~85, tame boom, present ~2.8k, roll ~7k.
        cabOn = fP[kCabSim] >= 0.5f;
        cabHP.highpass(osr, 85.0f, 0.70f);
        cabLowShelf.lowShelf(osr, 240.0f, -3.5f);
        cabPresence.peak(osr, 2800.0f, 3.0f, 0.7f);
        cabTopRoll.lowpass(osr, 7000.0f, 0.70f);
    }
public:
    BluesbreakerPlugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kBluesbreakerDef[i];
        baseSr=(float)getSampleRate(); osr=kOS*baseSr; core.setSampleRate(osr); applyAll(); }
protected:
    const char* getLabel() const override { return "MarstenBluesbreaker"; }
    const char* getDescription() const override { return "Marshall 1962 Bluesbreaker style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('B','b','6','2'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kInput||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kBluesbreakerNames[i]; p.symbol=kBluesbreakerSymbols[i]; p.ranges.min=kBluesbreakerMin[i]; p.ranges.max=kBluesbreakerMax[i]; p.ranges.def=kBluesbreakerDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { baseSr=(float)r; osr=kOS*baseSr; core.setSampleRate(osr); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k){ float s=core.process(ub[k]);
                if(cabOn){ s=cabHP.process(s); s=cabLowShelf.process(s); s=cabPresence.process(s); s=cabTopRoll.process(s); }
                ub[k]=rbAmpLvl(0.50f*s); }
            float y=os.downsample(ub);
            if(tremDepth>0.001f){ float lfo=0.5f+0.5f*std::sin(2.0f*kPi*lfoPhase);
                y *= 1.0f - tremDepth*(1.0f-lfo);
                lfoPhase += lfoInc; if(lfoPhase>=1.0f) lfoPhase-=1.0f; }
            oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BluesbreakerPlugin)
};
Plugin* createPlugin(){ return new BluesbreakerPlugin(); }
END_NAMESPACE_DISTRHO
