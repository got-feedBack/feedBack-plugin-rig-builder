#ifndef DUAL_RECT_CORE_H
#define DUAL_RECT_CORE_H

/*
 * DualRectCore - Mesa/Boogie 3-Channel Dual Rectifier Solo Head component model.
 *
 * White-box audio model (no SPICE), plain C++ so it can be unit-tested offline.
 * Reference: amps/Dual Rectifier (Cali_100)/Boogie_3ch_dual_rectifier.pdf
 *
 * Topology (block diagram + preamp sheets):
 *   INPUT -> V1a (shared) -> channel split:
 *     CH1 GREEN : V1a -> clean tone stack (Clean/Pushed) -> 1 recovery -> Master
 *     CH2 ORANGE: cascade V2a/V2b/V3a/V3b -> Recto tone stack (Raw/Vtg/Modern) -> Master
 *     CH3 RED   : same cascade, hotter -> Recto stack -> Master  (the metal voice)
 *   -> Output -> V5 PI -> 4x 6L6 push-pull -> O.T. -> 4x12, with a 5U4 tube or
 *   silicon-diode rectifier (Bold = tight / Spongy = saggy) and a presence
 *   feedback loop.
 *
 * Only ONE channel is live at a time (the Recto mutes when switching), so the
 * model configures a single signal chain from the ACTIVE channel's knobs + mode
 * in updateComponentValues(); process() just runs that chain. the game drives
 * the Red channel (Modern, Bold) — its 5 knobs map 1:1 to Red Gain/Treble/Mid/
 * Bass/Presence.
 */

#include "DualRectParams.h"
#include <cmath>

namespace dualrect {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float clampFreq(float hz, float sr) { const float ny = sr * 0.45f; return std::fmax(10.0f, std::fmin(hz, ny)); }
static inline float smoothstep(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }
static inline float smoothstepRange(float e0, float e1, float x) { return smoothstep((x - e0) / (e1 - e0)); }
static inline float softClip(float x) { return std::tanh(x); }
static inline float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }

// smooth asymmetric 12AX7 triode (no zero-crossing kink)
static inline float triode12AX7(float x, float bias) {
    const float g = x + bias;
    const float warped = 1.55f * g + 0.34f * g * std::fabs(g);
    const float idle   = 1.55f * bias + 0.34f * bias * std::fabs(bias);
    return std::tanh(warped) - std::tanh(idle);
}
// 6L6 push-pull pair (a little stiffer / more headroom than 6V6/EL84)
static inline float sixL6Pair(float x, float bias) {
    const float p = std::tanh(1.22f * (x + bias) + 0.04f * x * x);
    const float n = std::tanh(1.22f * (-x + bias) + 0.04f * x * x);
    const float idle = std::tanh(1.22f * bias);
    return 0.5f * ((p - idle) - (n - idle));
}

class RcHighPass {
    float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
public:
    void reset() { x1 = y1 = 0.0f; }
    void setHz(float sr, float hz) { hz = clampFreq(hz, sr); const float tau = 1.0f / (2.0f * kPi * hz), dt = 1.0f / std::fmax(sr, 1000.0f); a = tau / (tau + dt); }
    float process(float x) { const float y = a * (y1 + x - x1); x1 = x; y1 = y; return y; }
};
class RcLowPass {
    float a = 1.0f, z = 0.0f;
public:
    void reset() { z = 0.0f; }
    void setHz(float sr, float hz) { hz = clampFreq(hz, sr); const float tau = 1.0f / (2.0f * kPi * hz), dt = 1.0f / std::fmax(sr, 1000.0f); a = dt / (tau + dt); }
    float process(float x) { z += a * (x - z); return z; }
};

class Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2) {
        if (std::fabs(na0) < 1.0e-12f) na0 = 1.0f; const float k = 1.0f / na0;
        b0 = nb0 * k; b1 = nb1 * k; b2 = nb2 * k; a1 = na1 * k; a2 = na2 * k;
    }
public:
    void reset() { z1 = z2 = 0.0f; }
    float process(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void setLowPass(float sr, float hz, float q) { hz = clampFreq(hz, sr); const float w = 2 * kPi * hz / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 - c) * .5f, 1 - c, (1 - c) * .5f, 1 + al, -2 * c, 1 - al); }
    void setHighPass(float sr, float hz, float q) { hz = clampFreq(hz, sr); const float w = 2 * kPi * hz / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 + c) * .5f, -(1 + c), (1 + c) * .5f, 1 + al, -2 * c, 1 - al); }
    void setPeaking(float sr, float hz, float q, float dB) { hz = clampFreq(hz, sr); const float A = std::pow(10.f, dB / 40), w = 2 * kPi * hz / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
    void setLowShelf(float sr, float hz, float slope, float dB) { hz = clampFreq(hz, sr); const float A = std::pow(10.f, dB / 40), w = 2 * kPi * hz / sr, c = std::cos(w), s = std::sin(w), rA = std::sqrt(A);
        const float al = s * .5f * std::sqrt((A + 1 / A) * (1 / slope - 1) + 2);
        set(A * ((A + 1) - (A - 1) * c + 2 * rA * al), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - 2 * rA * al),
            (A + 1) + (A - 1) * c + 2 * rA * al, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - 2 * rA * al); }
    void setHighShelf(float sr, float hz, float slope, float dB) { hz = clampFreq(hz, sr); const float A = std::pow(10.f, dB / 40), w = 2 * kPi * hz / sr, c = std::cos(w), s = std::sin(w), rA = std::sqrt(A);
        const float al = s * .5f * std::sqrt((A + 1 / A) * (1 / slope - 1) + 2);
        set(A * ((A + 1) + (A - 1) * c + 2 * rA * al), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - 2 * rA * al),
            (A + 1) - (A - 1) * c + 2 * rA * al, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - 2 * rA * al); }
};

class DcBlock {
    float x1 = 0.0f, y1 = 0.0f;
public:
    void reset() { x1 = y1 = 0.0f; }
    float process(float x) { const float y = x - x1 + 0.995f * y1; x1 = x; y1 = y; return y; }
};

// 5U4 tube vs silicon-diode rectifier supply: Bold (silicon) = tight, little
// sag; Spongy (tube) = deeper, slower bloom/compression.
class RectoSupply {
    float sr = 48000.0f, sag = 0.0f, atk = 0.0f, rel = 0.0f;
public:
    void reset() { sag = 0.0f; }
    void setSampleRate(float s) { sr = s > 1000.0f ? s : 48000.0f; atk = 1.0f - std::exp(-1.0f / (0.006f * sr)); rel = 1.0f - std::exp(-1.0f / (0.130f * sr)); }
    // rectifier: 0 = Spongy (tube, saggy), 1 = Bold (silicon, tight)
    float process(float env, float drive, float rectifier) {
        sag += (env - sag) * (env > sag ? atk : rel);
        const float depth = (0.10f + 0.55f * (1.0f - rectifier)) * (0.5f + 0.7f * drive);
        return 1.0f / (1.0f + sag * depth);
    }
};

class DualRectCore {
    float sampleRate = 48000.0f;
    float p[kParamCount];

    RcLowPass inputGrid;
    RcHighPass tighten;          // pre-cascade low cut (mode/channel dependent)
    RcLowPass interLp1, interLp2;
    Biquad bassShelf, midScoop, trebleShelf;   // active channel tone stack
    Biquad presence;             // power-amp presence (feedback) high shelf
    Biquad spkHp, spkBody, spkBite, spkFizz, spkLp;  // 4x12 V30-ish
    DcBlock dcBlock;
    RectoSupply supply;

    // derived from the ACTIVE channel (set in updateComponentValues):
    float aDrv1 = 1, aDrv2 = 1, aDrv3 = 1, aRecov = 1;  // stage drives
    float aPower = 1, aMaster = 1, aClean = 0, aRect = 1, aMakeup = 1, aTone = 1;

    void updateComponentValues() {
        // pick the active channel's six knobs + mode
        const float ch = p[kChannel];
        int base; float gain, tre, mid, bass, pres, mast, mode, cleanCh;
        if (ch < 0.25f) { base = kC1Gain; cleanCh = 1.0f; }
        else if (ch < 0.75f) { base = kC2Gain; cleanCh = 0.0f; }
        else { base = kC3Gain; cleanCh = 0.0f; }
        gain = p[base]; tre = p[base+1]; mid = p[base+2]; bass = p[base+3]; pres = p[base+4]; mast = p[base+5]; mode = p[base+6];
        aClean = cleanCh;
        aMaster = mast; aRect = p[kRectifier];

        // channel gain structure: Green clean (low), Orange hot, Red hottest
        const float chHot = (ch < 0.25f) ? 0.0f : (ch < 0.75f ? 0.65f : 1.0f);
        const float g = smoothstep(gain);
        const float hot = smoothstepRange(0.45f, 1.0f, gain);

        // mode (Orange/Red): Raw(0)/Vintage(0.5)/Modern(1). Modern = tighter +
        // more gain + deeper scoop + more presence. Clean ch: mode = Clean/Pushed.
        const float modern = (cleanCh > 0.5f) ? 0.0f : smoothstepRange(0.5f, 1.0f, mode);
        const float vint   = (cleanCh > 0.5f) ? 0.0f : (1.0f - std::fabs(mode - 0.5f) * 2.0f);
        const float pushed = (cleanCh > 0.5f) ? mode : 0.0f;   // clean Pushed

        inputGrid.setHz(sampleRate, 60.0f);

        // pre-cascade tightening: Modern cuts more lows into the stages (tight),
        // Raw/Vintage looser. Clean channel barely tightens.
        const float tightHz = 70.0f + 160.0f * chHot * (0.4f + 0.6f * modern) + 50.0f * g;
        tighten.setHz(sampleRate, tightHz);
        interLp1.setHz(sampleRate, 12000.0f - 3500.0f * chHot + 1500.0f * tre);
        interLp2.setHz(sampleRate, 11000.0f - 3000.0f * chHot + 1500.0f * tre);

        // --- tone stack ---
        if (cleanCh > 0.5f) {
            // Green clean: Fender-ish — gentle, NOT scooped
            bassShelf.setLowShelf(sampleRate, 110.0f, 0.72f, eqDb(bass, 10.0f) + 1.5f);
            midScoop.setPeaking(sampleRate, 500.0f + 250.0f * mid, 0.70f, -2.0f + 7.0f * mid);
            trebleShelf.setHighShelf(sampleRate, 2400.0f + 900.0f * tre, 0.70f, eqDb(tre, 10.0f) + 1.0f);
            aTone = 1.0f;
        } else {
            // Recto tone stack: BIG bass, deep mid scoop (deeper in Modern), bright
            bassShelf.setLowShelf(sampleRate, 120.0f, 0.70f, eqDb(bass, 12.0f) + 2.5f);
            const float scoopHz = 560.0f + 220.0f * mid;
            const float scoopDb = -(5.0f + 6.0f * modern) + (11.0f + 3.0f * modern) * mid;  // Modern scoops harder at low Mid
            midScoop.setPeaking(sampleRate, scoopHz, 0.70f, scoopDb);
            trebleShelf.setHighShelf(sampleRate, 2200.0f + 1100.0f * tre, 0.62f, -4.0f + 16.0f * tre + 2.0f * modern);
            aTone = 1.0f;
        }

        // presence (power-amp feedback high shelf)
        presence.setHighShelf(sampleRate, 2600.0f, 0.80f, -2.0f + 9.0f * pres - 1.5f * modern);

        // --- 4x12 (V30-ish): tight modern voicing ---
        spkHp.setHighPass(sampleRate, 150.0f - 40.0f * modern + 70.0f * cleanCh, 0.80f);
        // body/low-mid: scoop the mud. Clean channel sits muddier (low drive, no
        // cascade) so it needs a broader, higher cut to lift its centroid.
        const float bodyHz = 250.0f + 350.0f * cleanCh;
        const float bodyQ  = 0.70f - 0.24f * cleanCh;
        spkBody.setPeaking(sampleRate, bodyHz, bodyQ, -4.5f - 8.5f * cleanCh + 3.0f * modern + 1.6f * bass - 0.6f * hot);
        spkBite.setPeaking(sampleRate, 2800.0f + 500.0f * tre, 0.55f, 9.0f + 4.0f * cleanCh + 2.2f * tre + 1.2f * pres);
        spkFizz.setHighShelf(sampleRate, 4400.0f, 0.70f, 20.0f + 2.0f * tre + 2.0f * pres - 16.0f * modern);
        spkLp.setLowPass(sampleRate, 15000.0f + 2000.0f * tre + 1000.0f * pres - 9500.0f * modern, 0.62f);

        // --- stage drives ---
        // base gain rises with channel hotness, the Gain knob, and Modern; Raw/
        // Vintage trim it. Clean channel stays low (Pushed nudges it).
        const float drive = (0.7f + 1.4f * chHot) * (0.5f + 1.4f * g + 0.9f * hot)
                          * (1.0f + 0.35f * modern + 0.30f * pushed) * (0.85f + 0.15f * (1.0f - vint));
        aDrv1 = 0.9f + 0.6f * g;
        aDrv2 = 0.6f + drive * (cleanCh > 0.5f ? 0.7f : 1.3f);
        aDrv3 = 0.5f + drive * (cleanCh > 0.5f ? 0.0f : 1.1f);   // 3rd stage only really bites on Orange/Red
        aRecov = 0.8f + 0.7f * g;
        aPower = 1.0f + 1.0f * g + 1.6f * hot + 0.4f * chHot;

        // output makeup: hold ~constant loudness at the BOX DC30 reference across
        // gain + channel, with a tone-energy term so big EQ moves don't shift level.
        const float toneEnergy = 1.0f
            + 0.013f * std::fabs((bass - 0.5f) * 20.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 22.0f)
            + 0.013f * std::fabs((tre - 0.5f) * 20.0f);
        // The Recto (Bold/silicon) barely sags, so high gain stays loud (it does
        // NOT collapse like the AC30/tweed). Makeup is therefore nearly flat —
        // only a gentle rise to offset the slight saturation compression.
        const float makeup = (cleanCh > 0.5f)
            ? (1.05f + 0.55f * g)                       // clean channel is quieter
            : (0.66f + 0.16f * (1.0f - g));             // hi-gain stays loud -> trim
        aMakeup = makeup / toneEnergy;
    }

public:
    void reset() {
        inputGrid.reset(); tighten.reset(); interLp1.reset(); interLp2.reset();
        bassShelf.reset(); midScoop.reset(); trebleShelf.reset(); presence.reset();
        spkHp.reset(); spkBody.reset(); spkBite.reset(); spkFizz.reset(); spkLp.reset();
        dcBlock.reset(); supply.reset();
        updateComponentValues();
    }
    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; supply.setSampleRate(sampleRate); reset(); }
    void setParam(int idx, float v) { if (idx >= 0 && idx < kParamCount) { p[idx] = clamp01(v); updateComponentValues(); } }
    void initDefaults() { for (int i = 0; i < kParamCount; ++i) p[i] = kDualRectDef[i]; }

    float process(float in) {
        float x = inputGrid.process(in);
        // V1a shared input gain stage
        x = triode12AX7(x * aDrv1, -0.018f);
        // pre-cascade tighten
        x = tighten.process(x);
        // cascade
        float y = triode12AX7(x * aDrv2, -0.022f);
        y = interLp1.process(y);
        if (aClean < 0.5f) {            // Orange/Red: extra cascaded stage
            y = triode12AX7(y * aDrv3, 0.020f);
            y = interLp2.process(y);
        }
        // tone stack
        y = bassShelf.process(y);
        y = midScoop.process(y);
        y = trebleShelf.process(y);
        // recovery + master + output
        y = triode12AX7(y * aRecov, -0.012f);
        y *= (0.25f + 1.1f * aMaster) * (0.4f + 1.0f * p[kOutput]);
        // 6L6 power amp + rectifier sag
        const float env = std::fabs(y);
        const float scale = supply.process(env, aPower, aRect);
        y = sixL6Pair(y * aPower * scale, 0.03f);
        y = (0.9f * y + 0.1f * softClip(y * 1.4f)) * scale;
        y = presence.process(y);
        y = dcBlock.process(y);
        // 4x12
        y = spkHp.process(y);
        y = spkBody.process(y);
        y = spkBite.process(y);
        y = spkFizz.process(y);
        y = spkLp.process(y);
        return softClip(y * aMakeup) * 0.98f;
    }
};

} // namespace dualrect

#endif // DUAL_RECT_CORE_H
