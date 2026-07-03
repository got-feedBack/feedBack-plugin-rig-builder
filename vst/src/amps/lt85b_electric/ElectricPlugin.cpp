/*
 * Electric B600F — Acoustic B600H (600 W solid-state bass head), circuit-real.
 *
 * From the Acoustic B450/B600H schematic + front panel (all solid-state):
 *   • INPUT  — Passive / Active jacks + Mute
 *   • PREAMP — Gain (NJM4558 op-amp gain that soft-clips at the rails = the Clip/
 *              Peak-Detect LED, POST_GAIN) and Volume (master)
 *   • NOTCH  — a sweepable band-reject filter (VR9 dual, Frequency + In/Out)
 *   • EQ     — a 6-band tone EQ: 40/120/350/800/2k/5k Hz (gyrators), +/-15 dB
 *   • POWER  — linear Class-AB output (2SC5200/2SA1943); clean, big headroom
 *
 * The ONLY nonlinearity is the op-amp Gain soft-clip (post-gain, pre-EQ, per the
 * schematic's Peak Detect node); everything else is clean op-amp EQ. That clip
 * runs at 2x oversampling. STEREO I/O, single mono core -> dual-mono out.
 */
#include "DistrhoPlugin.hpp"
#include "ElectricParams.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// ── RBJ biquad — 6-band EQ peaks + sweepable notch ───────────────────────────
class Biquad {
    float b0=1, b1=0, b2=0, a1=0, a2=0, z1=0, z2=0;
public:
    void reset() { z1 = z2 = 0.f; }
    inline float process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void setPeak(float fc, float dB, float Q, float fs) {
        if (fc > fs*0.49f) fc = fs*0.49f;
        const float A = std::pow(10.f, dB / 40.f);
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha / A;
        b0 = (1 + alpha * A) / a0; b1 = (-2 * cw) / a0; b2 = (1 - alpha * A) / a0;
        a1 = (-2 * cw) / a0; a2 = (1 - alpha / A) / a0;
    }
    void setNotch(float fc, float Q, float fs) {
        if (fc > fs*0.49f) fc = fs*0.49f;
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha;
        b0 = 1.f / a0; b1 = (-2 * cw) / a0; b2 = 1.f / a0;
        a1 = (-2 * cw) / a0; a2 = (1 - alpha) / a0;
    }
    void setBypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

class ElectricCore {
    float fs = 96000.f;
    Biquad notch, eq[kNumEq];
    float gain=1, volume=1; bool notchOn=false, mute=false;
public:
    void setSampleRate(float s){ fs=(s>0.f)?s:96000.f; }
    void reset(){ notch.reset(); for(int i=0;i<kNumEq;++i) eq[i].reset(); }

    void setParams(const float* p) {
        const float pad = (p[kActive] > 0.5f) ? 0.5f : 1.0f;
        gain = (0.5f + p[kGain] * 3.5f) * pad;
        notchOn = p[kNotchOn] > 0.5f;
        const float nfc = 50.f * std::pow(60.0f, p[kNotchFreq]);     // 50 Hz .. 3 kHz
        if (notchOn) notch.setNotch(nfc, 3.0f, fs); else notch.setBypass();
        for (int i=0;i<kNumEq;++i)
            eq[i].setPeak(kEqFreqs[i], (p[kFirstEq+i]-0.5f)*30.f, 0.9f, fs);   // +/-15 dB
        volume = p[kVolume] / 0.7f;
        mute = p[kMute] > 0.5f;
    }

    inline float process(float x) {
        if (mute) return 0.f;
        // Gain op-amp with rail soft-clip (POST_GAIN / Peak Detect). K ~ the op-amp
        // headroom: clean for normal levels, soft-clips only when Gain is pushed.
        const float K = 3.5f;
        float s = K * std::tanh((x * gain) / K);
        s = notch.process(s);                            // sweepable notch
        for (int i=0;i<kNumEq;++i) s = eq[i].process(s); // 6-band EQ (clean op-amps)
        return s * volume;                               // Volume (master) into the power amp
    }
};

// Family loudness makeup into the soft knee (tuned offline ~-14 dBFS @ noon).
static constexpr float kElectricMakeup = 1.20f;

class ElectricPlugin : public Plugin {
    ElectricCore core;
    float fParams[kParamCount];
    rbshared::Oversampler4x os;
    static constexpr int kOS = rbshared::Oversampler4x::OS;
public:
    ElectricPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kElectricDef[i];
        core.setSampleRate(kOS * (float)getSampleRate()); core.reset(); core.setParams(fParams);
    }
protected:
    const char* getLabel()       const override { return "ElectricB600F"; }
    const char* getDescription() const override { return "Acoustic B600H 600W solid-state bass head — circuit-real model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'E', 'B'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kActive) p.hints |= kParameterIsBoolean;
        p.name = kElectricNames[i]; p.symbol = kElectricSymbols[i];
        p.ranges.min = kElectricMin[i]; p.ranges.max = kElectricMax[i]; p.ranges.def = kElectricDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i]=v; core.setParams(fParams); } }
    void  sampleRateChanged(double r) override { core.setSampleRate(kOS * (float)r); os.reset(); core.reset(); core.setParams(fParams); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* i0=in[0]; float* oL=out[0]; float* oR=out[1];
        for (uint32_t i=0;i<frames;++i){
            float ub[kOS];
            os.upsample(i0[i], ub);
            for (int k=0;k<kOS;++k)
                ub[k] = rbAmpLvl(kElectricMakeup * core.process(ub[k]));
            const float y = os.downsample(ub);
            oL[i] = y; oR[i] = y;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElectricPlugin)
};

Plugin* createPlugin() { return new ElectricPlugin(); }

END_NAMESPACE_DISTRHO
