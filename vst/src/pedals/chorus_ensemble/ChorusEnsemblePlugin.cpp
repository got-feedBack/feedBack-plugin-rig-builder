/*
 * ChorusEnsemble - Boss CE-1 Chorus Ensemble (parody; the in-app face never
 * reads "Boss"). Reference: pedals/Boss CE-1 service notes (ET-10D).
 *
 * DSP in ChorusEnsembleCore.h — MN3002 BBD chorus/vibrato with the CE-1's
 * signature STEREO "Ensemble" spread. One mono core drives the switched mono
 * effect output and the separate direct stereo output. Base sample rate
 * (band-limited chorus — no oversampling needed).
 *
 * EXTRA gear — not mapped to any RS song.
 */
#include "DistrhoPlugin.hpp"
#include "ChorusEnsembleParams.h"
#include "ChorusEnsembleCore.h"
#include "../../_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float clamp01(float v){ return v<0.0f?0.0f:(v>1.0f?1.0f:v); }

class ChorusEnsemblePlugin : public Plugin {
    chorusensemble::ChorusEnsembleCore core;   // one core -> true stereo out
    float params[kParamCount];

    void applyAll(){
        core.setLevel(params[kLevel]);
        core.setIntensity(params[kIntensity]);
        core.setDepth(params[kDepth]);
        core.setRate(params[kRate]);
        core.setMode(params[kMode]);
        core.setEffect(params[kEffect]);
        core.setInputSens(params[kInputSens]);
    }

public:
    ChorusEnsemblePlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) params[i]=kChorusEnsembleDef[i];
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ChorusEnsemble"; }
    const char* getDescription() const override { return "Boss CE-1 Chorus Ensemble style BBD chorus/vibrato"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,2,0); }
    int64_t getUniqueId() const override { return d_cconst('C','E','1','x'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i>=(uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i==(uint32_t)kMode || i==(uint32_t)kEffect || i==(uint32_t)kInputSens) p.hints |= kParameterIsBoolean;
        p.name = kChorusEnsembleNames[i]; p.symbol = kChorusEnsembleSymbols[i];
        p.ranges.min = kChorusEnsembleMin[i]; p.ranges.max = kChorusEnsembleMax[i]; p.ranges.def = kChorusEnsembleDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?params[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if (i<(uint32_t)kParamCount){ params[i]=clamp01(v); applyAll(); } }
    void sampleRateChanged(double r) override { core.setSampleRate((float)r); applyAll(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0]; const float* iR=in[1];
        float* oL=out[0]; float* oR=out[1];
        for (uint32_t i=0;i<frames;++i){
            const rbmod::StereoInputPair f = rbmod::stereoPedalFeeds(iL[i], iR[i]);
            const float mono = 0.5f*(f.left + f.right);
            float l, r; core.process(mono, l, r);
            oL[i]=l; oR[i]=r;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChorusEnsemblePlugin)
};

Plugin* createPlugin() { return new ChorusEnsemblePlugin(); }

END_NAMESPACE_DISTRHO
