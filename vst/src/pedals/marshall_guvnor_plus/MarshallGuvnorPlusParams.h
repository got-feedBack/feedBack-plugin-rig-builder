#ifndef MARSHALL_GUVNOR_PLUS_PARAMS_H
#define MARSHALL_GUVNOR_PLUS_PARAMS_H

enum MarshallGuvnorPlusParamId
{
    kGain = 0,
    kBass,
    kMid,
    kTreble,
    kDeep,
    kParamCount
};

static const char* const kMarshallGuvnorPlusNames[kParamCount] = {
    "Gain",
    "Bass",
    "Mid",
    "Treble",
    "Deep",
};

static const char* const kMarshallGuvnorPlusSymbols[kParamCount] = {
    "gain",
    "bass",
    "mid",
    "treble",
    "deep",
};

static const float kMarshallGuvnorPlusMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kMarshallGuvnorPlusMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kMarshallGuvnorPlusDef[kParamCount] = { 0.45f, 0.52f, 0.56f, 0.54f, 0.38f };

#endif // MARSHALL_GUVNOR_PLUS_PARAMS_H
