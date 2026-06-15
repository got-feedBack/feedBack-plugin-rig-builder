/*
 * BassChorus — Boss CEB-3 Bass Chorus model for Bass_Pedal_BassChorus.
 *
 * BBD-style chorus: an LFO modulates a short delay; the delayed (detuned) copy
 * is mixed with the dry signal. The bass-specific LOW FILTER high-passes the
 * WET path so the low end stays dry and solid (only the highs get chorused) —
 * that's what keeps a bass from going wobbly/thin, and what the CE-3 guitar
 * version lacks. Frequencies tuned for bass (not the CE-3's guitar R/C values).
 * the game knobs: Rate, Depth, LoFilter, Mix.
 */
#include "DistrhoPlugin.hpp"
#include "BassChorusParams.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

class Chorus {
    static const int BUF = 2048;       // ~42 ms at 48 kHz — plenty for chorus delays
    float buf[BUF];
    int   wpos = 0;
    float fs = 48000.f;
    double phase = 0.0;
    float lfoInc = 0.f;
    float baseSamp = 432.f, depthSamp = 120.f;
    float cHP = 0.05f, zHP = 0.f;
    float mix = 0.5f;
public:
    void reset(float startPhase) { std::memset(buf, 0, sizeof(buf)); wpos = 0; zHP = 0.f; phase = startPhase; }
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; }
    void setParams(float rate, float depth, float lofilter, float mixP) {
        const float rateHz = 0.1f + rate * 7.9f;          // 0.1 .. 8 Hz
        lfoInc    = 6.2831853f * rateHz / fs;
        baseSamp  = 0.009f * fs;                          // 9 ms base delay
        depthSamp = depth * 0.005f * fs;                  // up to ±5 ms modulation
        cHP       = onePoleCoef(60.0f + lofilter * 340.0f, fs);  // wet high-pass: 60..400 Hz
        mix       = mixP;
    }
    inline float process(float x) {
        buf[wpos] = x;
        const float lfo = std::sin((float)phase);
        phase += lfoInc; if (phase > 6.2831853) phase -= 6.2831853;
        const float delay = baseSamp + depthSamp * (0.5f * (lfo + 1.0f));   // one-sided, always > 0
        float rp = (float)wpos - delay;
        while (rp < 0.f) rp += BUF;
        const int i0 = (int)rp; const float fr = rp - i0;
        const int i1 = (i0 + 1) % BUF;
        float wet = buf[i0] * (1.0f - fr) + buf[i1] * fr;
        // LOW FILTER: high-pass the wet so the low end stays dry/solid (bass)
        zHP += cHP * (wet - zHP);
        wet = wet - zHP;
        wpos = (wpos + 1) % BUF;
        return x * (1.0f - 0.4f * mix) + wet * mix;
    }
};

class BassChorusPlugin : public Plugin {
    Chorus L, R;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kRate], fParams[kDepth], fParams[kLoFilter], fParams[kMix]);
        R.setParams(fParams[kRate], fParams[kDepth], fParams[kLoFilter], fParams[kMix]);
    }
public:
    BassChorusPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBassChorusDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr);
        L.reset(0.0f); R.reset(1.5708f);   // R offset 90° for stereo width
        recalc();
    }
protected:
    const char* getLabel()       const override { return "BassChorus"; }
    const char* getDescription() const override { return "Boss CEB-3 bass chorus"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'C', 'h'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassChorusNames[i]; p.symbol = kBassChorusSymbols[i];
        p.ranges.min = kBassChorusMin[i]; p.ranges.max = kBassChorusMax[i]; p.ranges.def = kBassChorusDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(0.0f); R.reset(1.5708f); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassChorusPlugin)
};

Plugin* createPlugin() { return new BassChorusPlugin(); }

END_NAMESPACE_DISTRHO
