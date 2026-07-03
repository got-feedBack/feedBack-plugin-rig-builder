#ifndef SINAMP_PARAMS_H
#define SINAMP_PARAMS_H

/*
 * SINAMP BASS DRIVER = Tech 21 SansAmp Bass Driver DI (V2), for the game's
 * DI_Amp_BassDriver ("Amp_Bass Driver"). Parody brand "SinAmp" (sans = "sin"
 * en español — same joke, different language); the in-app face must never
 * read "Tech 21" / "SansAmp".
 *
 * Modelled component-by-component from the local schematic:
 *   amps/TECH21 Sansamp Bass Driver (BassDriver)/sansamp_v2_mod.pdf
 *   (KiCad redraw of the V2 board: TLC2272 op-amp chain, 3.3V zener clippers,
 *    CD4013 FX switching, NJM27520 rails)
 *
 * Real signal path: J1 -> U1B input buffer -> U1A PRESENCE stage (VR1 in the
 * feedback with C5 10n / C7 100p = variable pre-clip HF boost) -> U2A DRIVE
 * (VR2, R15 220K feedback) with D4/D5 3.3V zeners (the "tube" clipping) ->
 * U2B -> U3B/U3A fixed post filters (the speaker-emulation rolloff) ->
 * VR3 BLEND (clean buffer path vs amp path) -> VR4 LEVEL -> active tone:
 * VR7 BASS (+SW2 BASS-SHIFT), VR5 MID (+SW1 MID-SHIFT), VR6 TREBLE ->
 * U5A output. A DI: the speaker voicing IS part of the box (no external cab
 * model added on top).
 */
enum SinAmpParamId
{
    kDrive = 0,   // VR2 DRIVE - gain into the zener clippers   [RS Gain]
    kPresence,    // VR1 PRESENCE - pre-clip HF boost
    kBlend,       // VR3 BLEND - dry buffer vs amp-emulation path
    kBass,        // VR7 BASS  +/-12 dB shelf @ 80 Hz (shift: 40 Hz)
    kMid,         // VR5 MID   +/-12 dB peak  @ 1 kHz (shift: 500 Hz)
    kTreble,      // VR6 TREBLE +/-12 dB shelf @ 3.2 kHz
    kLevel,       // VR4 LEVEL - output
    kBassShift,   // SW2 BASS-SHIFT: 80 Hz -> 40 Hz
    kMidShift,    // SW1 MID-SHIFT: 1 kHz -> 500 Hz
    kParamCount
};

static const char* const kSinAmpNames[kParamCount] = {
    "Drive", "Presence", "Blend", "Bass", "Mid", "Treble", "Level",
    "Bass Shift", "Mid Shift",
};

static const char* const kSinAmpSymbols[kParamCount] = {
    "drive", "presence", "blend", "bass", "mid", "treble", "level",
    "bassshift", "midshift",
};

static const float kSinAmpMin[kParamCount] = { 0,0,0,0,0,0,0,0,0 };
static const float kSinAmpMax[kParamCount] = { 1,1,1,1,1,1,1,1,1 };
// Defaults: moderate drive, a little presence, BLEND full wet (the classic
// BDDI setting), tone flat, level at the loudness reference, shifts off.
static const float kSinAmpDef[kParamCount] = {
    0.50f, 0.35f, 1.00f, 0.50f, 0.50f, 0.50f, 0.60f, 0.0f, 0.0f,
};

#endif // SINAMP_PARAMS_H
