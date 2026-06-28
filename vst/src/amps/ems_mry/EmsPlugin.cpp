/*
 * MrY EMS - Dr.Z EMS = a JCM800 2204 circuit with a HI/LO gain switch (parody "Mr Y").
 * Built on the calibrated jcm800::Jcm800Core (real 2204 saturation + voicing) so it
 * matches the Marsten JCM800 family exactly, NOT the lighter shared core. Per the
 * build reference (Hoffman fwd thread, proguitar.de/ems):
 *   HI = full JCM800 2204 gain.
 *   LO = JTM50 levels: a 33k-to-ground after the 68k input (~6 dB input divider) AND
 *        the 0.68uF cathode-bypass LIFTED off the V2a 820R cathode (less stage gain).
 *        Modelled as a 0.5x input pre-divider + a scaled-down Gain into the core.
 * Adds the previously-DEAD HI/LO + a host-bypassable fallback 4x12. Mono core -> dual.
 */
#include "DistrhoPlugin.hpp"
#include "EmsParams.h"
#include "../jcm800_marsten/Jcm800Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static constexpr float kEmsPi = 3.14159265358979f;
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

struct EmsBq {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=y; return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void lowpass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kEmsPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al;
        b0=(1-c)*0.5f/a0; b1=(1-c)/a0; b2=(1-c)*0.5f/a0; a1=-2*c/a0; a2=(1-al)/a0; }
    void highpass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kEmsPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al;
        b0=(1+c)*0.5f/a0; b1=-(1+c)/a0; b2=(1+c)*0.5f/a0; a1=-2*c/a0; a2=(1-al)/a0; }
    void peak(float sr,float f,float dB,float Q){ if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} if(f>sr*0.49f)f=sr*0.49f;
        float A=std::pow(10.f,dB/40.f),w=2*kEmsPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al/A;
        b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
    void lowShelf(float sr,float f,float dB){ if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} if(f<10)f=10;
        float A=std::pow(10.f,dB/40.f),w=2*kEmsPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)+(A-1)*c+t; b0=A*((A+1)-(A-1)*c+t)/a0; b1=2*A*((A-1)-(A+1)*c)/a0; b2=A*((A+1)-(A-1)*c-t)/a0; a1=-2*((A-1)+(A+1)*c)/a0; a2=((A+1)+(A-1)*c-t)/a0; }
};

class EmsPlugin : public Plugin {
    jcm800::Jcm800Core core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    float osr = 96000.0f;
    float inDiv = 1.0f, loMakeup = 1.0f;        // HI/LO input divider + level match
    EmsBq cabLowCut, cabLowShelf, cabPresence, cabTopRoll;   // fallback 4x12 (CabSim)
    bool cabOn = true;

    void applyAll(){
        const bool lo = fP[kHiLo] >= 0.5f;
        // HI = full JCM800 2204; LO = JTM50: input /2 (68k/33k divider) + the Gain into
        // the core scaled to ~0.5 (the V2a cathode un-bypass drops the 2nd-stage drive).
        inDiv    = lo ? 0.5f : 1.0f;
        loMakeup = lo ? 1.45f : 1.0f;           // restore most of the dB the input pad removes
                                                // (a saturating stage recovers some on its own)
        core.setGain(lo ? fP[kGain]*0.5f : fP[kGain]);
        core.setBass(fP[kBass]); core.setMiddle(fP[kMiddle]); core.setTreble(fP[kTreble]);
        core.setPresence(fP[kPresence]); core.setVolume(fP[kVolume]);

        // Fallback Marshall 4x12 (CabSim, host bypasses with an external IR). The core
        // is amp-only/bright (otVoice 16k, +9 tilt); the cab rolls the top to a realistic
        // G12-voiced 4x12 (hi ~-13 dB vs the bare amp's -9): low cut 80, -3 dB low shelf,
        // +3 dB ~3.5 kHz push, 2nd-order roll ~5.6 kHz.
        cabOn = fP[kCabSim] >= 0.5f;
        cabLowCut.highpass(osr, 80.0f, 0.70f);
        cabLowShelf.lowShelf(osr, 220.0f, -3.0f);
        cabPresence.peak(osr, 3500.0f, 3.0f, 0.8f);
        cabTopRoll.lowpass(osr, 5600.0f, 0.70f);
    }
public:
    EmsPlugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kEmsDef[i];
        osr=kOS*(float)getSampleRate(); core.setSampleRate(osr); applyAll(); }
protected:
    const char* getLabel() const override { return "MrYEMS"; }
    const char* getDescription() const override { return "JCM800-style Marshall — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('Y','e','m','s'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kHiLo||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kEmsNames[i]; p.symbol=kEmsSymbols[i]; p.ranges.min=kEmsMin[i]; p.ranges.max=kEmsMax[i]; p.ranges.def=kEmsDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { osr=kOS*(float)r; core.setSampleRate(osr); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k){ float s=core.process(inDiv*ub[k])*loMakeup;
                if(cabOn){ s=cabLowCut.process(s); s=cabLowShelf.process(s); s=cabPresence.process(s); s=cabTopRoll.process(s); }
                ub[k]=rbAmpLvl(0.50f*s); }
            const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EmsPlugin)
};
Plugin* createPlugin(){ return new EmsPlugin(); }
END_NAMESPACE_DISTRHO
