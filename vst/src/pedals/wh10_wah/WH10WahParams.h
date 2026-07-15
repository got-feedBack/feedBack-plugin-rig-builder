#ifndef WH10_WAH_PARAMS_H
#define WH10_WAH_PARAMS_H

/*
 * WH10 WAH — Ibañez WH10 (Hoshino Gakki WN10). Parody brand (the in-app face
 * reads "Ibañez", never "Ibanez"). Reference: pedals/Ibanez WH10/ibanez_wh10_wah.pdf.
 *
 * NOT an inductor Cry Baby — the WH10 is an ACTIVE op-amp wah. From the
 * schematic: a 2SC1815 input buffer (Q1) -> a swept resonant band-pass built
 * around the NJM4558 dual op-amp (IC1A + IC1B) with the FREQ treadle pot
 * (VR1 50k) moving the centre frequency, a DEPTH pot (VR2 10k) setting the
 * resonance/mix, a GUITAR/BASS switch (S2) that drops the whole range for bass,
 * and 2SA1015/2SC1815/2SK30A output buffers. Its signature: a WIDE, vocal sweep
 * that goes lower and throatier than a Cry Baby, and grinds into op-amp
 * overdrive when you dig in with DEPTH up (the Frusciante / Tim Henson tone).
 *
 * EXTRA gear (not mapped to any RS song).
 */
// The real panel is treadle + DEPTH + GUITAR/BASS. In-game there is NO
// expression pedal, so AUTO (envelope touch-sweep) + SENS make the wah
// PLAYABLE — same contract as every other wah in the game (US/UK/Modern all
// carry Auto/Sens). The param ORDER matches the P.wh10wah canvas face, which
// maps knobs by index: Auto0 Position1 Depth2 Sens3 Range4.
enum WH10WahParamId
{
    kAuto = 0,     // Auto envelope-sweep on/off (game affordance)
    kPosition,     // FREQ treadle (VR1 50k) — manual cocked-wah / sweep floor
    kDepth,        // DEPTH (VR2 10k)
    kSens,         // envelope sensitivity (drives the auto sweep)
    kRange,        // S2: 0 = Guitar, 1 = Bass
    kParamCount
};

static const char* const kWH10WahNames[kParamCount]   = { "Auto", "Position", "Depth", "Sens", "Range" };
static const char* const kWH10WahSymbols[kParamCount] = { "auto", "position", "depth", "sens", "range" };

static const float kWH10WahMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kWH10WahMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kWH10WahDef[kParamCount] = { 1.0f, 0.50f, 0.68f, 0.60f, 0.0f };

#endif // WH10_WAH_PARAMS_H
