/*
 * BassFilterEcho — vintage tape-echo model for Bass_Pedal_BassFilterEcho.
 *
 * Same delay-line engine as the Bass Filter Delay, but voiced like tape:
 *   - darker feedback-loop low-pass (tape head/azimuth roll-off),
 *   - stronger soft saturation in the loop (tape compression/drive),
 *   - wow & flutter: the read position is modulated by a slow ~0.6 Hz wow
 *     plus a faster ~6 Hz flutter, giving the repeats their pitch wobble.
 *   Time/Feedback/Mix/Filter map the same as the delay.
 */
#include "DistrhoPlugin.hpp"
#include "BassFilterEchoParams.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

static const int kMaxDelay = 77000;   // 0.8 s @ 96 kHz

class TapeEcho {
    float fs = 48000.f;
    float buf[kMaxDelay];
    int   wr = 0;
    float lpZ = 0.f, cLP = 0.2f;
    float delSmooth = 9600.f, targetDel = 9600.f;
    float fb = 0.45f, mix = 0.4f;
    float wowPh = 0.f, flutPh = 0.f, wowInc = 0.f, flutInc = 0.f, wowDepth = 0.f, flutDepth = 0.f;
public:
    void setSampleRate(float s) {
        fs = (s > 0.f) ? s : 48000.f;
        wowInc  = 6.2831853f * 0.6f / fs;
        flutInc = 6.2831853f * 6.0f / fs;
        wowDepth  = 0.0016f * fs;     // ~1.6 ms slow wow
        flutDepth = 0.0004f * fs;     // ~0.4 ms flutter
        reset();
    }
    void reset() { std::memset(buf, 0, sizeof(buf)); wr = 0; lpZ = 0.f; wowPh = flutPh = 0.f; }
    void setParams(float timeP, float fbP, float mixP, float filterP) {
        const float ms = 40.0f + timeP * 660.0f;          // 40 .. 700 ms
        targetDel = ms * 0.001f * fs;
        const float maxd = (float)(kMaxDelay - 4);
        if (targetDel > maxd) targetDel = maxd;
        fb  = fbP * 0.95f;
        mix = mixP;
        cLP = onePoleCoef(400.0f * std::pow(2.0f, filterP * 3.4f), fs);  // 400 .. ~4200 Hz (darker)
    }
    inline float process(float x) {
        delSmooth += 0.0007f * (targetDel - delSmooth);

        // wow & flutter modulation of the read position
        wowPh += wowInc;   if (wowPh  > 6.2831853f) wowPh  -= 6.2831853f;
        flutPh += flutInc; if (flutPh > 6.2831853f) flutPh -= 6.2831853f;
        const float mod = wowDepth * std::sin(wowPh) + flutDepth * std::sin(flutPh);

        float rp = (float)wr - delSmooth - mod;
        while (rp < 0.f)             rp += (float)kMaxDelay;
        while (rp >= (float)kMaxDelay) rp -= (float)kMaxDelay;
        int i0 = (int)rp; float fr = rp - (float)i0;
        int i1 = i0 + 1; if (i1 >= kMaxDelay) i1 -= kMaxDelay;
        float wet = buf[i0] + fr * (buf[i1] - buf[i0]);

        // tape head roll-off (cumulative)
        lpZ += cLP * (wet - lpZ);
        wet = lpZ;

        // tape saturation in the loop (warmer/stronger than the BBD delay)
        float wn = x + wet * fb;
        wn = std::tanh(wn * 1.2f) * 0.95f;
        buf[wr] = wn;
        if (++wr >= kMaxDelay) wr = 0;

        return x * (1.0f - 0.3f * mix) + wet * mix;
    }
};

class BassFilterEchoPlugin : public Plugin {
    TapeEcho L, R;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kTime], fParams[kFeedback], fParams[kMix], fParams[kFilter]);
        R.setParams(fParams[kTime], fParams[kFeedback], fParams[kMix], fParams[kFilter]);
    }
public:
    BassFilterEchoPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBassFilterEchoDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); recalc();
    }
protected:
    const char* getLabel()       const override { return "BassFilterEcho"; }
    const char* getDescription() const override { return "Vintage tape echo"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'F', 'e'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassFilterEchoNames[i]; p.symbol = kBassFilterEchoSymbols[i];
        p.ranges.min = kBassFilterEchoMin[i]; p.ranges.max = kBassFilterEchoMax[i]; p.ranges.def = kBassFilterEchoDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFilterEchoPlugin)
};

Plugin* createPlugin() { return new BassFilterEchoPlugin(); }

END_NAMESPACE_DISTRHO
