#include "DistrhoPlugin.hpp"
#include "EngelFireballParams.h"
#include "EngelFireballCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class EngelFireballPlugin : public Plugin {
    engel::EngelFireballCore core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){
        core.lead     = fP[kChannel]  >= 0.5f;
        core.bright   = fP[kBright]   >= 0.5f;   // were DEAD
        core.bottom   = fP[kBottom]   >= 0.5f;
        core.midBoost = fP[kMidBoost] >= 0.5f;
        core.cabOn    = fP[kCabSim]   >= 0.5f;
        core.pCleanGain=fP[kCleanGain]; core.pLeadGain=fP[kLeadGain];
        core.pBass=fP[kBass]; core.pMid=fP[kMiddle]; core.pTreble=fP[kTreble];
        core.pPres=fP[kPresence]; core.pLeadVol=fP[kLeadVolume]; core.pMaster=fP[kMaster];
        core.recalc();
    }
public:
    EngelFireballPlugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kEngelFireballDef[i]; core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "EngelFireball"; }
    const char* getDescription() const override { return "EngelFireball — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('E','g','f','b'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kChannel||i==(uint32_t)kBright||i==(uint32_t)kBottom||i==(uint32_t)kMidBoost||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kEngelFireballNames[i]; p.symbol=kEngelFireballSymbols[i]; p.ranges.min=kEngelFireballMin[i]; p.ranges.max=kEngelFireballMax[i]; p.ranges.def=kEngelFireballDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.50f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngelFireballPlugin)
};
Plugin* createPlugin(){ return new EngelFireballPlugin(); }
END_NAMESPACE_DISTRHO
