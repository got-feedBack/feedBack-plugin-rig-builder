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
enum { cThreshold, cRatio, cAttack, cRelease, cOutput, cNumParams };

static const char* const kCompNames[cNumParams] = {
    "Threshold", "Ratio", "Attack", "Release", "Output"
};

// Param (0..1) -> real unit. Ranges MUST match the apply_vst_state
// 'studiocomp' block so RS real-unit values normalize to the same scale.
static inline float scThresholdDb(float v) { return -40.0f + v * 40.0f; }   // [-40 .. 0] dB
static inline float scRatio(float v)        { return 1.0f  + v * 11.0f; }    // [1 .. 12] :1
static inline float scAttackMs(float v)     { return v * 150.0f; }           // [0 .. 150] ms
static inline float scReleaseMs(float v)    { return 20.0f + v * 480.0f; }   // [20 .. 500] ms
static inline float scOutputDb(float v)     { return -12.0f + v * 36.0f; }   // [-12 .. +24] dB

// dbx OverEasy-style soft knee width (dB).
static const float SC_KNEE_DB = 10.0f;
// True-RMS detector averaging time (s) — short window for the smooth dbx
// character; attack/release ballistics ride on top.
static const float SC_RMS_TIME = 0.004f;

#define SC_PLUGIN_LABEL "Studio Comp"
