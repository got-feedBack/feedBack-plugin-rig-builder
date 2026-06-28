/* MARSTEN BLUESBREAKER - Marshall 1962 Bluesbreaker (parody). DSP in
 * BluesbreakerCore.h (circuit-real, controlled gain; 5881, dual Loudness combo).
 * Mono core -> dual-mono, 2x OS. */
#include "DistrhoPlugin.hpp"
#include "BluesbreakerParams.h"
#include "BluesbreakerCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class BluesbreakerPlugin : public Plugin {
    bluesbreaker::BluesbreakerCore core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){ core.setPresence(fP[kPresence]); core.setBass(fP[kBass]); core.setMiddle(fP[kMiddle]);
        core.setTreble(fP[kTreble]); core.setLoudness1(fP[kLoudness1]); core.setLoudness2(fP[kLoudness2]); core.setInput(fP[kInput]); }
public:
    BluesbreakerPlugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kBluesbreakerDef[i]; core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "MarstenBluesbreaker"; }
    const char* getDescription() const override { return "Marshall 1962 Bluesbreaker style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,0,0); }
    int64_t getUniqueId() const override { return d_cconst('B','b','6','2'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kInput||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kBluesbreakerNames[i]; p.symbol=kBluesbreakerSymbols[i]; p.ranges.min=kBluesbreakerMin[i]; p.ranges.max=kBluesbreakerMax[i]; p.ranges.def=kBluesbreakerDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.50f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BluesbreakerPlugin)
};
Plugin* createPlugin(){ return new BluesbreakerPlugin(); }
END_NAMESPACE_DISTRHO
