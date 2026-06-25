/*
 * DualRect - Mesa/Boogie 3-Channel Dual Rectifier Solo Head for the game's
 * Amp_CA100. DPF/VST3 wrapper; DSP in DualRectCore.h (circuit-real on the shared
 * framework, CONTROLLED gain staging per channel). Rewritten from the over-gained
 * 6-stage cascade that saturated every signal to ~100% THD.
 *
 * STEREO I/O, single mono core -> both outputs (dual-mono); 2x oversampling.
 */
#include "DistrhoPlugin.hpp"
#include "DualRectParams.h"
#include "DualRectCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

static constexpr float kDualRectMakeup = 0.50f;

class DualRectPlugin : public Plugin {
    dualrect::DualRectCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll(){
        const float chv = fParams[kChannel];
        const int ch = (chv < 0.34f) ? 0 : (chv < 0.67f) ? 1 : 2;   // Green / Orange / Red
        const int base = (ch==0) ? kC1Gain : (ch==1) ? kC2Gain : kC3Gain;
        core.setChannel(ch);
        core.setActive(fParams[base+0], fParams[base+1], fParams[base+2], fParams[base+3],
                       fParams[base+4], fParams[base+5], fParams[kOutput], fParams[kRectifier]);
    }
public:
    DualRectPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kDualRectDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }
protected:
    const char* getLabel() const override { return "DualRect"; }
    const char* getDescription() const override { return "Mesa Boogie Dual Rectifier style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', 'R', 'C', 'T'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == (uint32_t)kCabSim) p.hints |= kParameterIsBoolean;
        p.name = kDualRectNames[i]; p.symbol = kDualRectSymbols[i];
        p.ranges.min = kDualRectMin[i]; p.ranges.max = kDualRectMax[i]; p.ranges.def = kDualRectDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if (i<(uint32_t)kParamCount){ fParams[i]=v; applyAll(); } }
    void sampleRateChanged(double r) override { core.setSampleRate(kOS*(float)r); os.reset(); applyAll(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* i0 = in[0];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i=0;i<frames;++i){
            float ub[kOS];
            os.upsample(i0[i], ub);
            for (int k=0;k<kOS;++k)
                ub[k] = rbAmpLvl(kDualRectMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DualRectPlugin)
};

Plugin* createPlugin() { return new DualRectPlugin(); }

END_NAMESPACE_DISTRHO
