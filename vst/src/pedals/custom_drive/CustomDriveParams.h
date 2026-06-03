#ifndef CUSTOM_DRIVE_PARAMS_H
#define CUSTOM_DRIVE_PARAMS_H

enum CustomDriveParamId
{
    kGain = 0,
    kTone,
    kVoice,
    kParamCount
};

static const char* const kCustomDriveNames[kParamCount] = {
    "Gain",
    "Tone",
    "Voice",
};

static const char* const kCustomDriveSymbols[kParamCount] = {
    "gain",
    "tone",
    "voice",
};

static const float kCustomDriveMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kCustomDriveMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kCustomDriveDef[kParamCount] = { 0.22f, 0.50f, 0.0f };

#endif // CUSTOM_DRIVE_PARAMS_H
