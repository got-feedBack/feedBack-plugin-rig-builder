#ifndef SUPER_DRIVE_PARAMS_H
#define SUPER_DRIVE_PARAMS_H

enum SuperDriveParamId
{
    kGain = 0,
    kTone,
    kParamCount
};

static const char* const kSuperDriveNames[kParamCount] = {
    "Gain",
    "Tone",
};

static const char* const kSuperDriveSymbols[kParamCount] = {
    "gain",
    "tone",
};

static const float kSuperDriveMin[kParamCount] = { 0.0f, 0.0f };
static const float kSuperDriveMax[kParamCount] = { 1.0f, 1.0f };
static const float kSuperDriveDef[kParamCount] = { 0.45f, 0.50f };

#endif // SUPER_DRIVE_PARAMS_H
