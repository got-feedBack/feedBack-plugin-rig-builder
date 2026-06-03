#ifndef GERMANIUM_DRIVE_PARAMS_H
#define GERMANIUM_DRIVE_PARAMS_H

enum GermaniumDriveParamId
{
    kGain = 0,
    kTone,
    kParamCount
};

static const char* const kGermaniumDriveNames[kParamCount] = {
    "Gain",
    "Tone",
};

static const char* const kGermaniumDriveSymbols[kParamCount] = {
    "gain",
    "tone",
};

static const float kGermaniumDriveMin[kParamCount] = { 0.0f, 0.0f };
static const float kGermaniumDriveMax[kParamCount] = { 1.0f, 1.0f };
static const float kGermaniumDriveDef[kParamCount] = { 0.35f, 0.55f };

#endif // GERMANIUM_DRIVE_PARAMS_H
