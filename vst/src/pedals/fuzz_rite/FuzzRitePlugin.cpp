/*
 * FuzzRite - Mosrite FuzzRite (silicon), parody brand (the in-app face reads
 * "nosrite", never "mosrite"). Reference: pedals/fuzzrite/FuzzRiteV3.pdf.
 *
 * DSP in FuzzRiteCore.h — two grounded-emitter NPN silicon stages that slam into
 * hard transistor clipping, tiny 2n2 interstage caps for the thin nasal voice,
 * the real DEPTH wiper between Q1 and Q2's input node, and the C4 feedback path
 * from Q2's collector to Q1's collector. Runs at
 * 2x oversampling to keep the square-wave harmonics from aliasing. Real panel =
 * DEPTH + VOL.
 *
 * EXTRA gear — not mapped to any RS song.
 */
#include "DistrhoPlugin.hpp"
#include "FuzzRiteParams.h"
#include "FuzzRiteCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float clamp01(float v){ return v<0.0f?0.0f:(v>1.0f?1.0f:v); }
static inline float passiveOutput(float x){
    // Collector swing is already bounded in the core. This represents the
    // finite 9 V output headroom and never creates samples above full scale.
    return std::tanh(x);
}

class FuzzRitePlugin : public Plugin {
    fuzzrite::FuzzRiteCore left, right;
    rbshared::Oversampler4x osL, osR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll(){
        left.setDepth(params[kDepth]);   right.setDepth(params[kDepth]);
        left.setVolume(params[kVolume]); right.setVolume(params[kVolume]);
    }

public:
    FuzzRitePlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) params[i]=kFuzzRiteDef[i];
        const float sr=(float)getSampleRate();
        left.setSampleRate(kOS*sr); right.setSampleRate(kOS*sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "FuzzRite"; }
    const char* getDescription() const override { return "Mosrite FuzzRite style silicon fuzz"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,1,0); }
    int64_t getUniqueId() const override { return d_cconst('F','z','R','t'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i>=(uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kFuzzRiteNames[i]; p.symbol = kFuzzRiteSymbols[i];
        p.ranges.min = kFuzzRiteMin[i]; p.ranges.max = kFuzzRiteMax[i]; p.ranges.def = kFuzzRiteDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?params[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override {
        if (i<(uint32_t)kParamCount){ params[i]=clamp01(v); applyAll(); }
    }
    void sampleRateChanged(double r) override {
        const float sr=(float)r; osL.reset(); osR.reset();
        left.setSampleRate(kOS*sr); right.setSampleRate(kOS*sr); applyAll();
    }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0]; const float* iR=in[1];
        float* oL=out[0]; float* oR=out[1];
        // VOL is the real 500KA passive output pot: mute at zero, audio taper.
        const float vol = std::pow(params[kVolume], 2.6f);
        float ubL[kOS], ubR[kOS];
        for (uint32_t i=0;i<frames;++i){
            osL.upsample(iL[i], ubL); osR.upsample(iR[i], ubR);
            for (int k=0;k<kOS;++k){ ubL[k]=left.process(ubL[k]); ubR[k]=right.process(ubR[k]); }
            oL[i]=passiveOutput(osL.downsample(ubL)*vol);
            oR[i]=passiveOutput(osR.downsample(ubR)*vol);
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FuzzRitePlugin)
};

Plugin* createPlugin() { return new FuzzRitePlugin(); }

END_NAMESPACE_DISTRHO
