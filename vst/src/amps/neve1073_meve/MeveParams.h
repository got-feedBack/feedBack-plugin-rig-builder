#ifndef MEVE_PARAMS_H
#define MEVE_PARAMS_H

/*
 * MEVE 1073 = Neve 1073 channel amplifier (the class-A console preamp/EQ used
 * as a DI), for the game's DI_Amp_MixerPre ("Amp_Mixer Pre"). Parody brand
 * "Meve" — the in-app face must never read "Neve".
 *
 * Modelled component-by-component from the local schematics:
 *   amps/Neve 1073 Preamp (MixerPre)/1073-fullpak.pdf
 *   (EH10023 module schematic, EK20033 sensitivity switch, BA283AM/AV +
 *    BA284 class-A amplifier cards (BC184C / 2N3055), B182/C HPF board,
 *    B205 hi/low EQ board, B211 presence board, LO1166 output transformer)
 *
 * Real signal path: Mic (T1 10468, HI/LO Z) or Line (T2 31267) input
 * transformer -> EK20033 sensitivity switch (-80..-20 dBm mic / +10..-20 line
 * in 5 dB steps) -> BA284 input amp -> EQ boards (all class-A, BC184C):
 *   HIGH: RV1 10K shelf, fixed 12 kHz, +/-16 dB        (B205)
 *   LOW:  RV2 50K shelf, S3 select 35/60/110/220 Hz, +/-16 dB  (B205)
 *   PRESENCE (mid): RV3 10K peak, S5 select 0.36/0.7/1.6/3.2/4.8/7.2 kHz,
 *                   +/-18 dB                            (B211)
 *   HPF:  S4 select OFF/50/80/160/300 Hz, 3rd-order     (B182/C)
 * -> BA283AV gain/output stages (TR1/TR2 BC184C + TR3 2N3055 class-A,
 *    the famous progressive iron/transistor colour as gain rises) ->
 * T3 LO1166 output transformer -> balanced out. A DI: no speaker.
 *
 * Selector knobs are continuous 0..1 mapped onto the real switch positions.
 */
enum MeveParamId
{
    kGain = 0,    // EK20033 sensitivity (continuous over the stepped range) [RS Gain]
    kLow,         // LOW shelf  +/-16 dB (0.5 = flat)                        [RS Bass]
    kLowFreq,     // LOW freq select: 35 / 60 / 110 / 220 Hz   (4 positions)
    kMid,         // PRESENCE peak +/-18 dB (0.5 = flat)                     [RS Mid]
    kMidFreq,     // PRESENCE freq: 0.36/0.7/1.6/3.2/4.8/7.2 kHz (6 positions)
    kHigh,        // HIGH shelf +/-16 dB @ 12 kHz (0.5 = flat)               [RS Treble]
    kHpf,         // HPF select: OFF / 50 / 80 / 160 / 300 Hz   (5 positions)
    kOutput,      // output level
    kParamCount
};

static const char* const kMeveNames[kParamCount] = {
    "Gain", "Low", "Low Freq", "Mid", "Mid Freq", "High", "HPF", "Output",
};

static const char* const kMeveSymbols[kParamCount] = {
    "gain", "low", "lowfreq", "mid", "midfreq", "high", "hpf", "output",
};

static const float kMeveMin[kParamCount] = { 0,0,0,0,0,0,0,0 };
static const float kMeveMax[kParamCount] = { 1,1,1,1,1,1,1,1 };
// Defaults: moderate sensitivity, EQ flat, LOW @ 110 Hz, PRESENCE @ 1.6 kHz,
// HPF off, output at the loudness reference.
static const float kMeveDef[kParamCount] = {
    0.50f, 0.50f, 0.67f, 0.50f, 0.40f, 0.50f, 0.0f, 0.60f,
};

#endif // MEVE_PARAMS_H
