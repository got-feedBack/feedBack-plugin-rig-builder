/*
 * LOVOLT DR103 - early-70s Hiwatt DR103 Custom 100 circuit model.
 * Source: local hwpre1/hwpsu1/hwpwr100w schematics plus Mark Huss' corrected
 * early four-input drawings. The panel keeps the Lovolt parody brand.
 */
#include "DistrhoPlugin.hpp"
#include "Dr103Params.h"
#include "Dr103Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=.90f,c=.99f,a=std::fabs(x);
    if(a<=t)return x; return std::copysign(t+(c-t)*std::tanh((a-t)/(c-t)),x); }

class Dr103Plugin : public Plugin {
    dr103::Dr103Core core;
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
    Dr103Plugin():Plugin(kParamCount,0,0){
        for(int i=0;i<kParamCount;++i)params[i]=kDr103Def[i];
        core.setSampleRate(kOS*(float)getSampleRate()); applyAll();
    }
protected:
    const char* getLabel() const override{return "LovoltDR103";}
    const char* getDescription() const override{return "Hiwatt DR103 Custom 100 circuit model";}
    const char* getMaker() const override{return "RigBuilder";}
    const char* getLicense() const override{return "ISC";}
    uint32_t getVersion() const override{return d_version(2,0,0);}
    int64_t getUniqueId() const override{return d_cconst('L','d','0','3');}
    void initParameter(uint32_t i,Parameter& p) override{
        if(i>=(uint32_t)kParamCount)return;
        p.hints=kParameterIsAutomatable; if(i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kDr103Names[i];p.symbol=kDr103Symbols[i];
        p.ranges.min=kDr103Min[i];p.ranges.max=kDr103Max[i];p.ranges.def=kDr103Def[i];
    }
    float getParameterValue(uint32_t i) const override{return i<(uint32_t)kParamCount?params[i]:0.0f;}
    void setParameterValue(uint32_t i,float v) override{if(i<(uint32_t)kParamCount){params[i]=dr103::clamp01(v);applyAll();}}
    void sampleRateChanged(double s) override{core.setSampleRate(kOS*(float)s);os.reset();applyAll();}
    void run(const float** in,float** out,uint32_t frames) override{
        const float* src=in[0];float* l=out[0];float* r=out[1];const float makeup=core.outputMakeup();
        for(uint32_t i=0;i<frames;++i){
            float ub[kOS];os.upsample(2.2f*src[i],ub);
            for(int k=0;k<kOS;++k)ub[k]=rbAmpLvl(.58f*core.process(ub[k])*makeup);
            const float y=os.downsample(ub);l[i]=y;r[i]=y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dr103Plugin)
};

Plugin* createPlugin(){return new Dr103Plugin();}
END_NAMESPACE_DISTRHO
