#ifndef COSMIC_ECHO_PARAMS_H
#define COSMIC_ECHO_PARAMS_H

// the game "Cosmic Echo" -> Synthrotek ECHO / PT2399-style echo.
// the game exposes only the three musical controls.
enum CosmicEchoParamId
{
    kTime = 0,
    kFeedback,
    kMix,
    kParamCount
};

static const char* const kCosmicEchoNames[kParamCount] = {
    "Time",
    "Feedback",
    "Mix",
};

static const char* const kCosmicEchoSymbols[kParamCount] = {
    "time",
    "feedback",
    "mix",
};

static const float kCosmicEchoMin[kParamCount] = { 0.0f, 0.0f, 0.0f };
static const float kCosmicEchoMax[kParamCount] = { 1.0f, 1.0f, 1.0f };
static const float kCosmicEchoDef[kParamCount] = {
    420.0f / 2000.0f,
    0.28f,
    0.24f,
};

#endif // COSMIC_ECHO_PARAMS_H
