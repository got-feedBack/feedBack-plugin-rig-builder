#ifndef LINE_DRIVE_PARAMS_H
#define LINE_DRIVE_PARAMS_H

enum LineDriveParamId
{
    kGain = 0,
    kTone,
    kParamCount
};

static const char* const kLineDriveNames[kParamCount] = {
    "Gain",
    "Tone",
};

static const char* const kLineDriveSymbols[kParamCount] = {
    "gain",
    "tone",
};

static const float kLineDriveMin[kParamCount] = { 0.0f, 0.0f };
static const float kLineDriveMax[kParamCount] = { 1.0f, 1.0f };
static const float kLineDriveDef[kParamCount] = { 0.45f, 0.50f };

#endif // LINE_DRIVE_PARAMS_H
