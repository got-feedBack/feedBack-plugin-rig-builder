#ifndef SHRED_ZONE_PARAMS_H
#define SHRED_ZONE_PARAMS_H

enum ShredZoneParamId
{
    kGain = 0,
    kBass,
    kMid,
    kTreble,
    kParamCount
};

static const char* const kShredZoneNames[kParamCount] = {
    "Gain",
    "Bass",
    "Mid",
    "Treble",
};

static const char* const kShredZoneSymbols[kParamCount] = {
    "gain",
    "bass",
    "mid",
    "treble",
};

static const float kShredZoneMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kShredZoneMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kShredZoneDef[kParamCount] = { 0.70f, 0.50f, 0.50f, 0.50f };

#endif // SHRED_ZONE_PARAMS_H
