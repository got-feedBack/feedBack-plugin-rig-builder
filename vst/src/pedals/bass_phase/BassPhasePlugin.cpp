/*
 * BassPhase — analog bass phaser model for Bass_Pedal_BassPhase.
 *
 * Four cascaded first-order all-pass stages whose common break frequency is
 * swept by a sine LFO; the last stage feeds back into the input for the
 * resonant "regeneration" character, and the phased signal is summed with the
 * dry signal to form the moving notches. The wet path is high-passed at ~85 Hz
 * so a bass keeps its low fundamentals (a guitar phaser would wash them out).
 *   Rate   -> LFO speed (0.05 .. 8 Hz)
 *   Depth  -> sweep range in octaves + feedback amount
 *   Mix    -> dry/wet blend (full ≈ 50/50, deepest notches)
 *   Filter -> sweep centre frequency (150 .. ~1500 Hz)
 */
#include "DistrhoPlugin.hpp"
#include "BassPhaseParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

static const int kStages = 4;

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

class Phaser {
    float fs = 48000.f;
    float xPrev[kStages] = {0}, yPrev[kStages] = {0};
    float fbState = 0.f;
    float lfoPhase = 0.f, lfoInc = 0.001f;
    float baseFc = 400.f, depthOct = 1.5f, fb = 0.4f, mix = 0.3f;
    float hpZ = 0.f, cHP = 0.02f;
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; cHP = onePoleCoef(85.f, fs); }
    void reset() { for (int i=0;i<kStages;++i){xPrev[i]=yPrev[i]=0.f;} fbState=0.f; hpZ=0.f; }
    void setParams(float rate, float depth, float mixP, float filter) {
        lfoInc   = 6.2831853f * (0.05f + rate * 7.95f) / fs;
        depthOct = 0.3f + depth * 2.5f;                 // octaves of sweep
        baseFc   = 150.0f * std::pow(2.0f, filter * 3.3f); // 150 .. ~1490 Hz
        fb       = 0.25f + depth * 0.45f;               // regeneration
        mix      = mixP * 0.5f;                          // up to classic 50/50
    }
    inline float process(float x) {
        lfoPhase += lfoInc; if (lfoPhase > 6.2831853f) lfoPhase -= 6.2831853f;
        const float lfo = std::sin(lfoPhase);
        float fc = baseFc * std::pow(2.0f, lfo * depthOct);
        if (fc < 40.f) fc = 40.f; const float nyq = fs * 0.45f; if (fc > nyq) fc = nyq;
        const float t = std::tan(3.14159265f * fc / fs);
        const float a = (t - 1.0f) / (t + 1.0f);

        float s = x + fbState * fb;
        for (int i = 0; i < kStages; ++i) {
            const float in = s;
            s = a * in + xPrev[i] - a * yPrev[i];
            xPrev[i] = in; yPrev[i] = s;
        }
        fbState = s;
        // keep the lows solid: high-pass the wet path so bass fundamentals
        // pass through dry rather than getting phased into mush
        hpZ += cHP * (s - hpZ);
        const float wet = s - hpZ;
        return x * (1.0f - mix) + wet * mix * 2.0f;
    }
};

class BassPhasePlugin : public Plugin {
    Phaser L, R;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kRate], fParams[kDepth], fParams[kMix], fParams[kFilter]);
        R.setParams(fParams[kRate], fParams[kDepth], fParams[kMix], fParams[kFilter]);
    }
public:
    BassPhasePlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBassPhaseDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "BassPhase"; }
    const char* getDescription() const override { return "Analog bass phaser"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'P', 'h'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassPhaseNames[i]; p.symbol = kBassPhaseSymbols[i];
        p.ranges.min = kBassPhaseMin[i]; p.ranges.max = kBassPhaseMax[i]; p.ranges.def = kBassPhaseDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassPhasePlugin)
};

Plugin* createPlugin() { return new BassPhasePlugin(); }

END_NAMESPACE_DISTRHO
