/*
 * Studio Graphic EQ — API 550L (4-band discrete EQ) model, DPF VST3.
 *
 * Voiced from the API 550L schematic + set-up sheet (4 bands LF/LMF/HMF/HF, each
 * a stepped Frequency selector + stepped Boost/Cut, LF/HF switchable PEAK/SHELF).
 * Faithful details:
 *   • STEPPED Frequency (7 real API points/band) and Boost/Cut (0,±2,±4,±6,±9,
 *     ±12 dB) — snapped in SGEqParams.h.
 *   • PROPORTIONAL Q — the bell narrows as gain rises (the API signature).
 *   • LF/HF PEAK or SHELF (the front-panel switch).
 *   • COLOUR — the discrete 2510 op-amps + output transformer make the 550 punchy,
 *     NOT transparent: a gentle asymmetric saturation (2nd + 3rd harmonic, DC-
 *     blocked) adds that character, well below audible distortion at normal level.
 * Cascade: LF → LMF → HMF → HF (RBJ biquads), then the colour stage.
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
    Biquad lf, lmf, hmf, hf;
    float fs;
    float dcX, dcY;   // DC blocker for the asymmetric colour stage
public:
    SGEqChannel() { fs = 48000.f; lf.reset(); lmf.reset(); hmf.reset(); hf.reset(); dcX = dcY = 0.f; }
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; }
    void update(const float* p) {
        const float gLF = sgGainDb(p[gBass]), gLM = sgGainDb(p[gLoMid]), gHM = sgGainDb(p[gHiMid]), gHFv = sgGainDb(p[gTreble]);
        // LF: peak or shelf
        if (sgIsShelf(p[gBassShelf])) lf.set(LOWSHELF, sgFLF(p[gBassFreq]), gLF, 0.707f, fs);
        else                          lf.set(PEAK,     sgFLF(p[gBassFreq]), gLF, sgPropQ(gLF), fs);
        lmf.set(PEAK, sgFLMF(p[gLoMidFreq]), gLM, sgPropQ(gLM), fs);
        hmf.set(PEAK, sgFHMF(p[gHiMidFreq]), gHM, sgPropQ(gHM), fs);
        // HF: peak or shelf
        if (sgIsShelf(p[gTrebleShelf])) hf.set(HIGHSHELF, sgFHF(p[gTrebleFreq]), gHFv, 0.707f, fs);
        else                            hf.set(PEAK,      sgFHF(p[gTrebleFreq]), gHFv, sgPropQ(gHFv), fs);
    }
    inline float process(float x) {
        float y = hf.process(hmf.process(lmf.process(lf.process(x))));
        // API discrete + transformer colour: gentle saturation, biased toward 2nd
        // harmonic (transformer/Class-A warmth) and subtle at nominal level.
        const float d = 0.70f;
        const float s = std::tanh((y + 0.05f * y * y) * d) * (1.0f / d);   // 2nd-dominant, subtle
        const float o = s - dcX + 0.9995f * dcY;                            // DC blocker
        dcX = s; dcY = o;
        return o;
    }
};

class SGEqPlugin : public Plugin {
    SGEqChannel L, R;
    float fParams[gNumParams];
public:
    SGEqPlugin() : Plugin(gNumParams, 0, 0) {
        for (int i = 0; i < gNumParams; ++i) fParams[i] = kSgDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.update(fParams); R.update(fParams);
    }
protected:
    const char* getLabel()       const override { return "StudioGraphicEQ"; }
    const char* getDescription() const override { return "API 550L 4-band discrete EQ"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R','S','G','E'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)gNumParams) return;
        p.name = kSgNames[i]; p.symbol = kSgNames[i];
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = kSgDef[i];
        if (i == (uint32_t)gBassShelf || i == (uint32_t)gTrebleShelf)
            p.hints = kParameterIsAutomatable | kParameterIsBoolean;
        else
            p.hints = kParameterIsAutomatable;
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
