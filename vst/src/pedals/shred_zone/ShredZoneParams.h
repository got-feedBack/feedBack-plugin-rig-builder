#ifndef SHRED_ZONE_PARAMS_H
#define SHRED_ZONE_PARAMS_H

enum ShredZoneParamId
{
    kDist = 0,
    kLow,
    kHigh,
    kMiddle,
    kMiddleFreq,
    kLevel,
    kParamCount
};

static const char* const kShredZoneNames[kParamCount] = {
    "Dist",
    "Low",
    "High",
    "Middle",
    "MiddleFreq",
    "Level",
};

static const char* const kShredZoneSymbols[kParamCount] = {
    "dist",
    "low",
    "high",
    "middle",
    "middle_freq",
    "level",
};

static const float kShredZoneMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kShredZoneMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kShredZoneDef[kParamCount] = { 0.70f, 0.50f, 0.50f, 0.50f, 0.48f, 0.62f };

#endif // SHRED_ZONE_PARAMS_H
