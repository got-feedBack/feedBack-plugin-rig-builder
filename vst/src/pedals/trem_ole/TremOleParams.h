#ifndef TREM_OLE_PARAMS_H
#define TREM_OLE_PARAMS_H

enum TremOleParamId
{
    kRate = 0,
    kDepth,
    kShape,
    kLevel,
    kMode,
    kParamCount
};

static const char* const kTremOleNames[kParamCount] = {
    "Rate",
    "Depth",
    "Shape",
    "Level",
    "Mode",
};

static const char* const kTremOleSymbols[kParamCount] = {
    "rate",
    "depth",
    "shape",
    "level",
    "mode",
};

static const float kTremOleMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float kTremOleMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static const float kTremOleDef[kParamCount] = { 0.42f, 0.58f, 0.35f, 0.50f, 0.33f };

#endif // TREM_OLE_PARAMS_H
