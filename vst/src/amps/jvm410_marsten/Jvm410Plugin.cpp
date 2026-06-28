/*
 * MARSTEN JVM410 = Marshall JVM410H, 4-channel 100W EL34 head (parody "Marsten").
 * DSP in Jvm410Core.h (OWN circuit-real core — the proven DslCore/Jcm800Core cascade,
 * 4 channels CLEAN/CRUNCH/OD1/OD2 each with a green/orange/red mode, so the OD
 * channels saturate properly instead of the shared core's ~10% THD ceiling).
 * Wires the previously-DEAD Resonance / channel Volume / CabSim. Mono core -> dual.
 *
 * RS: Gain -> GAIN; Channel + Mode pinned per song via _static; Bass/Mid/Treble ->
 * stack, Pres -> Presence. Reverb is host-side (left at 0). Schematic:
 * amps/Marshall JVM410/Marshall_jvm410_sch.pdf.
 */
#include "DistrhoPlugin.hpp"
#include "Jvm410Params.h"
#include "Jvm410Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
class Jvm410Plugin : public Plugin {
    jvm410::Jvm410Core core; float fP[kParamCount];
    rbshared::Oversampler4x os; static constexpr int kOS = rbshared::Oversampler4x::OS;
    void applyAll(){
        float cv=fP[kChannel];
        core.ch   = cv<0.25f?0 : cv<0.5f?1 : cv<0.75f?2 : 3;   // Clean/Crunch/OD1/OD2
        core.mode = fP[kMode] < 0.34f ? 0.0f : (fP[kMode] < 0.67f ? 0.5f : 1.0f); // green/orange/red
        core.pGain=fP[kGain]; core.pVol=fP[kVolume];
        core.pBass=fP[kBass]; core.pMid=fP[kMiddle]; core.pTreble=fP[kTreble];
        core.pPres=fP[kPresence]; core.pReso=fP[kResonance]; core.pMaster=fP[kMaster];
        core.cabOn = fP[kCabSim] >= 0.5f;
        core.recalc();
    }
public:
    Jvm410Plugin() : Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fP[i]=kJvm410Def[i];
        core.setSampleRate(kOS*(float)getSampleRate()); applyAll(); }
protected:
    const char* getLabel() const override { return "MarstenJVM410"; }
    const char* getDescription() const override { return "Marshall JVM410 style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('J','v','4','1'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return; p.hints=kParameterIsAutomatable;
        if(i==(uint32_t)kCabSim)p.hints|=kParameterIsBoolean;
        p.name=kJvm410Names[i]; p.symbol=kJvm410Symbols[i]; p.ranges.min=kJvm410Min[i]; p.ranges.max=kJvm410Max[i]; p.ranges.def=kJvm410Def[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fP[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fP[i]=v; applyAll();} }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override { const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ float ub[kOS]; os.upsample(i0[i],ub);
            for(int k=0;k<kOS;++k) ub[k]=rbAmpLvl(0.50f*core.process(ub[k])); const float y=os.downsample(ub); oL[i]=y; oR[i]=y; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Jvm410Plugin)
};
Plugin* createPlugin(){ return new Jvm410Plugin(); }
END_NAMESPACE_DISTRHO
