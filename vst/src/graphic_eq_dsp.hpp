/*
 * Shared graphic-EQ DSP + DPF Plugin. The per-pedal .cpp defines the band
 * config (kEqBands, kEqFreqs[], kEqNames[], EQ_PLUGIN_LABEL, EQ_UNIQUE_ID,
 * EQ_Q, EQ_DB) before including this. Params are 0..1 normalized; the DSP
 * maps them to ±EQ_DB dB (0.5 = flat). N peaking biquads in series.
 */
#include "DistrhoPlugin.hpp"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

static inline float eqParamToDb(float v) { return (v - 0.5f) * (2.0f * EQ_DB); }

// ---- Boss GE-7 gyrator graphic-EQ model ----------------------------------
// In the real GE-7 each band is a gyrator (op-amp + caps simulating an
// inductor) forming a series-resonant bandpass, and all bands sum with the
// dry signal at the summing op-amp's virtual-ground node. We model exactly
// that: a constant-0dB-peak bandpass per band, run in PARALLEL, each scaled
// by g = 10^(dB/20) - 1 and added to the dry. This reproduces the GE-7's two
// signature traits — adjacent bands INTERACT (they sum, not cascade) and
// boost/cut are ASYMMETRIC. Band frequencies are the game's (= choosing the
// gyrator/series cap values); Q ≈ 1.4 gives the GE-7's ~1-octave bandwidth.
struct GyratorBP {
    float b0, b2, a1, a2, x1, x2, y1, y2;   // RBJ bandpass, constant 0 dB peak (b1 = 0)
    void reset() { x1 = x2 = y1 = y2 = 0.f; b0 = b2 = a1 = a2 = 0.f; }
    void set(float freq, float Q, float fs) {
        const float w0 = 6.28318530718f * freq / fs;
        const float cw = cosf(w0), sw = sinf(w0);
        const float alpha = sw / (2.0f * Q);
        const float a0 = 1.0f + alpha;
        b0 = alpha / a0; b2 = -alpha / a0;
        a1 = (-2.0f * cw) / a0; a2 = (1.0f - alpha) / a0;
    }
    inline float process(float x) {
        const float y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

class GraphicEqChannel {
    GyratorBP bp[kEqBands];
    float g[kEqBands];          // band contribution = 10^(dB/20) - 1
    float fs;
public:
    GraphicEqChannel() { fs = 48000.f; for (int i = 0; i < kEqBands; ++i) { g[i] = 0.f; bp[i].reset(); } recalc(); }
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; recalc(); }
    void setGainDb(int i, float db) { g[i] = powf(10.0f, db / 20.0f) - 1.0f; }
    void recalc() { for (int i = 0; i < kEqBands; ++i) bp[i].set(kEqFreqs[i], EQ_Q, fs); }
    inline float process(float x) {
        float out = x;                                      // dry path
        for (int i = 0; i < kEqBands; ++i) out += g[i] * bp[i].process(x);   // parallel gyrator branches
        return out;
    }
};

class GraphicEqPlugin : public Plugin {
    GraphicEqChannel L, R;
    float fParams[kEqBands];
    void applyAll() {
        for (int i = 0; i < kEqBands; ++i) { const float db = eqParamToDb(fParams[i]); L.setGainDb(i, db); R.setGainDb(i, db); }
    }
public:
    GraphicEqPlugin() : Plugin(kEqBands, 0, 0) {
        for (int i = 0; i < kEqBands; ++i) fParams[i] = 0.5f;
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr);
        applyAll();
    }
protected:
    const char* getLabel()       const override { return EQ_PLUGIN_LABEL; }
    const char* getDescription() const override { return "Graphic EQ"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return EQ_UNIQUE_ID; }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kEqBands) return;
        p.hints  = kParameterIsAutomatable;
        p.name   = kEqNames[i];                 // matches the RS knob name (the band freq)
        char sym[16]; std::snprintf(sym, sizeof(sym), "hz%s", kEqNames[i]);
        p.symbol = String(sym);
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 0.5f;
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kEqBands) ? fParams[i] : 0.5f; }
    void  setParameterValue(uint32_t i, float v) override {
        if (i < (uint32_t)kEqBands) { fParams[i] = v; L.setGainDb(i, eqParamToDb(v)); R.setGainDb(i, eqParamToDb(v)); }
    }
    void sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); applyAll(); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0];      float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphicEqPlugin)
};

Plugin* createPlugin() { return new GraphicEqPlugin(); }

END_NAMESPACE_DISTRHO
