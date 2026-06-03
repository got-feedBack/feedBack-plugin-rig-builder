/*
 * BassDistortion — Pro Co RAT model for Bass_Pedal_BassDistortion.
 *
 * RAT topology: input coupling → LM308 op-amp with enormous, frequency-shaped
 * gain (the feedback cap rolls highs, giving the RAT's midrange grit) → a pair
 * of hard silicon clipping diodes to ground → the passive "Filter" low-pass →
 * output. Modeled:
 *   input HP → Drive gain → pre-clip LP (Tone = grit character) → hard clip
 *   → post-clip LP (Filter = the RAT tone) → make-up.
 * Rocksmith knobs: Gain (=Distortion), Filter (=the RAT Filter), Tone (pre-clip).
 */
#include "DistrhoPlugin.hpp"
#include "BassDistortionParams.h"
#include "../_shared/automakeup.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

class Rat {
    float fs = 48000.f;
    float zHP = 0.f, zPre = 0.f, zPost = 0.f;
    float cHP, cPre, cPost;
    float drive = 100.f;

    // hard silicon-diode clip — the RAT's diodes clip HARD, which is what gives
    // the gritty buzz. Pure hard clip (the post-clip Filter low-pass tames the
    // worst fizz). The old cubic was too soft → sounded like a clean boost.
    static inline float clip(float x) {
        if (x >  1.0f) return  1.0f;
        if (x < -1.0f) return -1.0f;
        return x;
    }
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; recalcFixed(); }
    void recalcFixed() { cHP = onePoleCoef(35.f, fs); }   // input coupling HP
    void setParams(float gain, float toneP, float filterP) {
        // RAT gain: high enough to clip aggressively even on a low-level live
        // bass DI (the earlier 2..32 was too clean on quiet inputs — "barely
        // distorts"), but well below the old 500× that made loud hiss.
        // ~5 .. 85; gain 0.8 → ~69.
        drive = 12.0f + gain * 90.0f;
        cPre  = onePoleCoef(800.0f  * std::pow(10.0f, toneP),  fs);  // 800 .. 8000 Hz pre-clip
        cPost = onePoleCoef(700.0f  * std::pow(2.0f, filterP * 3.1f), fs); // 700 .. ~6000 Hz Filter
    }
    inline float process(float x) {
        zHP += cHP * (x - zHP);
        float s = drive * (x - zHP);
        zPre += cPre * (s - zPre); s = zPre;   // pre-clip brightness (Tone)
        s = clip(s);                           // hard silicon clipping
        zPost += cPost * (s - zPost); s = zPost; // RAT Filter low-pass
        return s * 0.26f;                      // output level — clip preserved, level-matched to bypass (was 0.85, far too loud)
    }
};

class BassDistortionPlugin : public Plugin {
    Rat L, R;
    RBAutoMakeup makeupL, makeupR;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kGain], fParams[kTone], fParams[kFilter]);
        R.setParams(fParams[kGain], fParams[kTone], fParams[kFilter]);
    }
public:
    BassDistortionPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBassDistortionDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr);
        makeupL.setSampleRate(sr); makeupR.setSampleRate(sr); recalc();
    }
protected:
    const char* getLabel()       const override { return "BassDistortion"; }
    const char* getDescription() const override { return "Pro Co RAT distortion"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'D', 's'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassDistortionNames[i]; p.symbol = kBassDistortionSymbols[i];
        p.ranges.min = kBassDistortionMin[i]; p.ranges.max = kBassDistortionMax[i]; p.ranges.def = kBassDistortionDef[i];
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassDistortionPlugin)
};

Plugin* createPlugin() { return new BassDistortionPlugin(); }

END_NAMESPACE_DISTRHO
