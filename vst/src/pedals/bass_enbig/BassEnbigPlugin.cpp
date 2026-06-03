/*
 * BassEnbig — "Bass Enbiggenator" LFO-modulated resonant filter (vibe/wobble).
 *
 * A zero-delay TPT state-variable band-pass at moderate Q whose centre
 * frequency is swept up and down by a sine LFO around a base frequency. Unlike
 * the Bass Wah (envelope-driven), this is a pure LFO throb — a sci-fi auto-vibe.
 *   Rate   -> LFO speed (0.1 .. 8 Hz)
 *   Depth  -> sweep range in octaves around the base (0 .. ~1.6 oct)
 *   Filter -> base centre frequency (120 Hz .. ~1.1 kHz)
 *   Mix    -> wet/dry blend
 */
#include "DistrhoPlugin.hpp"
#include "BassEnbigParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class VibeFilter {
    float fs = 48000.f;
    float ic1 = 0.f, ic2 = 0.f;
    float lfoPhase = 0.f, lfoInc = 0.001f;
    float baseFc = 300.f, depthOct = 1.0f, mix = 0.7f;
    const float Q = 2.8f;
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; }
    void reset() { ic1 = ic2 = 0.f; lfoPhase = 0.f; }
    void setParams(float rate, float depth, float mixP, float filter) {
        lfoInc   = 6.2831853f * (0.1f + rate * 7.9f) / fs;     // 0.1 .. 8 Hz
        depthOct = depth * 1.6f;                                // up to 1.6 oct
        baseFc   = 120.0f * std::pow(2.0f, filter * 3.2f);      // 120 .. ~1100 Hz
        mix      = mixP;
    }
    inline float process(float x) {
        lfoPhase += lfoInc; if (lfoPhase > 6.2831853f) lfoPhase -= 6.2831853f;
        const float lfo = std::sin(lfoPhase);                   // -1 .. 1
        float fc = baseFc * std::pow(2.0f, lfo * depthOct);
        if (fc < 40.f) fc = 40.f; const float nyq = fs * 0.45f; if (fc > nyq) fc = nyq;

        const float g = std::tan(3.14159265f * fc / fs);
        const float k = 1.0f / Q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;                    // band-pass
        const float v2 = ic2 + a2 * ic1 + g * a2 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        const float wet = v1 * k * 1.6f;                        // normalized resonant peak
        return x * (1.0f - mix) + wet * mix;
    }
};

class BassEnbigPlugin : public Plugin {
    VibeFilter L, R;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kRate], fParams[kDepth], fParams[kMix], fParams[kFilter]);
        R.setParams(fParams[kRate], fParams[kDepth], fParams[kMix], fParams[kFilter]);
    }
public:
    BassEnbigPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBassEnbigDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "BassEnbig"; }
    const char* getDescription() const override { return "Modulated resonant filter"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'E', 'n'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassEnbigNames[i]; p.symbol = kBassEnbigSymbols[i];
        p.ranges.min = kBassEnbigMin[i]; p.ranges.max = kBassEnbigMax[i]; p.ranges.def = kBassEnbigDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassEnbigPlugin)
};

Plugin* createPlugin() { return new BassEnbigPlugin(); }

END_NAMESPACE_DISTRHO
