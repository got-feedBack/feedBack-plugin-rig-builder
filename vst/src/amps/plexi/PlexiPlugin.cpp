/*
 * MARSTEN PLEXI - Marshall 1959 Super Lead 100W (Plexi/JMP) for the game's
 * Amp_MarshallPlexi. Parody brand "Marsten"; the in-app face must never read
 * "Marshall".
 *
 * DSP in PlexiCore.h (circuit-real on the shared tube_stage.hpp framework, with
 * CONTROLLED gain staging so the Loudness knobs sweep clean -> roar). Rewritten
 * from the old cascade that saturated every signal to ~100% THD. STEREO I/O,
 * single mono core -> both outputs (dual-mono); nonlinear chain at 2x oversampling.
 *
 * RS: Gain -> Loudness I; Treble/Bass/Mid -> tone stack, Pres -> Presence.
 */
#include "DistrhoPlugin.hpp"
#include "PlexiParams.h"
#include "PlexiCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

static constexpr float kPlexiMakeup = 0.50f;

class PlexiPlugin : public Plugin {
    plexi::PlexiCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll(){
        core.setPresence(fParams[kPresence]); core.setBass(fParams[kBass]); core.setMiddle(fParams[kMiddle]);
        core.setTreble(fParams[kTreble]); core.setLoudness1(fParams[kLoudness1]); core.setLoudness2(fParams[kLoudness2]);
        core.setInput(fParams[kInput]);
    }
public:
    PlexiPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kPlexiDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }
protected:
    const char* getLabel() const override { return "Plexi"; }
    const char* getDescription() const override { return "Marshall 1959 Super Lead Plexi style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2, 0, 1); }
    int64_t getUniqueId() const override { return d_cconst('P', 'l', '5', '9'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == (uint32_t)kInput || i == (uint32_t)kCabSim) p.hints |= kParameterIsBoolean;
        p.name = kPlexiNames[i]; p.symbol = kPlexiSymbols[i];
        p.ranges.min = kPlexiMin[i]; p.ranges.max = kPlexiMax[i]; p.ranges.def = kPlexiDef[i];
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
                ub[k] = rbAmpLvl(kPlexiMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlexiPlugin)
};

Plugin* createPlugin() { return new PlexiPlugin(); }

END_NAMESPACE_DISTRHO
