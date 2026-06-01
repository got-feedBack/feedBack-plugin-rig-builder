#ifndef US_WAH_PARAMS_H
#define US_WAH_PARAMS_H

// Rocksmith "US Wah" -> Dunlop Cry Baby GCB-95 style wah.
//   Auto  = auto sweep on/off
//   Pedal = treadle position / sweep bias
//   Sens  = envelope sensitivity and resonant bite
//   Speed = auto-sweep LFO rate
enum USWahParamId { kAuto = 0, kPedal, kSens, kSpeed, kParamCount };

static const char* const kUSWahNames[kParamCount]   = { "Auto", "Pedal", "Sens", "Speed" };
static const char* const kUSWahSymbols[kParamCount] = { "auto", "pedal", "sens", "speed" };

static const float kUSWahMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kUSWahMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kUSWahDef[kParamCount] = { 1.0f, 0.65f, 0.70f, 0.55f };

#endif // US_WAH_PARAMS_H
