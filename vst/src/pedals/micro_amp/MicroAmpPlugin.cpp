/*
 * MicroAmp - MXR Micro Amp (M133) clean boost, parody brand (the in-app face
 * reads "NYR", never "MXR"). Reference: pedals/MicroAmp/ggg_mamp_sc.pdf.
 *
 * DSP in MicroAmpCore.h — a single TL061 non-inverting boost, gain 1+R4/(R5+R6)
 * swept ~unity..+26 dB by the GAIN pot, with the op-amp's own bandwidth/slew/
 * rail behaviour. 2x oversampled so the soft clip at high GAIN stays clean.
 * Real panel = one knob (GAIN).
 *
 * EXTRA gear — not mapped to any RS song.
 */
#include "DistrhoPlugin.hpp"
#include "MicroAmpParams.h"
#include "MicroAmpCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float clamp01(float v){ return v<0.0f?0.0f:(v>1.0f?1.0f:v); }

class MicroAmpPlugin : public Plugin {
    microamp::MicroAmpCore left, right;
    rbshared::Oversampler4x osL, osR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll(){ left.setGain(params[kGain]); right.setGain(params[kGain]); }

public:
    MicroAmpPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) params[i]=kMicroAmpDef[i];
        const float sr=(float)getSampleRate();
        left.setSampleRate(kOS*sr); right.setSampleRate(kOS*sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MicroAmp"; }
    const char* getDescription() const override { return "MXR Micro Amp style clean boost"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,1,0); }
    int64_t getUniqueId() const override { return d_cconst('M','c','A','p'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i>=(uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kMicroAmpNames[i]; p.symbol = kMicroAmpSymbols[i];
        p.ranges.min = kMicroAmpMin[i]; p.ranges.max = kMicroAmpMax[i]; p.ranges.def = kMicroAmpDef[i];
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
        float ubL[kOS], ubR[kOS];
        for (uint32_t i=0;i<frames;++i){
            osL.upsample(iL[i], ubL); osR.upsample(iR[i], ubR);
            for (int k=0;k<kOS;++k){ ubL[k]=left.process(ubL[k]); ubR[k]=right.process(ubR[k]); }
            // The TL061 stage already models GBW, slew and rail clipping. A
            // second tanh here changed the circuit and reduced maximum boost.
            oL[i]=osL.downsample(ubL);
            oR[i]=osR.downsample(ubR);
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MicroAmpPlugin)
};

Plugin* createPlugin() { return new MicroAmpPlugin(); }

END_NAMESPACE_DISTRHO
