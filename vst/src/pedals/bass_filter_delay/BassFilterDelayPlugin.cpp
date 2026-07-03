/*
 * BassFilterDelay — Boss DM-2 analog (BBD) delay model for
 * Bass_Pedal_BassFilterDelay.
 *
 * A fractional-delay line with a low-pass + gentle saturation in the feedback
 * loop, so each repeat darkens and softens cumulatively the way a bucket-
 * brigade analog delay does. Time is smoothed to avoid zipper noise.
 *   Time     -> 30 .. 700 ms
 *   Feedback -> 0 .. 0.95 regeneration
 *   Filter   -> feedback-loop low-pass cutoff (600 Hz .. ~7 kHz); lower = the
 *               warmer, darker classic BBD voice
 *   Mix      -> wet/dry blend
 */
#include "DistrhoPlugin.hpp"
#include "BassFilterDelayParams.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

// 0.8 s buffer at up to 96 kHz
static const int kMaxDelay = 77000;

class DelayLine {
    float fs = 48000.f;
    float buf[kMaxDelay];
    int   wr = 0;
    float lpZ = 0.f, cLP = 0.2f;
    float delSmooth = 9600.f;     // smoothed delay in samples
    float targetDel = 9600.f;
    float fb = 0.4f, mix = 0.4f;
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; reset(); }
    void reset() { std::memset(buf, 0, sizeof(buf)); wr = 0; lpZ = 0.f; }
    void setParams(float timeP, float fbP, float mixP, float filterP) {
        const float ms = 30.0f + timeP * 670.0f;          // 30 .. 700 ms
        targetDel = ms * 0.001f * fs;
        const float maxd = (float)(kMaxDelay - 4);
        if (targetDel > maxd) targetDel = maxd;
        fb  = fbP * 0.95f;
        mix = mixP;
        cLP = onePoleCoef(600.0f * std::pow(2.0f, filterP * 3.6f), fs);  // 600 .. ~7200 Hz
    }
    inline float process(float x) {
        // smooth the delay time (one-pole, ~30 ms)
        delSmooth += 0.0007f * (targetDel - delSmooth);

        // fractional read
        float rp = (float)wr - delSmooth;
        while (rp < 0.f) rp += (float)kMaxDelay;
        int i0 = (int)rp; float fr = rp - (float)i0;
        int i1 = i0 + 1; if (i1 >= kMaxDelay) i1 -= kMaxDelay;
        float wet = buf[i0] + fr * (buf[i1] - buf[i0]);

        // darken the repeat (cumulative LP in the loop)
        lpZ += cLP * (wet - lpZ);
        wet = lpZ;

        // write input + gently-saturated feedback
        float wn = x + wet * fb;
        wn = std::tanh(wn * 0.8f) * 1.25f;     // soft analog clip + makeup
        buf[wr] = wn;
        if (++wr >= kMaxDelay) wr = 0;

        return x * (1.0f - 0.3f * mix) + wet * mix;
    }
};

class BassFilterDelayPlugin : public Plugin {
    DelayLine L, R;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kTime], fParams[kFeedback], fParams[kMix], fParams[kFilter]);
        R.setParams(fParams[kTime], fParams[kFeedback], fParams[kMix], fParams[kFilter]);
    }
public:
    BassFilterDelayPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBassFilterDelayDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); recalc();
    }
protected:
    const char* getLabel()       const override { return "BassFilterDelay"; }
    const char* getDescription() const override { return "DM-2 analog delay"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'F', 'd'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassFilterDelayNames[i]; p.symbol = kBassFilterDelaySymbols[i];
        p.ranges.min = kBassFilterDelayMin[i]; p.ranges.max = kBassFilterDelayMax[i]; p.ranges.def = kBassFilterDelayDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFilterDelayPlugin)
};

Plugin* createPlugin() { return new BassFilterDelayPlugin(); }

END_NAMESPACE_DISTRHO
