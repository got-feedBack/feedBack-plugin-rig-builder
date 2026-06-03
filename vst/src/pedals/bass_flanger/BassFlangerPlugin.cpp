/*
 * BassFlanger — Boss BF-2/BF-3 flanger model for Bass_Pedal_BassFlanger,
 * adapted for bass.
 *
 * Flanger = a SHORT LFO-modulated delay (~0.4–3.5 ms) summed with the dry
 * signal, with FEEDBACK around the delay to create the resonant "jet" (the
 * comb filter's resonance). Bass adaptation: the wet + feedback path is
 * high-passed so the low end stays solid and the feedback can't build up a
 * muddy sub rumble — only the highs flange. Delay range tuned for bass, not
 * the BF-2's guitar BBD clock values.
 * Rocksmith knobs: Rate, Depth, Filter (= Resonance/feedback), Mix.
 */
#include "DistrhoPlugin.hpp"
#include "BassFlangerParams.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

class Flanger {
    static const int BUF = 1024;       // ~21 ms at 48 kHz — flanger delays are short
    float buf[BUF];
    int   wpos = 0;
    float fs = 48000.f;
    double phase = 0.0;
    float lfoInc = 0.f;
    float baseSamp = 19.f, depthSamp = 70.f;
    float feedback = 0.4f;
    float cHP = 0.05f, zHP = 0.f;
    float mix = 0.6f;
public:
    void reset(float startPhase) { std::memset(buf, 0, sizeof(buf)); wpos = 0; zHP = 0.f; phase = startPhase; }
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; }
    void setParams(float rate, float depth, float filterP, float mixP) {
        const float rateHz = 0.05f + rate * 5.0f;          // 0.05 .. 5 Hz
        lfoInc    = 6.2831853f * rateHz / fs;
        baseSamp  = 0.0004f * fs;                          // ~0.4 ms base delay
        depthSamp = depth * 0.003f * fs;                   // up to ~3 ms sweep
        feedback  = filterP * 0.85f;                       // resonance / jet (clamped < 1)
        cHP       = onePoleCoef(120.0f, fs);               // keep lows out of wet + feedback (bass)
        mix       = mixP;
    }
    inline float process(float x) {
        const float lfo = std::sin((float)phase);
        phase += lfoInc; if (phase > 6.2831853) phase -= 6.2831853;
        const float delay = baseSamp + depthSamp * (0.5f * (lfo + 1.0f));
        float rp = (float)wpos - delay;
        while (rp < 0.f) rp += BUF;
        const int i0 = (int)rp; const float fr = rp - i0;
        const int i1 = (i0 + 1) % BUF;
        float wet = buf[i0] * (1.0f - fr) + buf[i1] * fr;
        // high-pass the wet so the low end stays dry/solid and the feedback
        // can't accumulate sub-bass rumble
        zHP += cHP * (wet - zHP);
        wet = wet - zHP;
        // write input + feedback of the (high-passed) wet → resonant jet
        buf[wpos] = x + feedback * wet;
        wpos = (wpos + 1) % BUF;
        return x * (1.0f - 0.5f * mix) + wet * mix;
    }
};

class BassFlangerPlugin : public Plugin {
    Flanger L, R;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kRate], fParams[kDepth], fParams[kFilter], fParams[kMix]);
        R.setParams(fParams[kRate], fParams[kDepth], fParams[kFilter], fParams[kMix]);
    }
public:
    BassFlangerPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBassFlangerDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr);
        L.reset(0.0f); R.reset(1.5708f);
        recalc();
    }
protected:
    const char* getLabel()       const override { return "BassFlanger"; }
    const char* getDescription() const override { return "Boss BF-2/BF-3 bass flanger"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'F', 'l'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassFlangerNames[i]; p.symbol = kBassFlangerSymbols[i];
        p.ranges.min = kBassFlangerMin[i]; p.ranges.max = kBassFlangerMax[i]; p.ranges.def = kBassFlangerDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(0.0f); R.reset(1.5708f); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFlangerPlugin)
};

Plugin* createPlugin() { return new BassFlangerPlugin(); }

END_NAMESPACE_DISTRHO
