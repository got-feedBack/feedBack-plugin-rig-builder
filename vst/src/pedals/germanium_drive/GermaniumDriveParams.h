#ifndef GERMANIUM_DRIVE_PARAMS_H
#define GERMANIUM_DRIVE_PARAMS_H

// Germanium Drive = Hudson Broadcast / Aion Skywave (pedals/germanium drive.pdf).
// Real controls: Gain, Low Cut, Level, Gain Mode and Voltage.
enum GermaniumDriveParamId
{
    kGain = 0,
    kLowCut,
    kLevel,
    kGainMode,
    kVoltage,
    kParamCount
};

static const char* const kGermaniumDriveNames[kParamCount] = {
    "Gain",
    "LowCut",
    "Level",
    "GainMode",
    "Voltage",
};

static const char* const kGermaniumDriveSymbols[kParamCount] = {
    "gain",
    "low_cut",
    "level",
    "gain_mode",
    "voltage",
};

static const float kGermaniumDriveMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kGermaniumDriveMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kGermaniumDriveDef[kParamCount] = { 0.35f, 0.45f, 0.55f, 0.50f, 0.50f };

#endif // GERMANIUM_DRIVE_PARAMS_H
