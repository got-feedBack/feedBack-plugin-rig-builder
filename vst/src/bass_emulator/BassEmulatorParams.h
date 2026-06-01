#ifndef BASS_EMULATOR_PARAMS_H
#define BASS_EMULATOR_PARAMS_H

// Rocksmith Pedal_BassEmulator exposes Body and Tone.
enum BassEmulatorParamId
{
    kBody = 0,
    kTone,
    kParamCount
};

static const char* const kBassEmulatorNames[kParamCount] = {
    "Body",
    "Tone",
};

static const char* const kBassEmulatorSymbols[kParamCount] = {
    "body",
    "tone",
};

static const float kBassEmulatorMin[kParamCount] = { 0.0f, 0.0f };
static const float kBassEmulatorMax[kParamCount] = { 1.0f, 1.0f };
static const float kBassEmulatorDef[kParamCount] = { 0.65f, 0.48f };

#endif // BASS_EMULATOR_PARAMS_H
