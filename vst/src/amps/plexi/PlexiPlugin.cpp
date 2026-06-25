/*
 * MARSTEN PLEXI - Marshall 1959 Super Lead 100W (Plexi/JMP) for the game's
 * Amp_MarshallPlexi. Parody brand "Marsten" (same as the DSL100 / GM-2 / UV-1);
 * the in-app face must never read "Marshall".
 *
 * This is the THIN WRAPPER: all the circuit modelling lives in PlexiCore.h, which
 * was rebuilt to the advanced framework (the same physical blocks the BOX AC30 /
 * BoxDC30Core uses) — real ECC83/12AX7 TubeStages with Miller loading, coupling-cap
 * grid-leak blocking distortion, a 12AX7 long-tail-pair PI, a 4x EL34 fixed-bias
 * push-pull, per-node B+ sag (solid-state-rectified, stiff), the Marshall FMV tone
 * stack (Yeh), real pot tapers, a reactive output transformer + bypassable 4x12.
 *
 * Local reference (modelled component-by-component):
 *   amps/Marshall Plexi/1959-01-60-02.pdf  (1959SLP-01 preamp + power schematics)
 *
 * the game: no gain knob, so RS Gain -> Loudness I (clean->crunch->roar);
 * Treble/Bass/Mid -> tone stack, Pres -> Presence. See rs_knob_to_vst_param.json
 * (Loudness II / Input jumper pinned via _static). Cab Sim is a fallback speaker
 * voice until Slopsmith bypasses it for an external cabinet/IR.
 */
#include "DistrhoPlugin.hpp"
#include "PlexiParams.h"
#include "PlexiCore.h"                     // advanced circuit-real Plexi core
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts never
// hard-clip.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

class PlexiPlugin : public Plugin
{
    plexi::PlexiCore left;
    plexi::PlexiCore right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;            // 4x anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        for (int i = 0; i < kParamCount; ++i)
        {
            left.setParam(i, params[i]);
            right.setParam(i, params[i]);
        }
    }

public:
    PlexiPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kPlexiDef[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Plexi"; }
    const char* getDescription() const override { return "Marshall 1959 Super Lead Plexi style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('P', 'l', '5', '9'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kPlexiNames[index];
        parameter.symbol = kPlexiSymbols[index];
        parameter.ranges.min = kPlexiMin[index];
        parameter.ranges.max = kPlexiMax[index];
        parameter.ranges.def = kPlexiDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = plexi::clamp01(value);
        left.setParam((int)index, params[index]);
        right.setParam((int)index, params[index]);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate(kOS * (float)newSampleRate);
        right.setSampleRate(kOS * (float)newSampleRate);
        osL.reset(); osR.reset();
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            float ub[kOS];
            osL.upsample(3.2f * inL[i], ub);
            for (int k = 0; k < kOS; ++k) ub[k] = rbAmpLvl(0.245f * left.process(ub[k]));
            outL[i] = osL.downsample(ub);
            osR.upsample(3.2f * inR[i], ub);
            for (int k = 0; k < kOS; ++k) ub[k] = rbAmpLvl(0.245f * right.process(ub[k]));
            outR[i] = osR.downsample(ub);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlexiPlugin)
};

Plugin* createPlugin()
{
    return new PlexiPlugin();
}

END_NAMESPACE_DISTRHO
