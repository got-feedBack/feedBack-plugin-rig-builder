#ifndef LINE_DRIVE_PARAMS_H
#define LINE_DRIVE_PARAMS_H

enum LineDriveParamId
{
    kDrive = 0,
    kTone,
    kColor,
    kLevel,
    kParamCount
};

static const char* const kLineDriveNames[kParamCount] = {
    "Drive",
    "Tone",
    "Color",
    "Level",
};

static const char* const kLineDriveSymbols[kParamCount] = {
    "drive",
    "tone",
    "color",
    "level",
};

static const float kLineDriveMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kLineDriveMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kLineDriveDef[kParamCount] = { 0.45f, 0.50f, 0.50f, 0.62f };

#endif // LINE_DRIVE_PARAMS_H
