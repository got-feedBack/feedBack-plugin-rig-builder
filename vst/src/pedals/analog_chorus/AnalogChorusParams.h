#ifndef ANALOG_CHORUS_PARAMS_H
#define ANALOG_CHORUS_PARAMS_H

enum AnalogChorusParamId
{
    kBass = 0,
    kTreble,
    kIntensity,
    kWidth,
    kSpeed,
    kParamCount
};

static const char* const kAnalogChorusNames[kParamCount] = {
    "Bass",
    "Treble",
    "Intensity",
    "Width",
    "Speed",
};

static const char* const kAnalogChorusSymbols[kParamCount] = {
    "bass",
    "treble",
    "intensity",
    "width",
    "speed",
};

static const float kAnalogChorusMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAnalogChorusMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAnalogChorusDef[kParamCount] = { 0.50f, 0.52f, 0.48f, 0.58f, 0.18f };

#endif // ANALOG_CHORUS_PARAMS_H
