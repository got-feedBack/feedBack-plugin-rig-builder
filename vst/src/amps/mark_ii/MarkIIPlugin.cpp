#include "DistrhoPlugin.hpp"
#include "MarkIIParams.h"
#include "MarkIICore.h"   // own circuit-real core: deep lead cascade (the shared core capped at ~10% THD)
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class MarkIIPlugin : public Plugin {
    markii::MarkIICore core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){
        const bool ld = fP[kChannel]>=0.5f;
        core.setChannel(ld?1:0);
        core.setActive(ld?fP[kLeadDrive]:fP[kVolume1], fP[kTreble], fP[kBass], fP[kMiddle], ld?fP[kLeadMaster]:fP[kMaster1]);
        core.setSwitches(fP[kBright1]>=0.5f, fP[kShift]>=0.5f, fP[kGainBoost]>=0.5f, fP[kBrightLead]>=0.5f, fP[kHalfPower]>=0.5f);
    }
public:
    MarkIIPlugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kMarkIIDef[i]; core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "MarkII"; }
    const char* getDescription() const override { return "Mesa Boogie Mark IIB style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,0,1); }
    int64_t getUniqueId() const override { return d_cconst('M','k','2','c'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kChannel||i==(uint32_t)kBright1||i==(uint32_t)kShift||i==(uint32_t)kGainBoost||i==(uint32_t)kBrightLead||i==(uint32_t)kHalfPower||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kMarkIINames[i]; p.symbol=kMarkIISymbols[i]; p.ranges.min=kMarkIIMin[i]; p.ranges.max=kMarkIIMax[i]; p.ranges.def=kMarkIIDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.50f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkIIPlugin)
};
Plugin* createPlugin(){ return new MarkIIPlugin(); }
END_NAMESPACE_DISTRHO
