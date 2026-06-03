/* StudioWahFilter — auto/envelope wah filter for Rack_StudioWahFilter.
 * A resonant TPT band-pass swept by the input envelope (Sens, with Attack/
 * Release times). With Auto off it sits at the manual Pedal position.
 *   Sens -> envelope depth   Attack/Release -> 1..200 / 20..800 ms
 *   Pedal -> manual position   Auto -> envelope on/off */
#include "DistrhoPlugin.hpp"
#include "StudioWahFilterParams.h"
#include <cmath>
#include "../_shared/automakeup.hpp"
START_NAMESPACE_DISTRHO
class WahCh {
    float fs=48000.f; float ic1=0.f,ic2=0.f,env=0.f,atk=0.f,rel=0.f;
    bool autoOn=true; float sens=0.7f,pedal=0.3f; const float Q=3.4f;
    static inline float ms(float m,float fs){ return std::exp(-1.0f/(0.001f*m*fs)); }
public:
    void setSampleRate(float s){ fs=(s>0.f)?s:48000.f; }
    void reset(){ ic1=ic2=env=0.f; }
    void setParams(float sensP,float attackP,float releaseP,float pedalP,float autoP){
        sens=sensP; pedal=pedalP; autoOn=autoP>0.5f;
        atk=ms(1.0f+attackP*200.0f,fs); rel=ms(20.0f+releaseP*780.0f,fs);
    }
    inline float process(float x){
        const float a=std::fabs(x); const float c=(a>env)?atk:rel; env=c*env+(1.0f-c)*a;
        float e=env*3.0f; if(e>1.f)e=1.f;
        float pos = autoOn ? (0.12f+0.75f*sens*e+0.10f) : (pedal*0.7f+0.3f*sens*e);
        if(pos<0.f)pos=0.f; if(pos>1.f)pos=1.f;
        float fc=120.0f*std::pow(14.0f,pos);   // 120 .. ~1700 Hz
        const float nyq=fs*0.45f; if(fc>nyq)fc=nyq;
        const float g=std::tan(3.14159265f*fc/fs); const float k=1.0f/Q;
        const float a1=1.0f/(1.0f+g*(g+k)); const float a2=g*a1;
        const float v3=x-ic2; const float v1=a1*ic1+a2*v3; const float v2=ic2+a2*ic1+g*a2*v3;
        ic1=2.0f*v1-ic1; ic2=2.0f*v2-ic2;
        return v1*k*1.8f*0.9f + x*0.12f;
    }
};
class StudioWahFilterPlugin : public Plugin {
    WahCh L,R; RBAutoMakeup makeupL, makeupR; float fParams[kParamCount];
    void recalc(){ L.setParams(fParams[kSens],fParams[kAttack],fParams[kRelease],fParams[kPedal],fParams[kAuto]);
        R.setParams(fParams[kSens],fParams[kAttack],fParams[kRelease],fParams[kPedal],fParams[kAuto]); }
public:
    StudioWahFilterPlugin():Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fParams[i]=kStudioWahFilterDef[i];
        L.setSampleRate((float)getSampleRate());R.setSampleRate((float)getSampleRate());makeupL.setSampleRate((float)getSampleRate());makeupR.setSampleRate((float)getSampleRate());L.reset();R.reset();recalc(); }
protected:
    const char* getLabel() const override { return "StudioWahFilter"; }
    const char* getDescription() const override { return "Auto wah filter"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,0,0); }
    int64_t getUniqueId() const override { return d_cconst('R','W','f','1'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return;
        p.hints=kParameterIsAutomatable; if(i==kAuto)p.hints|=kParameterIsBoolean;
        p.name=kStudioWahFilterNames[i]; p.symbol=kStudioWahFilterSymbols[i];
        p.ranges.min=kStudioWahFilterMin[i]; p.ranges.max=kStudioWahFilterMax[i]; p.ranges.def=kStudioWahFilterDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fParams[i]=v;recalc();makeupL.snap();makeupR.snap();} }
    void sampleRateChanged(double) override { L.setSampleRate((float)getSampleRate());R.setSampleRate((float)getSampleRate());makeupL.setSampleRate((float)getSampleRate());makeupR.setSampleRate((float)getSampleRate());recalc(); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0];const float* iR=in[1];float* oL=out[0];float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ oL[i]=makeupL.process(iL[i], L.process(iL[i])); oR[i]=makeupR.process(iR[i], R.process(iR[i])); } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioWahFilterPlugin)
};
Plugin* createPlugin(){ return new StudioWahFilterPlugin(); }
END_NAMESPACE_DISTRHO
