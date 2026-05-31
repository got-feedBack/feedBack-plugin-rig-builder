/*
 * Studio Graphic EQ — 5-band API-550-style sweepable EQ, DPF VST3.
 * Bass = low shelf, LoMid/Mid/HiMid = peaking with PROPORTIONAL Q (no Q knob;
 * Q narrows as gain rises — the API signature), Treble = high shelf. Cascaded
 * RBJ biquads.
 */
#include "DistrhoPlugin.hpp"
#include "SGEqParams.h"
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
            b0n = 1 + alpha * A; b1n = -2 * cw; b2n = 1 - alpha * A;
            a0  = 1 + alpha / A; a1n = -2 * cw; a2n = 1 - alpha / A;
        } else {
            const float alpha = sw * 0.70710678f;
            const float ta = 2.0f * sqrtf(A) * alpha;
            if (mode == LOWSHELF) {
                b0n = A * ((A+1) - (A-1)*cw + ta);  b1n = 2*A*((A-1) - (A+1)*cw);  b2n = A*((A+1) - (A-1)*cw - ta);
                a0  = (A+1) + (A-1)*cw + ta;         a1n = -2*((A-1) + (A+1)*cw);   a2n = (A+1) + (A-1)*cw - ta;
            } else {
                b0n = A * ((A+1) + (A-1)*cw + ta);  b1n = -2*A*((A-1) + (A+1)*cw); b2n = A*((A+1) + (A-1)*cw - ta);
                a0  = (A+1) - (A-1)*cw + ta;         a1n = 2*((A-1) - (A+1)*cw);    a2n = (A+1) - (A-1)*cw - ta;
            }
        }
        b0 = b0n/a0; b1 = b1n/a0; b2 = b2n/a0; a1 = a1n/a0; a2 = a2n/a0;
    }
    inline float process(float x) {
        const float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y; return y;
    }
};

class SGEqChannel {
    Biquad bass, lomid, mid, himid, treble;
    float fs;
public:
    SGEqChannel() { fs = 48000.f; bass.reset(); lomid.reset(); mid.reset(); himid.reset(); treble.reset(); }
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; }
    void update(const float* p) {
        const float dLo = sgDb(p[gLoMid]), dMi = sgDb(p[gMid]), dHi = sgDb(p[gHiMid]);
        bass.set(LOWSHELF,   sgFBass(p[gBassFreq]),     sgDb(p[gBass]),  0.7f,          fs);
        lomid.set(PEAK,      sgFLoMid(p[gLoMidFreq]),   dLo,             sgPropQ(dLo),  fs);
        mid.set(PEAK,        sgFMid(p[gMidFreq]),       dMi,             sgPropQ(dMi),  fs);
        himid.set(PEAK,      sgFHiMid(p[gHiMidFreq]),   dHi,             sgPropQ(dHi),  fs);
        treble.set(HIGHSHELF, sgFTreble(p[gTrebleFreq]), sgDb(p[gTreble]), 0.7f,        fs);
    }
    inline float process(float x) { return treble.process(himid.process(mid.process(lomid.process(bass.process(x))))); }
};

class SGEqPlugin : public Plugin {
    SGEqChannel L, R;
    float fParams[gNumParams];
public:
    SGEqPlugin() : Plugin(gNumParams, 0, 0) {
        for (int i = 0; i < gNumParams; ++i) fParams[i] = 0.5f;
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.update(fParams); R.update(fParams);
    }
protected:
    const char* getLabel()       const override { return "StudioGraphicEQ"; }
    const char* getDescription() const override { return "5-band API-style sweepable EQ (proportional Q)"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R','S','G','E'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)gNumParams) return;
        p.hints = kParameterIsAutomatable;
        p.name = kSgNames[i]; p.symbol = kSgNames[i];
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 0.5f;
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)gNumParams) ? fParams[i] : 0.5f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)gNumParams) { fParams[i] = v; L.update(fParams); R.update(fParams); } }
    void sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.update(fParams); R.update(fParams); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1]; float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SGEqPlugin)
};

Plugin* createPlugin() { return new SGEqPlugin(); }

END_NAMESPACE_DISTRHO
