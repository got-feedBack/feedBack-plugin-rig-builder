/*
 * BassWah — Dunlop Cry Baby Bass Wah (105Q) model for Bass_Pedal_BassWah.
 *
 * A resonant band-pass ("wah") filter — a zero-delay TPT state-variable filter
 * at high Q — whose centre frequency is swept across a bass-friendly range
 * (100 Hz .. 1.6 kHz). The sweep position comes from:
 *   - the manual Pedal (treadle) position,
 *   - the input envelope scaled by Sens (touch / auto-wah), and
 *   - an LFO at Speed when Auto is on.
 * A little dry signal is blended back so the bass keeps its low-end body when
 * the wah peak rides high (the 105Q's bass-retaining trick).
 */
#include "DistrhoPlugin.hpp"
#include "BassWahParams.h"
#include <cmath>
#include "../_shared/automakeup.hpp"

START_NAMESPACE_DISTRHO

class Wah {
    float fs = 48000.f;
    // TPT SVF state
    float ic1 = 0.f, ic2 = 0.f;
    // envelope follower
    float env = 0.f, atk = 0.f, rel = 0.f;
    // LFO
    float lfoPhase = 0.f, lfoInc = 0.001f;
    // params
    bool  autoSweep = true;
    float pedal = 0.25f, sens = 0.6f;
    const float Q = 3.6f;

    static inline float msCoef(float ms, float fs) { return std::exp(-1.0f / (0.001f * ms * fs)); }
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; atk = msCoef(6.f, fs); rel = msCoef(140.f, fs); }
    void reset() { ic1 = ic2 = env = 0.f; lfoPhase = 0.f; }
    void setParams(float autoP, float pedalP, float sensP, float speedP) {
        autoSweep = autoP > 0.5f;
        pedal = pedalP; sens = sensP;
        lfoInc = 6.2831853f * (0.1f + speedP * 5.9f) / fs;   // 0.1 .. 6 Hz
    }
    inline float process(float x) {
        // envelope (normalized-ish to 0..1)
        const float a = std::fabs(x);
        const float c = (a > env) ? atk : rel;
        env = c * env + (1.0f - c) * a;
        float e = env * 3.0f; if (e > 1.f) e = 1.f;

        // sweep position 0..1
        float pos;
        if (autoSweep) {
            lfoPhase += lfoInc; if (lfoPhase > 6.2831853f) lfoPhase -= 6.2831853f;
            const float lfo01 = 0.5f + 0.5f * std::sin(lfoPhase);
            pos = 0.12f + 0.55f * lfo01 + 0.35f * sens * e;
        } else {
            pos = pedal * 0.70f + 0.40f * sens * e;
        }
        if (pos < 0.f) pos = 0.f; if (pos > 1.f) pos = 1.f;

        // map to centre frequency (log, 100 Hz .. 1.6 kHz)
        const float fc = 100.0f * std::pow(16.0f, pos);

        // TPT state-variable band-pass
        const float g = std::tan(3.14159265f * fc / fs);
        const float k = 1.0f / Q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;           // band-pass output
        const float v2 = ic2 + a2 * ic1 + g * a2 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        // wah honk (band-pass × Q for the resonant peak) + a little dry body
        const float wah = v1 * k * 1.8f;
        return wah * 0.88f + x * 0.14f;
    }
};

class BassWahPlugin : public Plugin {
    Wah L, R;
    RBAutoMakeup makeupL, makeupR;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kAuto], fParams[kPedal], fParams[kSens], fParams[kSpeed]);
        R.setParams(fParams[kAuto], fParams[kPedal], fParams[kSens], fParams[kSpeed]);
    }
public:
    BassWahPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBassWahDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); makeupL.setSampleRate(sr); makeupR.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "BassWah"; }
    const char* getDescription() const override { return "Cry Baby bass wah"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'W', 'a'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == kAuto) p.hints |= kParameterIsBoolean;
        p.name = kBassWahNames[i]; p.symbol = kBassWahSymbols[i];
        p.ranges.min = kBassWahMin[i]; p.ranges.max = kBassWahMax[i]; p.ranges.def = kBassWahDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); makeupL.snap(); makeupR.snap(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); makeupL.setSampleRate((float)r); makeupR.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = makeupL.process(iL[i], L.process(iL[i])); oR[i] = makeupR.process(iR[i], R.process(iR[i])); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassWahPlugin)
};

Plugin* createPlugin() { return new BassWahPlugin(); }

END_NAMESPACE_DISTRHO
