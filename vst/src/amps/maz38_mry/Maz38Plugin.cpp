#include "DistrhoPlugin.hpp"
#include "Maz38Params.h"
#include "../../_shared/guitar_amp_core.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static constexpr float kMazPi = 3.14159265358979f;
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Small biquad (lowpass / highpass / peak) for the CUT + the fallback 2x12.
struct MazBq {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=y; return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void lowpass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kMazPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al;
        b0=(1-c)*0.5f/a0; b1=(1-c)/a0; b2=(1-c)*0.5f/a0; a1=-2*c/a0; a2=(1-al)/a0; }
    void highpass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kMazPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al;
        b0=(1+c)*0.5f/a0; b1=-(1+c)/a0; b2=(1+c)*0.5f/a0; a1=-2*c/a0; a2=(1-al)/a0; }
    void peak(float sr,float f,float dB,float Q){ if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} if(f>sr*0.49f)f=sr*0.49f;
        float A=std::pow(10.f,dB/40.f),w=2*kMazPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al/A;
        b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
    void lowShelf(float sr,float f,float dB){ if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} if(f<10)f=10;
        float A=std::pow(10.f,dB/40.f),w=2*kMazPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)+(A-1)*c+t; b0=A*((A+1)-(A-1)*c+t)/a0; b1=2*A*((A-1)-(A+1)*c)/a0; b2=A*((A+1)-(A-1)*c-t)/a0; a1=-2*((A-1)+(A+1)*c)/a0; a2=((A+1)+(A-1)*c-t)/a0; }
};

class Maz38Plugin : public Plugin {
    rbgtr::AmpCore<rbtube::TubeEL84> core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    float osr = 96000.0f;                       // oversampled rate (filters run here)
    MazBq cutLP;                                // CUT: post-power treble shunt
    MazBq cabLowCut, cabLowShelf, cabPresence, cabTopRoll;   // fallback 2x12 (CabSim)
    bool cabOn = true;

    void applyAll(){
        // Tone stack = the REAL Dr.Z Maz TMB traced from the Maz 18 Jr schematic:
        // Treble 250k/250pF, Bass 250k/0.1uF, Mid 10k/0.047uF, slope 56k (Fender-style
        // -> the "Vox-meets-Fender" voice). The old config used Marshall-ish values
        // (1M bass / 33k slope / 22n / 22n) which is the wrong family. EL84 power, -7.5V.
        core.configure(250e3,250e3,10e3,56e3, 250e-12,100e-9,47e-9,
                       0.30f, 5.0f, -2.5f, 3000.0f, 4.0f, 650.0f, 0.0f, 8.0f, -7.5f);
        core.setGain(fP[kVolume]);
        core.setBass(fP[kBass]); core.setMiddle(fP[kMiddle]); core.setTreble(fP[kTreble]);
        core.setPresence(0.5f); core.setVolume(fP[kMaster]);

        // CUT (was DEAD): the Vox/Dr.Z treble bleed across the PI/OT — higher = darker.
        // One-pole roll from ~20 kHz (open) down to ~2.5 kHz (full cut).
        const float cut = (fP[kCut]<0?0:(fP[kCut]>1?1:fP[kCut]));
        cutLP.lowpass(osr, 20000.0f * std::pow(0.125f, cut), 0.70f);

        // Fallback 2x12 (CabSim, host bypasses with an external IR): subsonic cut ~82 Hz,
        // a -4 dB low shelf to tame the Fender-stack boom (the bare TMB scoops bass at
        // noon), a +4 dB ~2.6 kHz chime peak (the Dr.Z cut), 2nd-order top roll ~5.3 kHz
        // -> the bright, mid-present, tight Dr.Z tilt rather than dark/boomy.
        cabOn = fP[kCabSim] >= 0.5f;
        cabLowCut.highpass(osr, 92.0f, 0.70f);
        cabLowShelf.lowShelf(osr, 240.0f, -5.0f);
        cabPresence.peak(osr, 2700.0f, 4.5f, 0.9f);
        cabTopRoll.lowpass(osr, 6300.0f, 0.70f);
    }
public:
    Maz38Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kMaz38Def[i];
        osr = kOS*(float)getSampleRate(); core.setSampleRate(osr); applyAll(); }
protected:
    const char* getLabel() const override { return "MrYMaz38"; }
    const char* getDescription() const override { return "MrYMaz38 — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('Y','m','3','8'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kMaz38Names[i]; p.symbol=kMaz38Symbols[i]; p.ranges.min=kMaz38Min[i]; p.ranges.max=kMaz38Max[i]; p.ranges.def=kMaz38Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { osr=kOS*(float)r; core.setSampleRate(osr); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k){ float s=cutLP.process(core.process(ub[k]));
                if(cabOn){ s=cabLowCut.process(s); s=cabLowShelf.process(s); s=cabPresence.process(s); s=cabTopRoll.process(s); }
                ub[k]=rbAmpLvl(0.50f*s); }
            const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Maz38Plugin)
};
Plugin* createPlugin(){ return new Maz38Plugin(); }
END_NAMESPACE_DISTRHO
