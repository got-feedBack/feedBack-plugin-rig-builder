/*
 * TurboDistortion - component-guided Boss DS-2 Turbo Distortion (parody; the
 * in-app face never reads "Boss"). Reference: pedals/Boss DS-2 schematic.
 *
 * DSP in TurboDistortionCore.h. Real panel: LEVEL / TONE / DIST + the TURBO
 * mode switch (I = classic, II = the hotter mid-forward Turbo voice). STEREO
 * I/O, dual-mono core; the nonlinear clip chain runs 4x oversampled.
 *
 * EXTRA gear — not mapped to any RS song.
 */
#include "DistrhoPlugin.hpp"
#include "TurboDistortionParams.h"
#include "TurboDistortionCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float clamp01(float v){ return v<0.0f?0.0f:(v>1.0f?1.0f:v); }

class TurboDistortionPlugin : public Plugin {
    turbodistortion::TurboDistortionCore left, right;
    rbshared::Oversampler4x osL, osR;
    float params[kParamCount];
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll(){
        left.setDist(params[kDist]);   right.setDist(params[kDist]);
        left.setTone(params[kTone]);   right.setTone(params[kTone]);
        left.setLevel(params[kLevel]); right.setLevel(params[kLevel]);
        left.setTurbo(params[kTurbo]); right.setTurbo(params[kTurbo]);
    }

public:
    TurboDistortionPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) params[i]=kTurboDistortionDef[i];
        const float sr=(float)getSampleRate();
        left.setSampleRate(kOS*sr); right.setSampleRate(kOS*sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "TurboDistortion"; }
    const char* getDescription() const override { return "Boss DS-2 Turbo Distortion style pedal"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('T','r','D','s'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i>=(uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i==(uint32_t)kTurbo) p.hints |= kParameterIsBoolean;
        p.name = kTurboDistortionNames[i]; p.symbol = kTurboDistortionSymbols[i];
        p.ranges.min = kTurboDistortionMin[i]; p.ranges.max = kTurboDistortionMax[i]; p.ranges.def = kTurboDistortionDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?params[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if (i<(uint32_t)kParamCount){ params[i]=clamp01(v); applyAll(); } }
    void sampleRateChanged(double r) override { const float sr=kOS*(float)r; left.setSampleRate(sr); right.setSampleRate(sr); osL.reset(); osR.reset(); applyAll(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0]; const float* iR=in[1];
        float* oL=out[0]; float* oR=out[1];
        for (uint32_t i=0;i<frames;++i){
            float ub[kOS];
            osL.upsample(iL[i], ub); for(int k=0;k<kOS;++k) ub[k]=left.process(ub[k]);  oL[i]=osL.downsample(ub);
            osR.upsample(iR[i], ub); for(int k=0;k<kOS;++k) ub[k]=right.process(ub[k]); oR[i]=osR.downsample(ub);
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TurboDistortionPlugin)
};

Plugin* createPlugin() { return new TurboDistortionPlugin(); }

END_NAMESPACE_DISTRHO
