/*
 * MARSTEN JCM800 - Marshall JCM800 2204 master-volume head for the game's
 * Amp_MarshallJCM800. Parody brand "Marsten"; the in-app face must never read
 * "Marshall". Reference: official 2204 preamp/power schematics.
 *
 * DSP in Jcm800Core.h (circuit-real on the shared tube_stage.hpp framework, with
 * CONTROLLED gain staging so the Gain knob sweeps clean -> crunch). Rewritten from
 * the old cascade that was so over-gained it saturated every signal to ~100% THD
 * at all settings. STEREO I/O, single mono core -> both outputs (dual-mono); the
 * nonlinear chain runs at 2x oversampling.
 *
 * RS: Gain -> GAIN, Bass/Mid/Treble -> tone stack, Pres -> Presence.
 */
#include "DistrhoPlugin.hpp"
#include "Jcm800Params.h"
#include "Jcm800Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

static constexpr float kJcm800Makeup = 0.50f;   // family-level trim (tuned offline)

class Jcm800Plugin : public Plugin {
    jcm800::Jcm800Core core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll(){
        core.setGain(fParams[kGain]); core.setBass(fParams[kBass]); core.setMiddle(fParams[kMiddle]);
        core.setTreble(fParams[kTreble]); core.setPresence(fParams[kPresence]); core.setVolume(fParams[kVolume]);
    }
public:
    Jcm800Plugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kJcm800Def[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }
protected:
    const char* getLabel() const override { return "MarstenJCM800"; }
    const char* getDescription() const override { return "Marshall JCM800 2204 style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2, 0, 1); }
    int64_t getUniqueId() const override { return d_cconst('M','j','8','0'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == (uint32_t)kCabSim) p.hints |= kParameterIsBoolean;
        p.name = kJcm800Names[i]; p.symbol = kJcm800Symbols[i];
        p.ranges.min = kJcm800Min[i]; p.ranges.max = kJcm800Max[i]; p.ranges.def = kJcm800Def[i];
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
                ub[k] = rbAmpLvl(kJcm800Makeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Jcm800Plugin)
};

Plugin* createPlugin() { return new Jcm800Plugin(); }

END_NAMESPACE_DISTRHO
