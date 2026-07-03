/* SynthFilter — envelope-following resonant filterbank for Rack_SynthFilter.
 * A TPT state-variable filter swept by the input envelope; FilterType crossfades
 * low-pass → band-pass → high-pass; Mix blends dry/wet.
 *   Sens -> envelope depth   Attack/Release -> envelope   Type -> LP/BP/HP */
#include "DistrhoPlugin.hpp"
#include "SynthFilterParams.h"
#include <cmath>
START_NAMESPACE_DISTRHO
class SynthCh {
    float fs=48000.f; float ic1=0.f,ic2=0.f,env=0.f,atk=0.f,rel=0.f;
    float sens=0.7f,mix=0.6f,type=0.f; const float Q=4.0f;
    static inline float ms(float m,float fs){ return std::exp(-1.0f/(0.001f*m*fs)); }
public:
    void setSampleRate(float s){ fs=(s>0.f)?s:48000.f; }
    void reset(){ ic1=ic2=env=0.f; }
    void setParams(float sensP,float attackP,float releaseP,float typeP,float mixP){
        sens=sensP; mix=mixP; type=typeP;
        atk=ms(1.0f+attackP*150.0f,fs); rel=ms(20.0f+releaseP*780.0f,fs);
    }
    inline float process(float x){
        const float a=std::fabs(x); const float c=(a>env)?atk:rel; env=c*env+(1.0f-c)*a;
        float e=env*3.0f; if(e>1.f)e=1.f;
        float pos=0.10f+0.85f*sens*e; if(pos>1.f)pos=1.f;
        float fc=120.0f*std::pow(20.0f,pos);   // 120 .. 2400 Hz
        const float nyq=fs*0.45f; if(fc>nyq)fc=nyq;
        const float g=std::tan(3.14159265f*fc/fs); const float k=1.0f/Q;
        const float a1=1.0f/(1.0f+g*(g+k)); const float a2=g*a1;
        const float v3=x-ic2; const float bp=a1*ic1+a2*v3; const float v2=ic2+a2*ic1+g*a2*v3;
        ic1=2.0f*bp-ic1; ic2=2.0f*v2-ic2;
        const float lp=v2; const float hp=x-k*bp-lp;
        // Type 0..1 → LP(0) .. BP(0.5) .. HP(1)
        float wet;
        if(type<0.5f){ float t=type*2.0f; wet=lp*(1.0f-t)+bp*k*1.8f*t; }
        else        { float t=(type-0.5f)*2.0f; wet=bp*k*1.8f*(1.0f-t)+hp*t; }
        return x*(1.0f-mix)+wet*mix;
    }
};
class SynthFilterPlugin : public Plugin {
    SynthCh L,R; float fParams[kParamCount];
    void recalc(){ L.setParams(fParams[kSens],fParams[kAttack],fParams[kRelease],fParams[kFilterType],fParams[kMix]);
        R.setParams(fParams[kSens],fParams[kAttack],fParams[kRelease],fParams[kFilterType],fParams[kMix]); }
public:
    SynthFilterPlugin():Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fParams[i]=kSynthFilterDef[i];
        L.setSampleRate((float)getSampleRate());R.setSampleRate((float)getSampleRate());L.reset();R.reset();recalc(); }
protected:
    const char* getLabel() const override { return "SynthFilter"; }
    const char* getDescription() const override { return "Envelope filterbank"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,0,0); }
    int64_t getUniqueId() const override { return d_cconst('R','S','f','1'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return;
        p.hints=kParameterIsAutomatable; p.name=kSynthFilterNames[i]; p.symbol=kSynthFilterSymbols[i];
        p.ranges.min=kSynthFilterMin[i]; p.ranges.max=kSynthFilterMax[i]; p.ranges.def=kSynthFilterDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fParams[i]=v;recalc();} }
    void sampleRateChanged(double) override { L.setSampleRate((float)getSampleRate());R.setSampleRate((float)getSampleRate());recalc(); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0];const float* iR=in[1];float* oL=out[0];float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ oL[i]=L.process(iL[i]); oR[i]=R.process(iR[i]); } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthFilterPlugin)
};
Plugin* createPlugin(){ return new SynthFilterPlugin(); }
END_NAMESPACE_DISTRHO
