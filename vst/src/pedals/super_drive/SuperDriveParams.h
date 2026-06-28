#ifndef SUPER_DRIVE_PARAMS_H
#define SUPER_DRIVE_PARAMS_H

enum SuperDriveParamId
{
    kDrive = 0,
    kTone,
    kLevel,
    kParamCount
};

static const char* const kSuperDriveNames[kParamCount] = {
    "Drive",
    "Tone",
    "Level",
};

static const char* const kSuperDriveSymbols[kParamCount] = {
    "drive",
    "tone",
    "level",
};

static const float kSuperDriveMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kSuperDriveMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kSuperDriveDef[kParamCount] = { 0.45f, 0.50f, 0.62f };

#endif // SUPER_DRIVE_PARAMS_H
