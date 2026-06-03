#ifndef MARK_IV_PARAMS_H
#define MARK_IV_PARAMS_H

enum MarkIVParamId { kGain = 0, kBass, kMid, kTreble, kParamCount };

static const char* const kMarkIVNames[kParamCount] = { "Gain", "Bass", "Mid", "Treble" };
static const char* const kMarkIVSymbols[kParamCount] = { "gain", "bass", "mid", "treble" };
static const float kMarkIVMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kMarkIVMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kMarkIVDef[kParamCount] = { 0.55f, 0.50f, 0.54f, 0.62f };

#endif // MARK_IV_PARAMS_H
