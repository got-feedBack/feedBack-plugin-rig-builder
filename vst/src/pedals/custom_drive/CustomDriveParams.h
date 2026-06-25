#ifndef CUSTOM_DRIVE_PARAMS_H
#define CUSTOM_DRIVE_PARAMS_H

enum CustomDriveParamId
{
    kDrive = 0,
    kTone,
    kVoice,
    kVolume,
    kParamCount
};

static const char* const kCustomDriveNames[kParamCount] = {
    "Drive",
    "Tone",
    "Voice",
    "Volume",
};

static const char* const kCustomDriveSymbols[kParamCount] = {
    "drive",
    "tone",
    "voice",
    "volume",
};

static const float kCustomDriveMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kCustomDriveMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kCustomDriveDef[kParamCount] = { 0.22f, 0.50f, 0.0f, 0.62f };

#endif // CUSTOM_DRIVE_PARAMS_H
