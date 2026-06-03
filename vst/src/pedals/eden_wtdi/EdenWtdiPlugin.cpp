/*
 * EdenWtdi — Eden WTDI bass preamp/DI model for Bass_Pedal_EdenWTDI.
 *
 * Signal flow (faithful to the real WTDI / WT-series preamp):
 *   input -> Gain (preamp drive, gentle tube-ish soft clip)
 *         -> Compressor (optical-style, amount = Comp)
 *         -> 3-band active EQ:  low shelf (Bass, corner set by Bass Boost)
 *                               mid peak  (Mid, centre set by Mid Shift)
 *                               high shelf (Treble)
 *         -> Enhance contour (boost lows+highs, scoop mids; amount = Enhance)
 *         -> Master output level.
 * Bass/Mid/Treble are ±15 dB; param 0.5 = flat. Bass Boost / Mid Shift are the
 * pedal's two red switches (RS LoShift / MidShift) modeled as 0/1 toggles.
 */
#include "DistrhoPlugin.hpp"
#include "EdenWtdiParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

// ── RBJ biquad (transposed direct form II) ───────────────────────────────────
class Biquad {
    float b0=1, b1=0, b2=0, a1=0, a2=0;
    float z1=0, z2=0;
public:
    void reset() { z1 = z2 = 0.f; }
    inline float process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void setLowShelf(float fc, float dB, float fs) {
        const float A = std::pow(10.f, dB / 40.f);
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw * 0.5f * 1.4142135f;           // S ~ 1
        const float sA = std::sqrt(A), tsAa = 2.f * sA * alpha;
        const float a0 =       (A + 1) + (A - 1) * cw + tsAa;
        b0 =  A * ((A + 1) - (A - 1) * cw + tsAa) / a0;
        b1 = 2*A * ((A - 1) - (A + 1) * cw)        / a0;
        b2 =  A * ((A + 1) - (A - 1) * cw - tsAa)  / a0;
        a1 = -2 * ((A - 1) + (A + 1) * cw)         / a0;
        a2 =      ((A + 1) + (A - 1) * cw - tsAa)  / a0;
    }
    void setHighShelf(float fc, float dB, float fs) {
        const float A = std::pow(10.f, dB / 40.f);
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw * 0.5f * 1.4142135f;
        const float sA = std::sqrt(A), tsAa = 2.f * sA * alpha;
        const float a0 =       (A + 1) - (A - 1) * cw + tsAa;
        b0 =  A * ((A + 1) + (A - 1) * cw + tsAa) / a0;
        b1 = -2*A * ((A - 1) + (A + 1) * cw)      / a0;
        b2 =  A * ((A + 1) + (A - 1) * cw - tsAa) / a0;
        a1 =  2 * ((A - 1) - (A + 1) * cw)        / a0;
        a2 =      ((A + 1) - (A - 1) * cw - tsAa) / a0;
    }
    void setPeak(float fc, float dB, float Q, float fs) {
        const float A = std::pow(10.f, dB / 40.f);
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha / A;
        b0 = (1 + alpha * A) / a0;
        b1 = (-2 * cw)       / a0;
        b2 = (1 - alpha * A) / a0;
        a1 = (-2 * cw)       / a0;
        a2 = (1 - alpha / A) / a0;
    }
    void setBypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

class WtdiChannel {
    float fs = 48000.f;
    Biquad bqLow, bqMid, bqHigh;       // tone stack
    Biquad enhLow, enhMid, enhHigh;    // Enhance contour
    float drive = 1.f, master = 1.f;
    // compressor
    float compThr = 1.f, compRatio = 1.f, compMakeup = 1.f, env = 0.f;
    float atkC = 0.f, relC = 0.f;
    bool  compOn = false;

    static inline float msCoef(float ms, float fs) {
        return std::exp(-1.0f / (0.001f * ms * fs));
    }
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; atkC = msCoef(8.f, fs); relC = msCoef(120.f, fs); }
    void reset() {
        bqLow.reset(); bqMid.reset(); bqHigh.reset();
        enhLow.reset(); enhMid.reset(); enhHigh.reset(); env = 0.f;
    }

    void setParams(float gain, float enhance, float comp, float masterP,
                   float bass, float mid, float treble,
                   float bassBoost, float midShift) {
        // ── input drive ──
        drive = 1.0f + gain * 4.0f;                     // 1 .. 5×

        // ── tone stack (±15 dB, 0.5 = flat) ──
        const bool bb = bassBoost > 0.5f;
        const float bassDb = (bass - 0.5f) * 30.f + (bb ? 4.0f : 0.0f);
        bqLow.setLowShelf(bb ? 45.f : 75.f, bassDb, fs);
        const float midFc = (midShift > 0.5f) ? 1200.f : 600.f;
        bqMid.setPeak(midFc, (mid - 0.5f) * 30.f, 0.8f, fs);
        bqHigh.setHighShelf(4000.f, (treble - 0.5f) * 30.f, fs);

        // ── Enhance contour (boost lows+highs, scoop mids) ──
        if (enhance > 0.001f) {
            enhLow.setLowShelf(110.f, enhance * 6.0f, fs);
            enhMid.setPeak(750.f, enhance * -9.0f, 1.0f, fs);
            enhHigh.setHighShelf(3500.f, enhance * 6.0f, fs);
        } else {
            enhLow.setBypass(); enhMid.setBypass(); enhHigh.setBypass();
        }

        // ── compressor ──
        compOn = comp > 0.001f;
        compThr   = 1.0f - comp * 0.6f;                 // 1.0 .. 0.4
        compRatio = 1.0f + comp * 4.0f;                 // 1 .. 5
        compMakeup = 1.0f + comp * 0.6f;                // gentle make-up

        // ── output (Master 0.7 ~ unity) ──
        master = masterP / 0.7f;
    }

    inline float process(float x) {
        // input drive — gentle soft clip, level-preserving at low gain
        const float sat = std::tanh(drive * x) * (1.0f / (0.3f + 0.7f * drive));

        // compressor (peak-following, downward)
        float s = sat;
        if (compOn) {
            const float a = std::fabs(s);
            const float c = (a > env) ? atkC : relC;
            env = c * env + (1.0f - c) * a;
            if (env > compThr) {
                const float over = env - compThr;
                const float comped = compThr + over / compRatio;
                s *= (comped / env);
            }
            s *= compMakeup;
        }

        // tone stack
        s = bqLow.process(s);
        s = bqMid.process(s);
        s = bqHigh.process(s);
        // enhance contour
        s = enhLow.process(s);
        s = enhMid.process(s);
        s = enhHigh.process(s);

        return s * master;
    }
};

class EdenWtdiPlugin : public Plugin {
    WtdiChannel L, R;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kGain], fParams[kEnhance], fParams[kComp], fParams[kMaster],
                    fParams[kBass], fParams[kMid], fParams[kTreble],
                    fParams[kBassBoost], fParams[kMidShift]);
        R.setParams(fParams[kGain], fParams[kEnhance], fParams[kComp], fParams[kMaster],
                    fParams[kBass], fParams[kMid], fParams[kTreble],
                    fParams[kBassBoost], fParams[kMidShift]);
    }
public:
    EdenWtdiPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kEdenWtdiDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "EdenWTDI"; }
    const char* getDescription() const override { return "Eden WTDI bass preamp/DI"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'E', 'w'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == kBassBoost || i == kMidShift) p.hints |= kParameterIsBoolean;
        p.name = kEdenWtdiNames[i]; p.symbol = kEdenWtdiSymbols[i];
        p.ranges.min = kEdenWtdiMin[i]; p.ranges.max = kEdenWtdiMax[i]; p.ranges.def = kEdenWtdiDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EdenWtdiPlugin)
};

Plugin* createPlugin() { return new EdenWtdiPlugin(); }

END_NAMESPACE_DISTRHO
