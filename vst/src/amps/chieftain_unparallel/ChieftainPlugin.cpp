/*
 * UNPARALLEL CHIEFTAIN - Matchless Chieftain (Reverb), Mark Sampson, for the
 * game's Amp_BT15. Parody brand "RigBuilder"; the in-app face must NEVER read
 * "Matchless" or "Chieftain".
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in ChieftainCore.h (plain C++,
 * offline-testable); see that header for the circuit topology and schematic refs.
 * The amp is now CIRCUIT-REAL / Guitarix-style: two real cathode-biased 12AX7
 * TubeStages, the real Matchless TMB tone stack (ToneStackYeh), and a 2x EL34
 * push-pull power amp (PowerAmpEL34, no NFB), all run at 2x oversampling -- the
 * same architecture as the BOX AC30 template (en30/). Rebuilt from the
 * hand-traced 7-page schematic (amps/Matchless Chieftan (BTQ-15)/).
 *
 * STEREO I/O, single mono core: the amp IS a mono device, so it runs ONE
 * ChieftainCore (half the CPU of a true dual-core stereo build), but presents
 * 2-in/2-out and writes the same processed signal to BOTH output channels
 * (dual-mono -> centered/balanced, like the BOX AC30).
 *
 * the game: RS Gain -> VOLUME (drives the preamp into the EL34s); Bass/Mid/
 * Treble -> tone stack. Brilliance/Master/Reverb set on the face.
 */
#include "DistrhoPlugin.hpp"
#include "ChieftainParams.h"
#include "ChieftainCore.h"
#include "../../_shared/oversampler.hpp"

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts
// never hard-clip. (This is the ONLY tanh in the chain -- the OT/output ceiling.)
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

class ChieftainPlugin : public Plugin
{
    chieftain::ChieftainCore core;
    float params[kParamCount];
    rbshared::Oversampler4x os;                 // 2x anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        core.setVolume(params[kVolume]);
        core.setBass(params[kBass]);
        core.setMid(params[kMiddle]);
        core.setTreble(params[kTreble]);
        core.setBrilliance(params[kBrilliance]);
        core.setMaster(params[kMaster]);
        core.setReverb(params[kReverb]);
        core.setCabSim(params[kCabSim]);
    }

public:
    ChieftainPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kChieftainDef[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "UnparallelChieftain"; }
    const char* getDescription() const override { return "Unparallel Chieftain boutique clean/crunch head"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('U', 'c', 'h', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kChieftainNames[index];
        parameter.symbol = kChieftainSymbols[index];
        parameter.ranges.min = kChieftainMin[index];
        parameter.ranges.max = kChieftainMax[index];
        parameter.ranges.def = kChieftainDef[index];
    }

    float getParameterValue(uint32_t index) const override { return index < (uint32_t)kParamCount ? params[index] : 0.0f; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount) return;
        params[index] = chieftain::clamp01(value);
        applyAll();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        core.setSampleRate(kOS * (float)newSampleRate);
        os.reset();
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* in0 = inputs[0];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            float ub[kOS];
            os.upsample(2.35f * in0[i], ub);
            for (int k = 0; k < kOS; ++k)                  // core + output soft-clip at 2x
                ub[k] = rbAmpLvl(0.560f * core.process(ub[k]));
            const float y = os.downsample(ub);
            outL[i] = y;
            outR[i] = y;   // dual-mono: one core, same signal both sides = centered/balanced
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChieftainPlugin)
};

Plugin* createPlugin() { return new ChieftainPlugin(); }

END_NAMESPACE_DISTRHO
