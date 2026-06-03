#ifndef MODERN_WAH_PARAMS_H
#define MODERN_WAH_PARAMS_H

// Rocksmith "Modern Wah" -> Morley Bad Horsie / optical contour wah style.
// Same four Rocksmith controls as the wah family.
enum ModernWahParamId { kAuto = 0, kPedal, kSens, kSpeed, kParamCount };

static const char* const kModernWahNames[kParamCount]   = { "Auto", "Pedal", "Sens", "Speed" };
static const char* const kModernWahSymbols[kParamCount] = { "auto", "pedal", "sens", "speed" };

static const float kModernWahMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kModernWahMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kModernWahDef[kParamCount] = { 1.0f, 0.50f, 0.68f, 0.68f };

#endif // MODERN_WAH_PARAMS_H
