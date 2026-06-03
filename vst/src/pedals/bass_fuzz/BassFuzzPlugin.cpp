/*
 * BassFuzz — EHX Bass Big Muff Pi model for Rocksmith's Bass_Pedal_BassFuzz.
 *
 * Big Muff topology: input coupling → two cascaded high-gain stages with
 * symmetric (diode-pair) soft clipping and interstage low-pass roll-off (the
 * smooth, compressed, sustaining Muff fuzz) → the Big Muff tone stack (a
 * low-pass and high-pass branch crossfaded by the Tone knob, with the
 * characteristic mid scoop at noon) → output. The Bass version adds a clean
 * low-end blend (the "Filter" knob) so the fuzz doesn't lose its lows.
 *
 * Rocksmith knobs: Gain (= Sustain / drive), Tone, Filter (clean-bass blend).
 */
#include "DistrhoPlugin.hpp"
#include "BassFuzzParams.h"
#include "../_shared/automakeup.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

class BigMuff {
    float fs = 48000.f;
    // one-pole lowpass states
    float zInHP = 0.f, zBass = 0.f, zS1 = 0.f, zS2 = 0.f, zToneLP = 0.f, zToneHP = 0.f, zOutDC = 0.f;
    // coefficients
    float cInHP, cBass, cS1, cS2, cToneLP, cToneHP, cOutDC;
    // params (derived)
    float drive = 40.f, tone = 0.55f, dryBlend = 0.45f, makeup = 0.24f;   // makeup lowered 0.55→0.24: clip kept, output level-matched to bypass

    // HARD clip (not tanh) — the square edges + their high harmonics are the
    // gritty, "8-bit" Big Muff character. Drive is kept moderate so the
    // hardness adds grit to the SIGNAL without lifting the noise floor.
    static inline float softclip(float x) { return x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x); }
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; recalcFilters(); }
    void recalcFilters() {
        cInHP   = onePoleCoef(45.f,   fs);   // remove sub-rumble before clipping
        cBass   = onePoleCoef(180.f,  fs);   // clean low-end tap for the blend
        cS1     = onePoleCoef(9000.f, fs);   // light interstage roll-off — keep the gritty highs
        cS2     = onePoleCoef(9000.f, fs);
        cToneLP = onePoleCoef(700.f,  fs);   // Big Muff tone — bass-leaning corners
        cToneHP = onePoleCoef(700.f,  fs);
        cOutDC  = onePoleCoef(18.f,   fs);   // output DC blocker
    }
    void setParams(float gain, float toneP, float filterP) {
        // Moderate drive (the hard clip does the grit). High enough for a
        // sustaining Muff fuzz, low enough that it doesn't saturate the input
        // noise floor (the old 126× pushed -50 dB hiss up to signal level →
        // white noise). ~3 .. 48; gain 0.8 (RS default) → ~40.
        drive    = 3.0f + gain * 45.0f;
        tone     = toneP;
        dryBlend = filterP;
    }
    inline float process(float x) {
        // clean low-end tap (for the bass blend) + input high-pass
        zBass += cBass * (x - zBass);
        const float bass = zBass;
        zInHP += cInHP * (x - zInHP);
        const float xin = x - zInHP;
        // stage 1: gain + soft clip + interstage LP
        float s = softclip(drive * xin);
        zS1 += cS1 * (s - zS1); s = zS1;
        // stage 2 — fixed modest gain (re-squares for more grit; re-applying
        // the full drive here is what saturated the noise floor)
        s = softclip(2.0f * s);
        zS2 += cS2 * (s - zS2); s = zS2;
        // Big Muff tone: crossfade LP and HP (mid scoop at tone=0.5)
        zToneLP += cToneLP * (s - zToneLP);
        const float lo = zToneLP;
        zToneHP += cToneHP * (s - zToneHP);
        const float hi = s - zToneHP;
        float out = (1.0f - tone) * lo * 1.6f + tone * hi;
        // Blend clean low end back in — the bass-specific "Filter" knob. Made
        // clearly audible: it ducks the fuzz a little and brings up the clean
        // bass, so it sweeps from thin/scooped fuzz (min) to fat fuzz with
        // solid lows (max).
        out = out * (1.0f - 0.4f * dryBlend) + bass * dryBlend * 3.0f;
        // output DC blocker + makeup
        zOutDC += cOutDC * (out - zOutDC);
        return (out - zOutDC) * makeup;
    }
};

class BassFuzzPlugin : public Plugin {
    BigMuff L, R;
    RBAutoMakeup makeupL, makeupR;
    float fParams[kParamCount];
    void recalc() { L.setParams(fParams[kGain], fParams[kTone], fParams[kFilter]);
                    R.setParams(fParams[kGain], fParams[kTone], fParams[kFilter]); }
public:
    BassFuzzPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBassFuzzDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr);
        makeupL.setSampleRate(sr); makeupR.setSampleRate(sr); recalc();
    }
protected:
    const char* getLabel()       const override { return "BassFuzz"; }
    const char* getDescription() const override { return "Bass Big Muff Pi fuzz"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'F', 'z'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassFuzzNames[i]; p.symbol = kBassFuzzSymbols[i];
        p.ranges.min = kBassFuzzMin[i]; p.ranges.max = kBassFuzzMax[i]; p.ranges.def = kBassFuzzDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); makeupL.snap(); makeupR.snap(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); makeupL.setSampleRate((float)r); makeupR.setSampleRate((float)r); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        // Auto makeup-gain: match output loudness to the dry input (clip not level).
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = makeupL.process(iL[i], L.process(iL[i])); oR[i] = makeupR.process(iR[i], R.process(iR[i])); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFuzzPlugin)
};

Plugin* createPlugin() { return new BassFuzzPlugin(); }

END_NAMESPACE_DISTRHO
