/*
 * LOVOLT DR504 - early-70s Hiwatt DR504 Custom 50 circuit model.
 * Source: local hwpre1/hwpsu2/hwpwr50w schematics and the Mark Huss layout.
 * The preamp drawing is byte-identical to the DR103 source, while this model
 * keeps the DR504's 2xEL34 output stage, -36V bias and Partridge 50W OT.
 */
#include "DistrhoPlugin.hpp"
#include "Dr504Params.h"
#include "Dr504Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=.90f,c=.99f,a=std::fabs(x);
    if(a<=t)return x; return std::copysign(t+(c-t)*std::tanh((a-t)/(c-t)),x); }

class Dr504Plugin : public Plugin {
    dr504::Dr504Core core;
    float params[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS=rbshared::Oversampler4x::OS;

    void applyAll(){
        core.setNormal(params[kNormalVol]); core.setBrilliant(params[kBrightVol]);
        core.setBass(params[kBass]); core.setTreble(params[kTreble]); core.setMiddle(params[kMiddle]);
        core.setPresence(params[kPresence]); core.setMaster(params[kMaster]);
        core.setInput(params[kInput]); core.setCabSim(params[kCabSim]);
    }
public:
    Dr504Plugin():Plugin(kParamCount,0,0){
        for(int i=0;i<kParamCount;++i)params[i]=kDr504Def[i];
        core.setSampleRate(kOS*(float)getSampleRate()); applyAll();
    }
protected:
    const char* getLabel() const override{return "LovoltDR504";}
    const char* getDescription() const override{return "Hiwatt DR504 Custom 50 circuit model";}
    const char* getMaker() const override{return "RigBuilder";}
    const char* getLicense() const override{return "ISC";}
    uint32_t getVersion() const override{return d_version(2,0,0);}
    int64_t getUniqueId() const override{return d_cconst('L','v','5','0');}
    void initParameter(uint32_t i,Parameter& p) override{
        if(i>=(uint32_t)kParamCount)return;
        p.hints=kParameterIsAutomatable; if(i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kDr504Names[i];p.symbol=kDr504Symbols[i];
        p.ranges.min=kDr504Min[i];p.ranges.max=kDr504Max[i];p.ranges.def=kDr504Def[i];
    }
    float getParameterValue(uint32_t i) const override{return i<(uint32_t)kParamCount?params[i]:0.0f;}
    void setParameterValue(uint32_t i,float v) override{if(i<(uint32_t)kParamCount){params[i]=dr504::clamp01(v);applyAll();}}
    void sampleRateChanged(double s) override{core.setSampleRate(kOS*(float)s);os.reset();applyAll();}
    void run(const float** in,float** out,uint32_t frames) override{
        const float* src=in[0];float* l=out[0];float* r=out[1];const float makeup=core.outputMakeup();
        for(uint32_t i=0;i<frames;++i){
            float ub[kOS];os.upsample(2.2f*src[i],ub);
            for(int k=0;k<kOS;++k)ub[k]=rbAmpLvl(.58f*core.process(ub[k])*makeup);
            const float y=os.downsample(ub);l[i]=y;r[i]=y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dr504Plugin)
};

Plugin* createPlugin(){return new Dr504Plugin();}
END_NAMESPACE_DISTRHO
