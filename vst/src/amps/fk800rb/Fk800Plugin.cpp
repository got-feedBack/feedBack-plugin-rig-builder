/*
 * Freddy Krueger 800BR — Gallien-Krueger 800RB bass-head model.
 *
 * DSP modeled from the GK 800RB service manual (preamp sheet 406-0045 "Bob
 * Gallien 800 RB Preamp", + the Operators Manual block diagram & specs). The
 * 800RB is an all-op-amp (LF353) preamp feeding a bi-amp power section
 * (300W low / 100W high). We recreate each block in order:
 *
 *   1. Input  : 1/4" in with a -10 dB pad (drops preamp gain 10 dB, +headroom).
 *   2. Volume : preamp drive into a gentle op-amp soft-clip — the GK "growl"
 *               that the input/diode stage (D1,D2) produces when pushed.
 *   3. Voicing filters (the 3 square switches):
 *        Lo Cut      — bass roll-off (high-pass) to kill stage rumble.
 *        Mid Contour — notch at ~500 Hz ("mellow round sound").
 *        Hi Boost    — presence/high-shelf boost ("adds edge and definition").
 *   4. 4-band Active EQ, ±15 dB, flat at 0.5:
 *        Bass   low shelf  60 Hz | Lo-Mid peak 250 Hz
 *        Hi-Mid peak     1.0 kHz | Treble high shelf 4 kHz
 *   5. Boost : footswitchable preset gain, up to +15 dB, with a touch more
 *              growl when driven hard.
 *   6. Electronic Crossover + Master volumes: a Linkwitz-ish LP/HP split at the
 *        Crossover frequency (100 Hz .. 1.04 kHz). In Bi-Amp mode the low band
 *        is scaled by the 300W master and the high band by the 100W master and
 *        summed (the real head's two power amps feeding one signal here); in
 *        Full-range mode both masters act on the full-range signal.
 */
#include "DistrhoPlugin.hpp"
#include "Fk800Params.h"
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
        const float alpha = sw * 0.5f * 1.4142135f;
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
    void setLowPass(float fc, float Q, float fs) {
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha;
        b0 = ((1 - cw) * 0.5f) / a0;
        b1 = (1 - cw)          / a0;
        b2 = ((1 - cw) * 0.5f) / a0;
        a1 = (-2 * cw)         / a0;
        a2 = (1 - alpha)       / a0;
    }
    void setHighPass(float fc, float Q, float fs) {
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha;
        b0 = ((1 + cw) * 0.5f) / a0;
        b1 = -(1 + cw)         / a0;
        b2 = ((1 + cw) * 0.5f) / a0;
        a1 = (-2 * cw)         / a0;
        a2 = (1 - alpha)       / a0;
    }
    void setBypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; z1 = z2 = 0; }
};

class Gk800Channel {
    float fs = 48000.f;
    Biquad loCut;                          // voicing: bass roll-off
    Biquad contour;                        // voicing: ~500 Hz notch
    Biquad hiBoost;                        // voicing: presence
    Biquad bqBass, bqLoMid, bqHiMid, bqTreble;   // 4-band active EQ
    // 2nd-order Butterworth (cascaded biquads, Q=0.707 then ~0.541) for a
    // 4th-order-ish crossover split that recombines cleanly.
    Biquad xLow1, xLow2, xHigh1, xHigh2;

    float inGain = 1.f, inComp = 1.f;      // preamp drive + make-up
    float boostGain = 1.f;
    bool  boostOn = false;
    float g100 = 1.f, g300 = 1.f;          // master gains
    bool  biamp = false;

    static inline float softclip(float x) { return std::tanh(x); }
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; }
    void reset() {
        loCut.reset(); contour.reset(); hiBoost.reset();
        bqBass.reset(); bqLoMid.reset(); bqHiMid.reset(); bqTreble.reset();
        xLow1.reset(); xLow2.reset(); xHigh1.reset(); xHigh2.reset();
    }

    void setParams(float volume, float treble, float hiMid, float loMid, float bass,
                   float boostLevel, float xover, float master100, float master300,
                   bool pad, bool loCutOn, bool contourOn, bool hiBoostOn,
                   bool boostOnP, bool biampP) {
        // ── input / preamp drive (the GK growl). -10 dB pad cuts gain ~3.16x ──
        const float padScale = pad ? 0.316f : 1.0f;
        inGain = (0.6f + volume * 3.2f) * padScale;   // ~0.6 .. 3.8
        inComp = 1.0f / (0.6f + 0.55f * inGain);      // keep level sane vs drive

        // ── voicing filters ──
        if (loCutOn)  loCut.setHighPass(110.f, 0.707f, fs);          else loCut.setBypass();
        if (contourOn) contour.setPeak(500.f, -11.f, 1.1f, fs);      else contour.setBypass();
        if (hiBoostOn) hiBoost.setHighShelf(2200.f, 6.5f, fs);       else hiBoost.setBypass();

        // ── 4-band active EQ, ±15 dB (0.5 = flat). Frequencies/Q derived from
        //    the preamp R/C (Bob Gallien sheet 60045A), which confirm the manual:
        //      Bass   : R30 12K + C16 .22uF  -> 1/(2pi*R*C) = 60.3 Hz (low shelf)
        //      Lo-Mid : C13/C14 .022uF net   -> ~250 Hz peak
        //      Hi-Mid : C11/C12 .0047uF (same topology) -> 250*(.022/.0047) ~ 1.17 kHz
        //      Treble : high shelf, design 4 kHz
        //    GK's mid bands are broad/gentle, so Q ~ 0.7 (not a narrow notch).
        bqBass.setLowShelf(60.f,      (bass   - 0.5f) * 30.f, fs);
        bqLoMid.setPeak(250.f,        (loMid  - 0.5f) * 30.f, 0.7f, fs);
        bqHiMid.setPeak(1150.f,       (hiMid  - 0.5f) * 30.f, 0.7f, fs);
        bqTreble.setHighShelf(4000.f, (treble - 0.5f) * 30.f, fs);

        // ── boost: preset, footswitchable, up to +15 dB ──
        boostOn   = boostOnP;
        boostGain = std::pow(10.f, (boostLevel * 15.f) / 20.f);   // 1 .. ~5.6

        // ── crossover split + masters ──
        biamp = biampP;
        const float fc = 100.f + 940.f * xover;        // 100 Hz .. 1040 Hz
        xLow1.setLowPass(fc, 0.707f, fs);  xLow2.setLowPass(fc, 0.707f, fs);
        xHigh1.setHighPass(fc, 0.707f, fs); xHigh2.setHighPass(fc, 0.707f, fs);
        g300 = master300 / 0.7f;   // low / 300W amp  (unity ~ 0.7)
        g100 = master100 / 0.7f;   // high / 100W amp
    }

    inline float process(float x) {
        // 1-2. preamp drive + growl (level-preserving soft clip)
        float s = softclip(inGain * x) * inComp;

        // 3. voicing filters
        s = loCut.process(s);
        s = contour.process(s);
        s = hiBoost.process(s);

        // 4. active EQ
        s = bqBass.process(s);
        s = bqLoMid.process(s);
        s = bqHiMid.process(s);
        s = bqTreble.process(s);

        // 5. boost stage (extra growl only when pushed hard)
        if (boostOn) {
            s *= boostGain;
            if (boostGain > 2.0f) s = softclip(s) ;  // gentle clip at high boost
        }

        // 6. crossover + masters
        const float low  = xLow2.process(xLow1.process(s));
        const float high = xHigh2.process(xHigh1.process(s));
        if (biamp)
            return low * g300 + high * g100;          // bi-amp: split & re-sum
        return s * (0.5f * g300 + 0.5f * g100);       // full range: combined master
    }
};

class Fk800Plugin : public Plugin {
    Gk800Channel L, R;
    float fParams[kParamCount];
    void recalc() {
        const bool pad = fParams[kPad] > 0.5f, lc = fParams[kLoCut] > 0.5f;
        const bool ct  = fParams[kContour] > 0.5f, hb = fParams[kHiBoost] > 0.5f;
        const bool bo  = fParams[kBoostOn] > 0.5f, ba = fParams[kBiamp] > 0.5f;
        L.setParams(fParams[kVolume], fParams[kTreble], fParams[kHiMid], fParams[kLoMid], fParams[kBass],
                    fParams[kBoostLevel], fParams[kXover], fParams[kMaster100], fParams[kMaster300],
                    pad, lc, ct, hb, bo, ba);
        R.setParams(fParams[kVolume], fParams[kTreble], fParams[kHiMid], fParams[kLoMid], fParams[kBass],
                    fParams[kBoostLevel], fParams[kXover], fParams[kMaster100], fParams[kMaster300],
                    pad, lc, ct, hb, bo, ba);
    }
public:
    Fk800Plugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kFk800Def[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "FreddyKrueger800BR"; }
    const char* getDescription() const override { return "Gallien-Krueger 800RB bass head model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'F', 'k'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kPad) p.hints |= kParameterIsBoolean;
        p.name = kFk800Names[i]; p.symbol = kFk800Symbols[i];
        p.ranges.min = kFk800Min[i]; p.ranges.max = kFk800Max[i]; p.ranges.def = kFk800Def[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = L.process(iL[i]); oR[i] = R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Fk800Plugin)
};

Plugin* createPlugin() { return new Fk800Plugin(); }

END_NAMESPACE_DISTRHO
