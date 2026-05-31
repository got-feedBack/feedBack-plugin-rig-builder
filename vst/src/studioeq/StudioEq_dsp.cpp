/*
 * Studio EQ — clean 4-band parametric (GML-style), DPF VST3.
 * Cascade: low shelf (Bass) → peaking (LoMid) → peaking (HiMid) → high shelf
 * (Treble). RBJ biquads, mathematically transparent (the GML's signature).
 */
#include "DistrhoPlugin.hpp"
#include "StudioEqParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

enum BiqMode { LOWSHELF, PEAK, HIGHSHELF };

struct Biquad {
    float b0, b1, b2, a1, a2, x1, x2, y1, y2;
    void reset() { x1 = x2 = y1 = y2 = 0.f; b0 = 1.f; b1 = b2 = a1 = a2 = 0.f; }
    void set(BiqMode mode, float freq, float gainDb, float Q, float fs) {
        const float A  = powf(10.0f, gainDb / 40.0f);
        const float w0 = 6.28318530718f * freq / fs;
        const float cw = cosf(w0), sw = sinf(w0);
        float b0n, b1n, b2n, a0, a1n, a2n;
        if (mode == PEAK) {
            const float alpha = sw / (2.0f * Q);
            b0n = 1 + alpha * A; b1n = -2 * cw;       b2n = 1 - alpha * A;
            a0  = 1 + alpha / A; a1n = -2 * cw;       a2n = 1 - alpha / A;
        } else {
            const float alpha = sw * 0.70710678f;     // shelf slope S = 1
            const float ta = 2.0f * sqrtf(A) * alpha;
            if (mode == LOWSHELF) {
                b0n = A * ((A + 1) - (A - 1) * cw + ta);
                b1n = 2 * A * ((A - 1) - (A + 1) * cw);
                b2n = A * ((A + 1) - (A - 1) * cw - ta);
                a0  = (A + 1) + (A - 1) * cw + ta;
                a1n = -2 * ((A - 1) + (A + 1) * cw);
                a2n = (A + 1) + (A - 1) * cw - ta;
            } else { // HIGHSHELF
                b0n = A * ((A + 1) + (A - 1) * cw + ta);
                b1n = -2 * A * ((A - 1) + (A + 1) * cw);
                b2n = A * ((A + 1) + (A - 1) * cw - ta);
                a0  = (A + 1) - (A - 1) * cw + ta;
                a1n = 2 * ((A - 1) - (A + 1) * cw);
                a2n = (A + 1) - (A - 1) * cw - ta;
            }
        }
        b0 = b0n / a0; b1 = b1n / a0; b2 = b2n / a0; a1 = a1n / a0; a2 = a2n / a0;
    }
    inline float process(float x) {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

class StudioEqChannel {
    Biquad bass, lomid, himid, treble;
    float fs;
public:
    StudioEqChannel() { fs = 48000.f; bass.reset(); lomid.reset(); himid.reset(); treble.reset(); }
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; }
    void update(const float* p) {
        bass.set(LOWSHELF,   seqFBass(p[kBassFreq]),     seqDb(p[kBass]),   0.7f,            fs);
        lomid.set(PEAK,      seqFLoMid(p[kLoMidFreq]),   seqDb(p[kLoMid]),  seqQ(p[kLoMidQ]), fs);
        himid.set(PEAK,      seqFHiMid(p[kHiMidFreq]),   seqDb(p[kHiMid]),  seqQ(p[kHiMidQ]), fs);
        treble.set(HIGHSHELF, seqFTreble(p[kTrebleFreq]), seqDb(p[kTreble]), 0.7f,            fs);
    }
    inline float process(float x) { return treble.process(himid.process(lomid.process(bass.process(x)))); }
};

class StudioEqPlugin : public Plugin {
    StudioEqChannel L, R;
    float fParams[kNumParams];
public:
    StudioEqPlugin() : Plugin(kNumParams, 0, 0) {
        for (int i = 0; i < kNumParams; ++i) fParams[i] = 0.5f;   // flat (0 dB), mid freqs, Q~1.1
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.update(fParams); R.update(fParams);
    }
protected:
    const char* getLabel()       const override { return "StudioEQ"; }
    const char* getDescription() const override { return "Clean 4-band parametric EQ (GML-style)"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R','S','E','Q'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kNumParams) return;
        p.hints = kParameterIsAutomatable;
        p.name = kSeqNames[i]; p.symbol = kSeqNames[i];
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 0.5f;
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kNumParams) ? fParams[i] : 0.5f; }
    void  setParameterValue(uint32_t i, float v) override {
        if (i < (uint32_t)kNumParams) { fParams[i] = v; L.update(fParams); R.update(fParams); }
    }
    void sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.update(fParams); R.update(fParams); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0];      float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioEqPlugin)
};

Plugin* createPlugin() { return new StudioEqPlugin(); }

END_NAMESPACE_DISTRHO
