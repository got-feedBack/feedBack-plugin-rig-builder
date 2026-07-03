#pragma once
#include <cmath>
// the game "Studio Compressor" (Rack_StudioCompressor) -> dbx 160 model.
// The dbx 160 (this schematic) is a FEED-FORWARD compressor built around a
// dbx 202 true-RMS level detector + a dbx 202 log-domain VCA. Its signature
// is true-RMS detection (smooth, program-dependent) and exponential/log gain
// control, with the "OverEasy"-style soft knee. We model that behaviour:
// RMS detector -> dB-domain soft-knee gain computer -> attack/release
// ballistics -> log-domain (dB) VCA gain.
//
// RS knob names (1:1): Threshold (dB), Ratio, Attack (ms), Release (ms).
// Output is a VST-only make-up trim (RS doesn't send it); an auto make-up
// also lifts the level by part of the gain reduction so it isn't quiet.
// cGR is an OUTPUT param (gain reduction in dB) that feeds the real-time VU/GR
// meter in the UI — it is NOT a knob and is never set by RS/the host.
enum { cThreshold, cRatio, cAttack, cRelease, cOutput, cGR, cNumParams };
// Number of editable KNOBS (excludes the cGR output meter param).
#define SC_KNOB_COUNT 5
// Meter range: gain reduction shown 0..this many dB.
#define SC_GR_METER_MAX 20.0f

static const char* const kCompNames[cNumParams] = {
    "Threshold", "Ratio", "Attack", "Release", "Output", "GR"
};

// Defaults (also used by the UI for double-click reset): −20 dB threshold, 3:1,
// ~20 ms attack, ~120 ms release, +2.5 dB Output (lands a calibrated input ≈−12 dBFS).
static const float kCompDef[cNumParams] = { 0.5f, 0.1818f, 0.1333f, 0.2083f, 0.4028f, 0.0f };

// Param (0..1) -> real unit. Ranges MUST match the apply_vst_state
// 'studiocomp' block so RS real-unit values normalize to the same scale.
static inline float scThresholdDb(float v) { return -40.0f + v * 40.0f; }   // [-40 .. 0] dB
// Ratio: 1:1 .. ∞ (the real 160's Compression knob, ∞ = limiter). The lower
// segment stays LINEAR 1..4 so RS ratios (sent 1..4, normalized via range
// [1,12] → param 0..0.273) reproduce exactly; above that the knob curves up to a
// brick-wall limiter (slope → ~1). RS never sends param > 0.273, so the upper
// curve is reached only manually.
static inline float scRatio(float v) {
    if (v <= 0.2727f) return 1.0f + v * 11.0f;             // 1 .. 4 (RS-exact)
    const float t = (v - 0.2727f) / (1.0f - 0.2727f);      // 0..1 over the upper knob
    const float slope = 0.75f + 0.247f * t;                // slope 0.75 (=4:1) .. 0.997
    return 1.0f / (1.0f - slope);                           // 4 .. ~330 (≈ ∞ / limiter)
}
static inline float scAttackMs(float v)     { return v * 150.0f; }           // [0 .. 150] ms
static inline float scReleaseMs(float v)    { return 20.0f + v * 480.0f; }   // [20 .. 500] ms
static inline float scOutputDb(float v)     { return -12.0f + v * 36.0f; }   // [-12 .. +24] dB

// Knee width (dB). The discrete 160 is hard-knee; keep this NARROW (just rounds
// the corner). The dbx smoothness is the true-RMS detector, not a wide knee —
// the 10 dB OverEasy knee belongs to the later 160X, so it's not modeled here.
static const float SC_KNEE_DB = 3.0f;
// True-RMS detector averaging time (s) — short window for the smooth dbx
// character; attack/release ballistics ride on top.
static const float SC_RMS_TIME = 0.004f;

#define SC_PLUGIN_LABEL "Studio Comp"
