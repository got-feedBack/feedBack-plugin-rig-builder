#ifndef TAPE_ECHO_PARAMS_H
#define TAPE_ECHO_PARAMS_H

// Rocksmith "Tape Echo" rack -> Roland RE-201 Space Echo. A stereo tape echo:
// dark, saturated repeats with wow & flutter, the two channels spread by Stereo.
//   Time     = echo time (RS stores ms, ~110 .. 120 in the test songs)
//   Feedback = repeats
//   Filter   = tone of the repeats (low-pass; dark .. bright)
//   Stereo   = L/R spread / ping-pong width
//   Mix      = wet/dry blend
enum TapeEchoParamId { kTime = 0, kFeedback, kFilter, kStereo, kMix, kParamCount };

static const char* const kTapeEchoNames[kParamCount]   = { "Time", "Feedback", "Filter", "Stereo", "Mix" };
static const char* const kTapeEchoSymbols[kParamCount] = { "time", "feedback", "filter", "stereo", "mix" };

static const float kTapeEchoMin[kParamCount] = { 0,0,0,0,0 };
static const float kTapeEchoMax[kParamCount] = { 1,1,1,1,1 };
static const float kTapeEchoDef[kParamCount] = { 0.30f, 0.40f, 0.45f, 0.50f, 0.30f };

#endif // TAPE_ECHO_PARAMS_H
