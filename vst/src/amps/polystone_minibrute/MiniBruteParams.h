#ifndef MINIBRUTE_PARAMS_H
#define MINIBRUTE_PARAMS_H

/*
 * POLYSTONE MINIBRUTE = Polytone Mini Brute (CS-100) — the front panel, 1:1,
 * from the local schematic (Polytone_mini_brute.pdf). Parody brand "Polystone"
 * (Polytone -> Polystone); the in-app face must never read "Polytone".
 *
 * A SIMPLE SOLID-STATE jazz combo (the warm clean "jazz box" tone, e.g. Joe
 * Pass): an op-amp (4558) preamp into a Baxandall-ish BASS/TREBLE tone amp, a
 * VOLUME, and a BRITE switch (treble high-shelf boost). NO middle control. The
 * panel also has Hi/Lo inputs. SOLID-STATE: stays clean, dark-ish voicing, only
 * a gentle op-amp soft-limit near full VOLUME (jazz amps stay clean). The DIST/
 * reverb circuits on the full schematic are NOT on this clean voice and are
 * omitted; the power amp (PA378B) is a clean transistor push-pull.
 *
 * Panel (1:1): BASS, TREBLE, VOLUME + a BRITE switch + Hi/Lo inputs.
 *
 * the game mapping: RS Gain -> VOLUME (the only level/drive; mostly clean),
 * RS Bass -> Bass, RS Treble -> Treble (no Mid on this amp).
 */
enum MiniBruteParamId
{
    kVolume = 0, // VOLUME (the only level/drive; mostly clean)  [RS Gain]
    kBass,       // BASS  (Baxandall bass shelf)                 [RS Bass]
    kTreble,     // TREBLE (Baxandall treble shelf)              [RS Treble]
    kBrite,      // BRITE switch (treble high-shelf boost)
    kParamCount
};

static const char* const kMiniBruteNames[kParamCount] = {
    "Volume", "Bass", "Treble", "Brite",
};

static const char* const kMiniBruteSymbols[kParamCount] = {
    "volume", "bass", "treble", "brite",
};

static const float kMiniBruteMin[kParamCount] = { 0, 0, 0, 0 };
static const float kMiniBruteMax[kParamCount] = { 1, 1, 1, 1 };
// Defaults: a warm clean jazz-box voice. VOLUME 0.55, tone flat (0.50), BRITE off.
static const float kMiniBruteDef[kParamCount] = {
    0.55f, 0.50f, 0.50f, 0.00f,
};

#endif // MINIBRUTE_PARAMS_H
