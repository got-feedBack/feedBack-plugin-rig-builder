#ifndef MARK_III_PARAMS_H
#define MARK_III_PARAMS_H

enum MarkIIIParamId { kGain = 0, kBass, kMid, kTreble, kParamCount };

static const char* const kMarkIIINames[kParamCount] = { "Gain", "Bass", "Mid", "Treble" };
static const char* const kMarkIIISymbols[kParamCount] = { "gain", "bass", "mid", "treble" };
static const float kMarkIIIMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kMarkIIIMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kMarkIIIDef[kParamCount] = { 0.46f, 0.48f, 0.58f, 0.64f };

#endif // MARK_III_PARAMS_H
