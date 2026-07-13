#ifndef TURBO_DISTORTION_PARAMS_H
#define TURBO_DISTORTION_PARAMS_H

/*
 * TURBO DISTORTION — component-guided Boss DS-2 Turbo Distortion. Parody brand
 * (no "Boss" on the face). Reference: pedals/Boss DS-2/boss-ds2-schematic.webp
 * + hobby-hour DS-2 notes.
 *
 * Real panel: LEVEL, TONE, DIST + the TURBO mode switch (I / II). Mode I is the
 * classic DS-1-family distortion; Mode II (Turbo) engages the extra gain/clip
 * network (Q23/Q15-19/Q17-18 on the schematic) for a hotter, tighter,
 * mid-forward, more compressed "turbo" voice. Silicon clipping throughout
 * (1SS-133 / 1SS-188FM).
 *
 * EXTRA gear (not mapped to any RS song) — panel is the real DS-2.
 */
enum TurboDistortionParamId
{
    kDist = 0,   // DIST — distortion amount (VR1 250K)
    kTone,       // TONE — LO/HI active tone (VR2 100K)
    kLevel,      // LEVEL — output (VR3 50K)
    kTurbo,      // TURBO switch: 0 = Mode I (classic), 1 = Mode II (turbo)
    kParamCount
};

static const char* const kTurboDistortionNames[kParamCount] = {
    "Dist", "Tone", "Level", "Turbo",
};
static const char* const kTurboDistortionSymbols[kParamCount] = {
    "dist", "tone", "level", "turbo",
};
static const float kTurboDistortionMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kTurboDistortionMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
// Defaults: classic mid-gain DS-2 rhythm — Dist past noon, Tone centred,
// Level for unity-ish, Mode I.
static const float kTurboDistortionDef[kParamCount] = {
    0.55f, 0.50f, 0.50f, 0.00f,
};

#endif // TURBO_DISTORTION_PARAMS_H
