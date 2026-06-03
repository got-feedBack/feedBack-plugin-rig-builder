/* StereoPhaser — 6-stage stereo phaser for Rack_StereoPhaser.
 * All-pass chain swept by an LFO (Rate Hz), L/R LFOs offset 90° for width.
 *   Rate -> 0.1..6 Hz   Depth -> sweep range   Mix -> dry/wet (notches at 50/50) */
#include "DistrhoPlugin.hpp"
#include "StereoPhaserParams.h"
#include <cmath>
START_NAMESPACE_DISTRHO
static const int kStages = 6;
class PhaserCh {
    float fs = 48000.f; float xz[kStages] = {0}, yz[kStages] = {0};
    float baseFc = 400.f, depthOct = 1.5f, mix = 0.5f;
public:
    void setSampleRate(float s){ fs=(s>0.f)?s:48000.f; }
    void reset(){ for(int i=0;i<kStages;++i){xz[i]=yz[i]=0.f;} }
    void setParams(float depth, float mixP){ depthOct=0.3f+depth*2.3f; mix=mixP*0.5f; baseFc=300.f; }
    inline float process(float x, float lfo){
        float fc = baseFc * std::pow(2.0f, lfo*depthOct);
        if(fc<40.f)fc=40.f; const float nyq=fs*0.45f; if(fc>nyq)fc=nyq;
        const float t=std::tan(3.14159265f*fc/fs); const float a=(t-1.0f)/(t+1.0f);
        float s=x;
        for(int i=0;i<kStages;++i){ float in=s; s=a*in+xz[i]-a*yz[i]; xz[i]=in; yz[i]=s; }
        return x*(1.0f-mix)+s*mix*2.0f;
    }
};
class StereoPhaserPlugin : public Plugin {
    PhaserCh L,R; float lfoPhase=0.f, lfoInc=0.f; float fParams[kParamCount];
    void recalc(){ lfoInc=6.2831853f*(0.1f+fParams[kRate]*5.9f)/(float)getSampleRate();
        L.setParams(fParams[kDepth],fParams[kMix]); R.setParams(fParams[kDepth],fParams[kMix]); }
public:
    StereoPhaserPlugin():Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fParams[i]=kStereoPhaserDef[i];
        L.setSampleRate((float)getSampleRate());R.setSampleRate((float)getSampleRate());L.reset();R.reset();recalc(); }
protected:
    const char* getLabel() const override { return "StereoPhaser"; }
    const char* getDescription() const override { return "Stereo phaser"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,0,0); }
    int64_t getUniqueId() const override { return d_cconst('R','P','h','1'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return;
        p.hints=kParameterIsAutomatable; p.name=kStereoPhaserNames[i]; p.symbol=kStereoPhaserSymbols[i];
        p.ranges.min=kStereoPhaserMin[i]; p.ranges.max=kStereoPhaserMax[i]; p.ranges.def=kStereoPhaserDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fParams[i]=v;recalc();} }
    void sampleRateChanged(double) override { L.setSampleRate((float)getSampleRate());R.setSampleRate((float)getSampleRate());recalc(); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0];const float* iR=in[1];float* oL=out[0];float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ lfoPhase+=lfoInc; if(lfoPhase>6.2831853f)lfoPhase-=6.2831853f;
            oL[i]=L.process(iL[i],std::sin(lfoPhase)); oR[i]=R.process(iR[i],std::sin(lfoPhase+1.5708f)); } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoPhaserPlugin)
};
Plugin* createPlugin(){ return new StereoPhaserPlugin(); }
END_NAMESPACE_DISTRHO
