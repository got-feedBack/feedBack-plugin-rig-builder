#ifndef BASS_CHORUS_PARAMS_H
#define BASS_CHORUS_PARAMS_H

// Boss CEB-3 Bass Chorus style model. The real pedal is an ES56028 short
// digital delay with NJM022/M5223 analog conditioning and a bass Low Filter
// path, not a MN3007-style BBD chorus.
// Parameter order is kept stable for old RS presets: Rate, Depth, Low Filter,
// and E.Level. Rocksmith Mix maps to E.Level in the external mapping file.
enum BassChorusParamId { kRate = 0, kDepth, kLowFilter, kELevel, kParamCount };

static const char* const kBassChorusNames[kParamCount]   = { "Rate", "Depth", "Low Filter", "E.Level" };
static const char* const kBassChorusSymbols[kParamCount] = { "rate", "depth", "lowfilter", "elevel" };

static const float kBassChorusMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassChorusMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassChorusDef[kParamCount] = { 0.22f, 0.58f, 0.42f, 0.60f };

#endif // BASS_CHORUS_PARAMS_H
