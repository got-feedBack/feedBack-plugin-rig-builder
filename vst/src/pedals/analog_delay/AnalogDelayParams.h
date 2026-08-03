#ifndef ANALOG_DELAY_PARAMS_H
#define ANALOG_DELAY_PARAMS_H

// FM104 — Moog MF-104M Analog Delay, FULL real panel. Original 5 params keep
// their ids/names (existing seeds untouched); the MF-104M's missing controls
// are appended: SHORT/LONG delay range, and the LFO section (Rate / Amount /
// Waveform) that modulates the BBD clock (chorus/vibrato on the delay line).
enum AnalogDelayParamId
{
    kDrive = 0,
    kOutput,
    kTime,
    kFeedback,
    kMix,
    kRange,      // SHORT (0) / LONG (1)
    kRate,       // LFO rate
    kAmount,     // LFO amount
    kWaveform,   // LFO shape: sine/tri/square/saw/ramp/S&H
    kParamCount
};

static const char* const kAnalogDelayNames[kParamCount] = {
    "Drive",
    "Output",
    "Time",
    "Feedback",
    "Mix",
    "Range",
    "Rate",
    "Amount",
    "Waveform",
};

static const char* const kAnalogDelaySymbols[kParamCount] = {
    "drive",
    "output",
    "time",
    "feedback",
    "mix",
    "range",
    "rate",
    "amount",
    "waveform",
};

static const float kAnalogDelayMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kAnalogDelayMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kAnalogDelayDef[kParamCount] = {
    0.50f,
    0.50f,
    0.50f,
    0.20f,
    0.50f,
    0.0f,     // SHORT (matches the old fixed window)
    0.50f,    // logarithmic 0.05-50 Hz LFO rate
    0.00f,    // no LFO modulation at zero, as on the hardware
    0.0f,     // sine (the old hardcoded shape)
};

#endif // ANALOG_DELAY_PARAMS_H
