/*
 * MARSTEN SILVER JUBILEE - Marshall 2555 Silver Jubilee (25th Anniversary,
 * 50/100W head). Parody brand "Marsten"; the in-app face must never read
 * "Marshall". Reference: official Marshall 2555 STD schematic (2555.DGM,
 * iss.3 6-6-88).
 *
 * DSP in JubileeCore.h — circuit-real on the shared tube_stage.hpp framework
 * plus a mixed LED/1N4007 Shockley clipper: the Jubilee's voice is a preamp
 * diode network, not pure tube clipping. The GAIN pull switch adds the D4/D5
 * rhythm pair and C6 high-frequency shunt. STEREO
 * I/O, single mono core -> both outputs (dual-mono); the nonlinear chain runs
 * at 2x oversampling.
 *
 * EXTRA gear — not mapped to any RS song yet. Panel is the real 2555:
 * Gain / Lead Master / Bass / Middle / Treble / Presence / Master +
 * Rhythm Clip switch + Cab Sim.
 */
#include "DistrhoPlugin.hpp"
#include "SilverJubileeParams.h"
#include "JubileeCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

static constexpr float kJubileeMakeup = 0.50f;   // family-level trim, post-DSP only

class SilverJubileePlugin : public Plugin {
    jubilee::JubileeCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll(){
        core.setGain(fParams[kGain]); core.setLeadMaster(fParams[kLeadMaster]);
        core.setBass(fParams[kBass]); core.setMiddle(fParams[kMiddle]); core.setTreble(fParams[kTreble]);
        core.setPresence(fParams[kPresence]); core.setMaster(fParams[kMaster]);
        core.setRhythmClip(fParams[kRhythmClip]); core.setCabSim(fParams[kCabSim]);
    }
public:
    SilverJubileePlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kSilverJubileeDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }
protected:
    const char* getLabel() const override { return "MarstenSilverJubilee"; }
    const char* getDescription() const override { return "Marshall 2555 Silver Jubilee style amp — circuit-real model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('M','S','J','b'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == (uint32_t)kCabSim || i == (uint32_t)kRhythmClip) p.hints |= kParameterIsBoolean;
        p.name = kSilverJubileeNames[i]; p.symbol = kSilverJubileeSymbols[i];
        p.ranges.min = kSilverJubileeMin[i]; p.ranges.max = kSilverJubileeMax[i]; p.ranges.def = kSilverJubileeDef[i];
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
                ub[k] = rbAmpLvl(kJubileeMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilverJubileePlugin)
};

Plugin* createPlugin() { return new SilverJubileePlugin(); }

END_NAMESPACE_DISTRHO
