/*
 * BassOverdrive — Darkglass Microtubes B3K model for Bass_Pedal_BassOverdrive.
 *
 * B3K = a CLEAN path blended with a DISTORTION path (the "always tight low end"
 * trick of modern bass ODs). The distortion is CMOS-inverter clipping with
 * asymmetric diodes (1N4148 + 1N5817 Schottky → even-harmonic grit). Modeled:
 *   clean path: full dry (keeps the lows)
 *   dist path : Grunt high-pass (how much low end enters the clipper) → Drive
 *               gain → asymmetric CMOS soft clip → Attack high-shelf (treble of
 *               the distortion) → level
 *   Blend mixes clean + dist.
 * Rocksmith knobs: Blend, Gain (=Drive), Filter (=Grunt), Tone (=Attack).
 */
#include "DistrhoPlugin.hpp"
#include "BassOverdriveParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

class B3K {
    float fs = 48000.f;
    float zGrunt = 0.f, zPost = 0.f, zShelf = 0.f, zOutDC = 0.f;
    float cGrunt, cPost, cShelf, cOutDC;
    // derived params
    float drive = 30.f, blend = 0.6f, shelfGain = 1.0f;

    // asymmetric CMOS soft clip: bias adds even harmonics; tanh saturates to the
    // rails (CMOS clips fairly hard at high drive).
    static inline float cmos(float x) { return std::tanh(1.1f * x + 0.18f); }
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; recalcFixed(); }
    void recalcFixed() {
        cPost  = onePoleCoef(5500.f, fs);   // tame fizz after clipping
        cShelf = onePoleCoef(1500.f, fs);   // Attack high-shelf split point
        cOutDC = onePoleCoef(18.f,   fs);   // DC blocker (the clip bias)
    }
    void setParams(float blendP, float gain, float filterP, float toneP) {
        blend = blendP;
        drive = std::pow(10.0f, 0.4f + gain * 1.8f);             // ~2.5 .. 158
        // Grunt: more Filter → lower HP corner → more low end into the clipper.
        const float gruntHz = 40.0f + (1.0f - filterP) * 300.0f; // 340 (tight) .. 40 (grunty)
        cGrunt = onePoleCoef(gruntHz, fs);
        // Attack: more Tone → brighter distortion (high-shelf boost).
        shelfGain = std::pow(10.0f, ((toneP - 0.4f) * 14.0f) / 20.0f); // ~ -5.6 .. +8.4 dB
    }
    inline float process(float x) {
        const float clean = x;
        // dist path — Grunt high-pass into the clipper
        zGrunt += cGrunt * (x - zGrunt);
        const float hp = x - zGrunt;
        float d = cmos(drive * hp);
        // tame post-clip fizz
        zPost += cPost * (d - zPost); d = zPost;
        // Attack: high-shelf (low part flat, high part scaled by shelfGain)
        zShelf += cShelf * (d - zShelf);
        const float low = zShelf, high = d - zShelf;
        d = low + high * shelfGain;
        // DC blocker (removes the clip bias)
        zOutDC += cOutDC * (d - zOutDC);
        d = d - zOutDC;
        // blend clean + dist (×0.8 dist to keep peaks sane), then a final
        // output trim so the pedal sits at bypass level (was ~3.7× too loud)
        return (clean * (1.0f - blend) + d * blend * 0.8f) * 0.40f;
    }
};

class BassOverdrivePlugin : public Plugin {
    B3K L, R;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kBlend], fParams[kGain], fParams[kFilter], fParams[kTone]);
        R.setParams(fParams[kBlend], fParams[kGain], fParams[kFilter], fParams[kTone]);
    }
public:
    BassOverdrivePlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBassOverdriveDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); recalc();
    }
protected:
    const char* getLabel()       const override { return "BassOverdrive"; }
    const char* getDescription() const override { return "Darkglass B3K CMOS bass overdrive"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'O', 'd'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassOverdriveNames[i]; p.symbol = kBassOverdriveSymbols[i];
        p.ranges.min = kBassOverdriveMin[i]; p.ranges.max = kBassOverdriveMax[i]; p.ranges.def = kBassOverdriveDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassOverdrivePlugin)
};

Plugin* createPlugin() { return new BassOverdrivePlugin(); }

END_NAMESPACE_DISTRHO
