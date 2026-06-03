#ifndef BASS_WAH_PARAMS_H
#define BASS_WAH_PARAMS_H

// Rocksmith "Bass Wah" -> Dunlop Cry Baby Bass Wah (105Q). A resonant band-pass
// "wah" filter swept across a bass-friendly range. Rocksmith drives it through
// four knobs shared by the whole wah family (Pedal_UKWah etc.):
//   Auto  = auto-sweep on/off (switch). When on, an LFO moves the wah; when
//           off, the filter sits at the manual Pedal position (cocked wah).
//   Pedal = treadle position (the manual wah frequency when Auto is off)
//   Sens  = envelope sensitivity — how much your picking dynamics open the wah
//   Speed = LFO rate of the auto-sweep (only meaningful when Auto is on)
enum BassWahParamId { kAuto = 0, kPedal, kSens, kSpeed, kParamCount };

static const char* const kBassWahNames[kParamCount]   = { "Auto", "Pedal", "Sens", "Speed" };
static const char* const kBassWahSymbols[kParamCount] = { "auto", "pedal", "sens", "speed" };

static const float kBassWahMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassWahMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
// Auto on (matches every RS song that uses the family), gentle sweep defaults.
static const float kBassWahDef[kParamCount] = { 1.0f, 0.25f, 0.60f, 0.40f };

#endif // BASS_WAH_PARAMS_H
