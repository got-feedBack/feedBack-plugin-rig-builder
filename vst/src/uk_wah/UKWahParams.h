#ifndef UK_WAH_PARAMS_H
#define UK_WAH_PARAMS_H

// Rocksmith "UK Wah" -> Vox V847 / Clyde McCoy style wah.
// Same four Rocksmith controls as the other wah pedals.
enum UKWahParamId { kAuto = 0, kPedal, kSens, kSpeed, kParamCount };

static const char* const kUKWahNames[kParamCount]   = { "Auto", "Pedal", "Sens", "Speed" };
static const char* const kUKWahSymbols[kParamCount] = { "auto", "pedal", "sens", "speed" };

static const float kUKWahMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kUKWahMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kUKWahDef[kParamCount] = { 1.0f, 0.28f, 0.68f, 0.55f };

#endif // UK_WAH_PARAMS_H
