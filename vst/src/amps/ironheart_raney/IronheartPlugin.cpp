/*
 * RANEY IRONHEART - Laney Ironheart IRT60H (2x 6L6, 4x ECC83). Parody brand
 * "Raney"; the in-app face must never read "Laney". Modelled from the official
 * service schematic; calibrated per channel against the user's references.
 *
 * DSP in IronheartCore.h — circuit-real on tube_stage.hpp: op-amp Pre-Boost,
 * Clean/Rhythm/Lead tube paths (2/3/5 stages), post-distortion TMB, post-PI
 * TONE tilt, WATTS drive attenuator, DYNAMICS as NFB depth, 2x 6L6GC.
 * STEREO I/O, mono core -> both outputs; nonlinear chain oversampled.
 *
 * EXTRA gear — not mapped to any RS song (the song-mapped Laney stays AOR50).
 */
#include "DistrhoPlugin.hpp"
#include "IronheartParams.h"
#include "IronheartCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

static constexpr float kIronheartMakeup = 0.42f;

class IronheartPlugin : public Plugin {
    irt::IronheartCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll(){
        core.setGain(fParams[kGain]); core.setBass(fParams[kBass]);
        core.setMiddle(fParams[kMiddle]); core.setTreble(fParams[kTreble]);
        core.setVolume(fParams[kVolume]); core.setDynamics(fParams[kDynamics]);
        core.setTone(fParams[kTone]); core.setWatts(fParams[kWatts]);
        core.setCabSim(fParams[kCabSim]);
        core.setChannel((int)std::lround(fParams[kChannel] * 2.0f));
        core.setBoost(fParams[kBoost]);
        core.setBoostOn(fParams[kBoostOn] > 0.5f);
    }
public:
    IronheartPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kIronheartDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }
protected:
    const char* getLabel() const override { return "RaneyIronheart"; }
    const char* getDescription() const override { return "Laney Ironheart IRT60 style amp — circuit-real Clean/Rhythm/Lead model"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D','z','V','4'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == (uint32_t)kCabSim || i == (uint32_t)kBoostOn) p.hints |= kParameterIsBoolean;
        p.name = kIronheartNames[i]; p.symbol = kIronheartSymbols[i];
        p.ranges.min = kIronheartMin[i]; p.ranges.max = kIronheartMax[i]; p.ranges.def = kIronheartDef[i];
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
                ub[k] = rbAmpLvl(kIronheartMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IronheartPlugin)
};

Plugin* createPlugin() { return new IronheartPlugin(); }

END_NAMESPACE_DISTRHO
