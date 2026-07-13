/*
 * BOX AC30 - AC30 Top Boost-style amp for the game's Amp_EN30.
 *
 * DPF wrapper (VST3 + AU). All the DSP lives in EN30Core.h (plain C++,
 * offline-testable); see that header for the circuit topology and schematic refs.
 *
 * STEREO I/O, single mono core: the amp IS a mono device, so it runs ONE EN30Core
 * (half the CPU of a true dual-core stereo build), but it presents 2-in/2-out and
 * writes the same processed signal to BOTH output channels. feedBack's engine
 * routed a 1-out (mono) plugin to a single side -> imbalanced/louder on one side;
 * dual-mono output keeps it centered/balanced while staying CPU-cheap.
 */
#include "DistrhoPlugin.hpp"
#include "EN30Params.h"
#include "BoxDC30Core.h"   // rebuilt Guitarix-style core (was EN30Core.h)
#include "../../_shared/oversampler.hpp"

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts
// never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Minimal peaking biquad for the Top-Boost upper-mid scoop (en30-only — keeps the
// shared BoxDC30Core / DC30 untouched).
struct En30Bq {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=y; return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void peak(float sr,float f,float dB,float Q){ if(dB==0.0f||sr<1000.0f){b0=1;b1=b2=a1=a2=0;return;} if(f>sr*0.49f)f=sr*0.49f;
        const float pi=3.14159265358979f, A=std::pow(10.f,dB/40.f),w=2*pi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al/A;
        b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
};

class EN30Plugin : public Plugin
{
    boxdc30::BoxDC30Core core;
    float params[kParamCount];
    rbshared::Oversampler4x os;                 // 4x anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;
    float osr = 96000.0f;
    En30Bq scoop;                               // Top-Boost upper-mid scoop (ref consensus)

    void applyAll()
    {
        // The AC30 Top Boost is upper-mid scooped (the ac30 reference is hm ~-5; the
        // UAD "ruby" ref ~0) -> a gentle ~1.7 kHz dip that scales with the Top Boost
        // weight (Input): Normal flat, Top Boost the full scoop. Lands hm ~-3 (consensus).
        scoop.peak(osr, 1850.0f, -4.0f * params[kInput], 1.15f);
        core.setNormalVol(params[kNormalVol]);
        core.setTBVol(params[kTBVol]);
        core.setTreble(params[kTreble]);
        core.setBass(params[kBass]);
        core.setRevTone(params[kRevTone]);
        core.setRevLevel(params[kRevLevel]);
        core.setSpeed(params[kSpeed]);
        core.setDepth(params[kDepth]);
        core.setCut(params[kCut]);
        core.setMaster(params[kMaster]);
        core.setInput(params[kInput]);
        core.setBright(params[kBright]);
        core.setCabSim(params[kCabSim]);
    }

public:
    EN30Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kEN30Def[i];
        osr = kOS * (float)getSampleRate();
        core.setSampleRate(osr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BOX AC30"; }
    const char* getDescription() const override { return "BOX AC30 / AC30 Top Boost style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('E', 'n', '3', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kEN30Names[index];
        parameter.symbol = kEN30Symbols[index];
        parameter.ranges.min = kEN30Min[index];
        parameter.ranges.max = kEN30Max[index];
        parameter.ranges.def = kEN30Def[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = boxdc30::clamp01(value);
        applyAll();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        osr = kOS * (float)newSampleRate;
        core.setSampleRate(osr);
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
            os.upsample(3.2f * in0[i], ub);
            for (int k = 0; k < kOS; ++k)                  // core + Top-Boost scoop + soft-clip at 4x
                ub[k] = rbAmpLvl(1.477f * scoop.process(core.process(ub[k])));
            const float y = os.downsample(ub);
            outL[i] = y;
            outR[i] = y;   // dual-mono: one core, same signal both sides = centered/balanced
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EN30Plugin)
};

Plugin* createPlugin()
{
    return new EN30Plugin();
}

END_NAMESPACE_DISTRHO
