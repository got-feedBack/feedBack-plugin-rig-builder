#ifndef M610_PARAMS_H
#define M610_PARAMS_H

/*
 * MULTIVERSAL 610 = Universal Audio 610-A console module (the 1960s tube DI /
 * preamp), for the game's DI_Amp_TubePre ("Amp_Tube Pre"). Parody brand
 * "Multiversal" — the in-app face must never read "Universal Audio" / "UA".
 *
 * Modelled component-by-component from the local schematic:
 *   amps/UA 610 Preamp (TubePre)/610-A UA 610 Preamp Schematic.pdf
 *   (John Hinson's redraw of two 1960s 610 modules, C-10068) +
 *   Universal-Audio-610-Notes.pdf (input UTC O-1, output UTC PA-5946 30K:600,
 *   ~7 mA through the OT primary).
 *
 * Real signal path: Mic/Line in -> T1 UTC O-1 input transformer -> V1 12AX7
 * (both halves cascaded; plates R6 270K / R8 100K, coupling C3/C4 .047uF,
 * grid leaks 1M2, cathodes 4K7 / 1K) -> S1 Lo/Hi Gain divider (R9 39K /
 * R11 68K + C14 .2uF) -> passive stepped shelf EQ: S2 HF (-6/0/+3/+6;
 * C10 100pF, C12 47pF, C9 4n7, R22 220K, R23 100K) and S3 LF (-6/0/+6;
 * C13 4n7, C16 10n, C17 2n, R21/R25 270K, R24 2K2) -> V2 12AY7 (V2-A
 * plate R13 250K, cathode R15 4K7; V2-B driver, cathode R18 560R) ->
 * T2 UTC PA-5946 output transformer -> Line Out. No speaker (this is a DI).
 *
 * The VST exposes the module's real controls; the stepped EQ switches are
 * mapped onto continuous knobs (0.5 = the switch's "0"/flat position).
 */
enum M610ParamId
{
    kGain = 0,    // V1 drive (the module's gain trim)        [RS Gain]
    kLowEq,       // S3 LF shelf  -6..+6 dB  (0.5 = flat)     [RS Bass]
    kHighEq,      // S2 HF shelf  -6..+6 dB  (0.5 = flat)     [RS Treble]
    kLevel,       // output level into the line out
    kHiGain,      // S1 Lo(0)/Hi(1) Gain switch
    kMicLine,     // input: Line(0, padded) / Mic(1, full step-up = hotter)
    kParamCount
};

static const char* const kM610Names[kParamCount] = {
    "Gain", "Low EQ", "High EQ", "Level", "Hi Gain", "Mic",
};

static const char* const kM610Symbols[kParamCount] = {
    "gain", "loweq", "higheq", "level", "higain", "mic",
};

static const float kM610Min[kParamCount] = { 0, 0, 0, 0, 0, 0 };
static const float kM610Max[kParamCount] = { 1, 1, 1, 1, 1, 1 };
// Defaults: moderate gain, EQ flat, level at the loudness reference, Lo gain,
// Line input (instrument DI).
static const float kM610Def[kParamCount] = { 0.45f, 0.50f, 0.50f, 0.60f, 0.0f, 0.0f };

#endif // M610_PARAMS_H
