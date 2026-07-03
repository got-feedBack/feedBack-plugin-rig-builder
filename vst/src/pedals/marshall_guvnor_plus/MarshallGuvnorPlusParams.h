#ifndef MARSHALL_GUVNOR_PLUS_PARAMS_H
#define MARSHALL_GUVNOR_PLUS_PARAMS_H

enum MarshallGuvnorPlusParamId
{
    kGain = 0,
    kBass,
    kMid,
    kTreble,
    kDeep,
    kVolume,
    kParamCount
};

static const char* const kMarshallGuvnorPlusNames[kParamCount] = {
    "Gain",
    "Bass",
    "Mid",
    "Treble",
    "Deep",
    "Volume",
};

static const char* const kMarshallGuvnorPlusSymbols[kParamCount] = {
    "gain",
    "bass",
    "mid",
    "treble",
    "deep",
    "volume",
};

static const float kMarshallGuvnorPlusMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kMarshallGuvnorPlusMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kMarshallGuvnorPlusDef[kParamCount] = { 0.45f, 0.52f, 0.56f, 0.54f, 0.38f, 0.62f };

#endif // MARSHALL_GUVNOR_PLUS_PARAMS_H
