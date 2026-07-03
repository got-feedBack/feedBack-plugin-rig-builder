#ifndef CHORUS20_PARAMS_H
#define CHORUS20_PARAMS_H

enum Chorus20ParamId
{
    kIntensity = 0,
    kSpeed1,
    kSpeed2,
    kSpeedSelect,
    kVolume,
    kMode,
    kParamCount
};

static const char* const kChorus20Names[kParamCount] = {
    "Intensity",
    "Speed1",
    "Speed2",
    "SpeedSel",
    "Volume",
    "Mode",
};

static const char* const kChorus20Symbols[kParamCount] = {
    "intensity",
    "speed1",
    "speed2",
    "speedselect",
    "volume",
    "mode",
};

static const float kChorus20Min[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kChorus20Max[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kChorus20Def[kParamCount] = { 0.62f, 0.26f, 0.44f, 0.0f, 0.62f, 0.0f };

#endif // CHORUS20_PARAMS_H
