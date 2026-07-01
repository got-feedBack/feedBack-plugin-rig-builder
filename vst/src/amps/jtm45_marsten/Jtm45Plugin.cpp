/*
 * MARSTEN JTM45 - Marshall JTM45 for the game's Amp_MarshallJTM45. Parody brand.
 * DSP in Jtm45Core.h (circuit-real, controlled gain staging; 5881 power, dual
 * Loudness, non-master). Rewritten from the over-gained cascade. Mono core ->
 * dual-mono, 2x oversampled.
 */
#include "DistrhoPlugin.hpp"
#include "Jtm45Params.h"
#include "Jtm45Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
static constexpr float kMakeup = 0.436f;
class Jtm45Plugin : public Plugin {
    jtm45::Jtm45Core core; float fParams[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){ core.setPresence(fParams[kPresence]); core.setBass(fParams[kBass]); core.setMiddle(fParams[kMiddle]);
        core.setTreble(fParams[kTreble]); core.setLoudness1(fParams[kLoudness1]); core.setLoudness2(fParams[kLoudness2]); core.setInput(fParams[kInput]); }
public:
    Jtm45Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fParams[i]=kJtm45Def[i]; core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "MarstenJTM45"; }
    const char* getDescription() const override { return "Marshall JTM45 style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,0,1); }
    int64_t getUniqueId() const override { return d_cconst('J','t','4','5'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kInput||i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kJtm45Names[i]; p.symbol=kJtm45Symbols[i]; p.ranges.min=kJtm45Min[i]; p.ranges.max=kJtm45Max[i]; p.ranges.def=kJtm45Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fParams[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(kMakeup*core.process(ub[k]));
            const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Jtm45Plugin)
};
Plugin* createPlugin(){ return new Jtm45Plugin(); }
END_NAMESPACE_DISTRHO
