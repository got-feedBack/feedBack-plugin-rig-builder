#ifndef MICRO_AMP_PARAMS_H
#define MICRO_AMP_PARAMS_H

/*
 * MICRO AMP — MXR Micro Amp (M133), from the GGG schematic
 * (pedals/MicroAmp/ggg_mamp_sc.pdf). Parody brand (the in-app face never reads
 * "MXR").
 *
 * A single TL061 JFET op-amp in a non-inverting boost: gain = 1 + R4/(R5+R6)
 * with R4 = 56k, R6 = 2k7 and R5 = the 500k reverse-log GAIN pot. So GAIN
 * sweeps from ~unity (R5 = 500k -> ~+0.9 dB) to ~+26 dB (R5 = 0 -> 1 + 56k/2k7
 * ~= 21.7x). A clean, transparent boost (22M input, doesn't load the guitar);
 * pushed hard into a hot signal the TL061 rails and clips softly. One knob.
 *
 * EXTRA gear (not mapped to any RS song).
 */
enum MicroAmpParamId
{
    kGain = 0,    // GAIN (R5, 500k reverse-log) — ~unity to ~+26 dB clean boost
    kParamCount
};

static const char* const kMicroAmpNames[kParamCount]   = { "Gain" };
static const char* const kMicroAmpSymbols[kParamCount] = { "gain" };

static const float kMicroAmpMin[kParamCount] = { 0.0f };
static const float kMicroAmpMax[kParamCount] = { 1.0f };
// Default: a clean, useful boost (~+8 dB) that stays below the TL061 rail on
// the calibrated input — crank it toward max for the rail-clipped grit.
static const float kMicroAmpDef[kParamCount] = { 0.30f };

#endif // MICRO_AMP_PARAMS_H
