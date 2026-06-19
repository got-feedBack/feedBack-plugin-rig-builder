#ifndef DUAL_RECT_CORE_H
#define DUAL_RECT_CORE_H

/*
 * DualRectCore - Mesa/Boogie 3-Channel Dual Rectifier Solo Head, CIRCUIT-REAL.
 *
 * Rebuilt the same way as BOX DC30 / JCM800 (see REAL_TUBE_AMP_GUIDE.md): real
 * cathode-biased 12AX7 stages (pure Koren plate tables + the physical cathode
 * auto-bias loop) + a real Yeh R/C tone stack + a real 6L6 push-pull power amp.
 * NOT Guitarix GPL code — OUR Koren tables + Yeh model.
 *
 * Topology read off the schematic (amps/Dual Rectifier (Cali_100)/
 * Boogie_3ch_dual_rectifier.pdf, BLOCK DIAGRAM + PREAMP PT1/PT2):
 *   INPUT -> V1a (shared 12AX7) -> channel split:
 *     CH1 GREEN : V1a -> CH1 tone stack -> Gain -> V1b -> Master   (clean, 2 stages)
 *     CH2 ORANGE: V1a -> Gain -> V2a -> V2b -> V3a -> V3b -> CH2 tone stack -> Master
 *     CH3 RED   : same 4-stage cascade, hotter -> CH3 tone stack -> Master   (metal)
 *   -> V5 phase inverter -> 4x 6L6 push-pull -> O.T. -> 4x12.
 *   Rectifier: 2x 5U4 tube (Spongy, saggy) OR silicon (Bold, tight) = power-amp sag.
 *
 * Tone-stack R/C from the schematic:
 *   CH1 (clean): Treble 250k/250pF · Bass 250k/0.1uF · Mid 25k/0.047uF · slope 100k
 *   CH2/CH3 (Recto): Treble 250k/500pF · Bass 1M/0.02uF · Mid 25k/0.02uF · slope 47k
 *
 * Only ONE channel is live at a time (the Recto mutes when switching), so the
 * model configures a single chain from the ACTIVE channel's knobs + mode in
 * updateComponentValues(); process() runs that chain. The game drives the Red
 * channel (Modern, Bold) — its 5 knobs map to Red Gain/Treble/Mid/Bass/Presence.
 */

#include "DualRectParams.h"
#include "../../_shared/tube_stage.hpp"   // real 12AX7 stages + 6L6 PP + Yeh tone stack
#include <cmath>

namespace dualrect {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float clampFreq(float hz, float sr) { const float ny = sr * 0.45f; return std::fmax(10.0f, std::fmin(hz, ny)); }
static inline float smoothstep(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }
static inline float smoothstepRange(float e0, float e1, float x) { return smoothstep((x - e0) / (e1 - e0)); }
static inline float softClip(float x) { return std::tanh(x); }

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

class DualRectCore {
    float sampleRate = 48000.0f;
    float p[kParamCount];

    RcLowPass  inputGrid;
    RcHighPass tighten;                 // pre-cascade low cut (mode/channel dependent)
    RcLowPass  interLp1, interLp2;      // inter-stage Miller rolloff
    rbtube::TubeStage   v1a, v2a, v2b, v3a, v3b, v1b;   // real 12AX7 stages (Koren tables)
    rbtube::ToneStackYeh tone;          // active channel's R/C tone stack (Yeh model)
    rbtube::PowerAmp6L6  power;          // 4x 6L6 push-pull (~100W)
    Biquad presence;                    // power-amp presence (feedback) high shelf
    Biquad spkHp, spkBody, spkBite, spkFizz, spkLp;    // 4x12 V30-ish
    DcBlock dcBlock;

    // derived from the ACTIVE channel (set in updateComponentValues):
    float aClean = 0.0f, inScale = 2.2f, toneMk = 12.0f, aMaster = 1.0f, aMakeup = 1.0f;
    float dGain = 1.0f, dClean = 0.5f, gFloorG = 0.0f, aPush = 0.0f, aGainKnob = 0.5f;

    void setupTubes() {
        // shared input + cascade: standard 12AX7 (250k grid-leak tab, 250V plate, /40).
        // fck climbs along the cascade (later stages have smaller cathode bypass = less
        // low-end gain -> the tight Recto low end). All cathode-biased, self-bias solved.
        v1a.set(sampleRate, 1, 250.0f, 40.0f, 25.0f, 1500.0f);   // shared input
        v2a.set(sampleRate, 1, 250.0f, 40.0f, 30.0f, 1500.0f);   // cascade 1
        v2b.set(sampleRate, 1, 250.0f, 40.0f, 45.0f, 1500.0f);   // cascade 2
        v3a.set(sampleRate, 1, 250.0f, 40.0f, 60.0f, 1500.0f);   // cascade 3
        v3b.set(sampleRate, 1, 250.0f, 40.0f, 80.0f, 1500.0f);   // cascade 4
        v1b.set(sampleRate, 1, 250.0f, 40.0f, 40.0f, 1500.0f);   // clean-channel recovery
    }

    void updateComponentValues() {
        // pick the active channel's six knobs + mode
        const float ch = p[kChannel];
        int base; float cleanCh;
        if (ch < 0.25f) { base = kC1Gain; cleanCh = 1.0f; }
        else if (ch < 0.75f) { base = kC2Gain; cleanCh = 0.0f; }
        else { base = kC3Gain; cleanCh = 0.0f; }
        const float gain = p[base], tre = p[base+1], mid = p[base+2], bass = p[base+3], pres = p[base+4], mast = p[base+5], mode = p[base+6];
        aClean = cleanCh; aMaster = mast; aGainKnob = gain;

        // channel gain structure: Green clean (low), Orange hot, Red hottest
        const float chHot = (ch < 0.25f) ? 0.0f : (ch < 0.75f ? 0.70f : 1.0f);
        const float g = smoothstep(gain);
        const float hot = smoothstepRange(0.45f, 1.0f, gain);
        // mode (Orange/Red): Raw(0)/Vintage(0.5)/Modern(1). Modern = tighter + more
        // gain + deeper scoop + more presence. Clean ch: mode = Clean/Pushed.
        const float modern = (cleanCh > 0.5f) ? 0.0f : smoothstepRange(0.5f, 1.0f, mode);
        const float vint   = (cleanCh > 0.5f) ? 0.0f : (1.0f - std::fabs(mode - 0.5f) * 2.0f);
        const float pushed = (cleanCh > 0.5f) ? mode : 0.0f;   // clean Pushed

        inputGrid.setHz(sampleRate, 60.0f);
        const float tightHz = 70.0f + 160.0f * chHot * (0.4f + 0.6f * modern) + 50.0f * g;
        tighten.setHz(sampleRate, tightHz);
        interLp1.setHz(sampleRate, 12000.0f - 3500.0f * chHot + 1500.0f * tre);
        interLp2.setHz(sampleRate, 11000.0f - 3000.0f * chHot + 1500.0f * tre);

        // --- tone stack (CIRCUIT-REAL, Yeh model, real schematic R/C) ---
        if (cleanCh > 0.5f) {
            // CH1 clean (Mesa/Mark-style): Treble 250k/250pF, Bass 250k/0.1uF, Mid 25k/0.047uF, slope 100k
            tone.setComponents(250e3, 250e3, 25e3, 100e3, 250e-12, 100e-9, 47e-9);
            toneMk = 11.0f;
        } else {
            // CH2/CH3 Recto: Treble 250k/500pF, Bass 1M/0.02uF, Mid 25k/0.02uF, slope 47k
            tone.setComponents(250e3, 1.0e6, 25e3, 47e3, 500e-12, 20e-9, 20e-9);
            toneMk = 13.0f;
        }
        tone.update(sampleRate, tre, mid, bass);

        // presence (power-amp feedback high shelf)
        presence.setHighShelf(sampleRate, 2600.0f, 0.80f, -2.0f + 9.0f * pres - 1.5f * modern);

        // --- 4x12 (V30-ish). A real cab ATTENUATES the top (no +20 dB fizz shelf —
        //     that inflates crest without distorting); bite/fizz kept modest. ---
        spkHp.setHighPass(sampleRate, 150.0f - 40.0f * modern + 70.0f * cleanCh, 0.80f);
        const float bodyHz = 250.0f + 350.0f * cleanCh;
        const float bodyQ  = 0.70f - 0.24f * cleanCh;
        spkBody.setPeaking(sampleRate, bodyHz, bodyQ, -4.5f - 8.5f * cleanCh + 3.0f * modern + 5.0f * pushed + 1.6f * bass - 0.6f * hot);
        spkBite.setPeaking(sampleRate, 2800.0f + 500.0f * tre, 0.55f, 3.5f + 2.0f * cleanCh + 1.5f * tre + 1.0f * pres);
        spkFizz.setHighShelf(sampleRate, 4400.0f, 0.70f, -3.0f + 2.0f * tre + 2.0f * pres - 6.0f * modern);
        spkLp.setLowPass(sampleRate, 11000.0f + 1500.0f * tre + 1000.0f * pres - 4000.0f * modern, 0.62f);

        // --- drives (grid volts) ---
        // Green keeps V1a nearly clean (low input scale); Orange/Red slam it to feed
        // the 4-stage cascade.
        inScale = (cleanCh > 0.5f) ? 1.30f : 2.20f;
        // hi-gain cascade factor: rises with channel hotness, the Gain knob, and Modern;
        // Raw/Vintage trim it. Low floor so Raw at low gain stays clean; compounds across
        // the 4 cascaded stages.
        dGain = (0.8f + 1.5f * chHot) * (0.28f + 1.40f * g + 0.80f * hot)
              * (0.85f + 0.15f * vint + 0.45f * modern);   // Raw < Vintage < Modern gain ladder
        // clean channel V1b drive: pristine at low gain, slight breakup at gain 10; the
        // Pushed switch cooks V1b hard (crushes at gain 10, stays clean at gain 2).
        dClean = 0.40f + 1.00f * g + 5.0f * pushed * g;
        gFloorG = g;                                            // later-cascade gain-pot scaling
        aPush = pushed;

        // 4x 6L6 (~100W). Rectifier: Bold/silicon(1) = tight (low sag), Spongy/tube(0) =
        // deep sag/bloom -> map straight onto the power-amp sagDepth.
        const float rect = p[kRectifier];
        // Low floor so a clean/low-volume signal leaves the power tubes clean (the green
        // channel at low master must stay pristine); it climbs steeply with channel
        // hotness (Orange/Red push the power tubes hard) + gain.
        const float pdrive = 1.0f + 4.5f * chHot + 1.8f * g + 2.8f * hot;
        power.set(sampleRate, pdrive, -45.0f, 0.12f + 0.50f * (1.0f - rect), 60.0f, 11000.0f);
        power.out = 0.012f;
        power.biasShift = 2.2f;

        // output makeup: hold ~constant loudness across gain + channel, tone-energy term
        // so big EQ moves don't shift level. The Recto (Bold) barely sags so hi-gain
        // stays loud (no AC30/tweed collapse).
        const float toneEnergy = 1.0f
            + 0.013f * std::fabs((bass - 0.5f) * 20.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 22.0f)
            + 0.013f * std::fabs((tre - 0.5f) * 20.0f);
        const float makeup = (cleanCh > 0.5f) ? (0.95f + 0.40f * g) : (0.72f + 0.12f * (1.0f - g));
        aMakeup = makeup / toneEnergy;
    }

public:
    void reset() {
        inputGrid.reset(); tighten.reset(); interLp1.reset(); interLp2.reset();
        v1a.reset(); v2a.reset(); v2b.reset(); v3a.reset(); v3b.reset(); v1b.reset();
        tone.reset(); power.reset(); presence.reset();
        spkHp.reset(); spkBody.reset(); spkBite.reset(); spkFizz.reset(); spkLp.reset();
        dcBlock.reset();
        setupTubes();
        updateComponentValues();
    }
    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }
    void setParam(int idx, float v) { if (idx >= 0 && idx < kParamCount) { p[idx] = clamp01(v); updateComponentValues(); } }
    void initDefaults() { for (int i = 0; i < kParamCount; ++i) p[i] = kDualRectDef[i]; }

    float process(float in) {
        float x = inputGrid.process(in);
        x = v1a.process(x * inScale);                   // V1a shared input stage (real 12AX7)
        x = tighten.process(x);                         // pre-cascade tighten

        float y;
        if (aClean > 0.5f) {
            // CH1 GREEN: tone stack EARLY (V1a -> tone -> Gain -> V1b), stays clean.
            y = tone.process(x) * toneMk;
            y = v1b.process(y * dClean);                // recovery / clean gain
            // Pushed switch adds cascade gain (V2a/V2b) -> heavy crunch at high gain,
            // stays clean at low gain (scaled by the Gain knob).
            if (aPush > 0.01f) {
                y = v2a.process(y * (0.6f + 10.0f * aPush * gFloorG));
                y = v2b.process(y * (0.8f +  6.0f * aPush * gFloorG));
            }
            y = interLp1.process(y);
        } else {
            // CH2/CH3: 4-stage cascade THEN the Recto tone stack (post-distortion).
            // The later-stage drive floors scale with the Gain knob (real 2204-style
            // gain pot sits BEFORE the cascade) so low gain leaves the cascade clean;
            // the floors reach 0.80 at gain 10 (unchanged hi-gain saturation).
            const float gfloor = 0.35f + 0.45f * gFloorG;
            y = v2a.process(x * (0.35f + dGain));
            y = interLp1.process(y);
            y = v2b.process(y * (gfloor + 0.45f * dGain));
            y = v3a.process(y * (gfloor + 0.35f * dGain));
            y = interLp2.process(y);
            y = v3b.process(y * (gfloor + 0.25f * dGain));
            y = tone.process(y) * toneMk;               // Recto tone stack
        }

        // master + output level into the power amp
        y *= (0.25f + 1.1f * aMaster) * (0.4f + 1.0f * p[kOutput]);
        // 4x 6L6 power amp + rectifier sag (real pentode table + OT + sag)
        y = power.process(y);
        y = presence.process(y);
        y = dcBlock.process(y);
        // 4x12
        y = spkHp.process(y);
        y = spkBody.process(y);
        y = spkBite.process(y);
        y = spkFizz.process(y);
        y = spkLp.process(y);
        // loudness flattening vs the active channel's GAIN knob (clean post-output makeup;
        // ~0 dB at gain 0.5). Fit on the Orange channel; flattens the dominant gain axis.
        float gcDb = 7.885f - 23.961f * aGainKnob + 14.432f * aGainKnob * aGainKnob;
        if (gcDb > 12.0f) gcDb = 12.0f; else if (gcDb < -12.0f) gcDb = -12.0f;
        return softClip(y * aMakeup) * 0.98f * std::pow(10.0f, 0.05f * gcDb);
    }
};

} // namespace dualrect

#endif // DUAL_RECT_CORE_H
