/*
 * BassSubOctave — Boss OC-2 analog octaver model for Bass_Pedal_BassSubOctave.
 *
 * Faithful to the OC-2's analog frequency-divider topology:
 *   1. Track-band low-pass (~350 Hz) isolates the fundamental so zero crossings
 *      are clean (a bass note's harmonics would otherwise mis-trigger).
 *   2. A hysteresis comparator squares that tracking signal; the hysteresis
 *      scales with the input envelope to reject jitter at low level.
 *   3. A toggle flip-flop divides the square's frequency by two on every rising
 *      edge -> a square wave one octave below the played note.
 *   4. That divided square is multiplied by the input envelope, so the sub
 *      tracks playing dynamics (exactly what the OC-2 does with its VCA).
 *   5. The Tone knob low-passes the sub (square -> rounded, deep sub), then the
 *      Mix knob blends it against the dry signal.
 */
#include "DistrhoPlugin.hpp"
#include "BassSubOctaveParams.h"
#include <cmath>
#include "../_shared/automakeup.hpp"

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

class Octaver {
    float fs = 48000.f;
    // tracking low-pass (2-pole) on the input
    float tr1 = 0.f, tr2 = 0.f, cTrack = 0.f;
    // envelope follower
    float env = 0.f, atk = 0.f, rel = 0.f;
    // comparator + flip-flop
    bool  high = false, flop = false;
    float sub = -1.f;            // ±1 square (one octave down)
    // tone low-pass (2-pole) on the sub
    float to1 = 0.f, to2 = 0.f, cTone = 0.f;
    float mix = 0.5f;

    static inline float msCoef(float ms, float fs) { return std::exp(-1.0f / (0.001f * ms * fs)); }
public:
    void setSampleRate(float s) {
        fs = (s > 0.f) ? s : 48000.f;
        cTrack = onePoleCoef(350.f, fs);
        atk = msCoef(4.f, fs); rel = msCoef(90.f, fs);
    }
    void reset() { tr1 = tr2 = env = to1 = to2 = 0.f; high = flop = false; sub = -1.f; }
    void setParams(float mixP, float tone) {
        mix   = mixP;
        cTone = onePoleCoef(250.f * std::pow(10.0f, tone), fs);  // 250 .. 2500 Hz
    }
    inline float process(float x) {
        // envelope of the dry input
        const float a = std::fabs(x);
        const float c = (a > env) ? atk : rel;
        env = c * env + (1.0f - c) * a;

        // tracking band-limit
        tr1 += cTrack * (x  - tr1);
        tr2 += cTrack * (tr1 - tr2);
        const float tr = tr2;

        // hysteresis comparator — threshold scales with level, with a floor so
        // silence doesn't toggle on noise
        const float thr = 0.08f * env + 0.0006f;
        if (high) { if (tr < -thr) { high = false; } }
        else      { if (tr >  thr) { high = true; flop = !flop; sub = flop ? 1.f : -1.f; } }

        // divided square, amplitude-tracked by the envelope
        const float subRaw = sub * env;

        // Tone low-pass on the sub (2-pole)
        to1 += cTone * (subRaw - to1);
        to2 += cTone * (to1    - to2);

        return x * (1.0f - mix) + to2 * mix * 1.8f;
    }
};

class BassSubOctavePlugin : public Plugin {
    Octaver L, R;
    RBAutoMakeup makeupL, makeupR;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kMix], fParams[kTone]);
        R.setParams(fParams[kMix], fParams[kTone]);
    }
public:
    BassSubOctavePlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBassSubOctaveDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); makeupL.setSampleRate(sr); makeupR.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "BassSubOctave"; }
    const char* getDescription() const override { return "Boss OC-2 analog octaver"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'S', 'o'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassSubOctaveNames[i]; p.symbol = kBassSubOctaveSymbols[i];
        p.ranges.min = kBassSubOctaveMin[i]; p.ranges.max = kBassSubOctaveMax[i]; p.ranges.def = kBassSubOctaveDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); makeupL.snap(); makeupR.snap(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); makeupL.setSampleRate((float)r); makeupR.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = makeupL.process(iL[i], L.process(iL[i])); oR[i] = makeupR.process(iR[i], R.process(iR[i])); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassSubOctavePlugin)
};

Plugin* createPlugin() { return new BassSubOctavePlugin(); }

END_NAMESPACE_DISTRHO
