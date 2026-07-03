#ifndef COSMIC_ECHO_PARAMS_H
#define COSMIC_ECHO_PARAMS_H

// the game "Cosmic Echo" -> Synthrotek ECHO / PT2399-style echo.
// the game exposes only the three musical controls.
enum CosmicEchoParamId
{
    kDelayLength = 0,
    kFeedback,
    kMix,
    kParamCount
};

static const char* const kCosmicEchoNames[kParamCount] = {
    "Delay Length",
    "Feedback",
    "Mix",
};

static const char* const kCosmicEchoSymbols[kParamCount] = {
    "delay_length",
    "feedback",
    "mix",
};

static const float kCosmicEchoMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kCosmicEchoMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kCosmicEchoDef[kParamCount] = {
    (420.0f - 28.0f) / (650.0f - 28.0f),
    0.28f,
    0.24f,
};

#endif // COSMIC_ECHO_PARAMS_H
