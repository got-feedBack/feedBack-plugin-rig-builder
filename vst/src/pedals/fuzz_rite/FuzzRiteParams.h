#ifndef FUZZ_RITE_PARAMS_H
#define FUZZ_RITE_PARAMS_H

/*
 * FUZZ RITE — Mosrite FuzzRite (mid-'60s), silicon version, from the FuzzDog
 * V4 build doc (pedals/fuzzrite/FuzzRiteV3.pdf). Parody brand (the in-app face
 * reads "Mosright", never "Mosrite").
 *
 * Two grounded-emitter NPN silicon gain stages (BC337-16, ~150 hFE) — no
 * emitter degeneration, so they slam into hard transistor clipping - with tiny
 * 2n2 interstage caps that thin the signal into the pedal's nasal, buzzy 60s
 * voice. DEPTH reads between Q1's C2-coupled collector signal and the node
 * loaded by C3, R8 and Q2. Q2's collector returns through C4 to Q1's collector;
 * that AC feedback and the loaded pot node create the spit and honk.
 *
 * The real panel is just two pots: DEPTH (the fuzz-blend / character) and VOL.
 *
 * EXTRA gear (not mapped to any RS song).
 */
enum FuzzRiteParamId
{
    kDepth = 0,   // DEPTH (500KB) — the signature fuzz blend / cancellation sweep
    kVolume,      // VOL (500KA) — output level
    kParamCount
};

static const char* const kFuzzRiteNames[kParamCount]   = { "Depth", "Volume" };
static const char* const kFuzzRiteSymbols[kParamCount] = { "depth", "volume" };

static const float kFuzzRiteMin[kParamCount] = { 0.0f, 0.0f };
static const float kFuzzRiteMax[kParamCount] = { 1.0f, 1.0f };
// Default: DEPTH ~2 o'clock (the classic spitty 60s blend), VOL unity-ish.
static const float kFuzzRiteDef[kParamCount] = { 0.62f, 0.70f };

#endif // FUZZ_RITE_PARAMS_H
