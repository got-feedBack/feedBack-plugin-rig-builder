#ifndef TAPE_ECHO_PARAMS_H
#define TAPE_ECHO_PARAMS_H

// the game "Tape Echo" rack -> Roland RE-201 Space Echo style tape machine.
//   Time     = repeat-rate / tape speed. RS stores ms as ms/700.
//   Feedback = RE-201 Intensity/regeneration into the record amp
//   Filter   = combined echo Tone/Bass/Treble coloration
//   Stereo   = artificial spread of the three mono playback heads
//   Mix      = Echo Volume / wet-dry blend
enum TapeEchoParamId { kTime = 0, kFeedback, kFilter, kStereo, kMix, kParamCount };

static const char* const kTapeEchoNames[kParamCount]   = { "Time", "Feedback", "Filter", "Stereo", "Mix" };
static const char* const kTapeEchoSymbols[kParamCount] = { "time", "feedback", "filter", "stereo", "mix" };

static const float kTapeEchoMin[kParamCount] = { 0,0,0,0,0 };
static const float kTapeEchoMax[kParamCount] = { 1,1,1,1,1 };
static const float kTapeEchoDef[kParamCount] = { 0.30f, 0.40f, 0.45f, 0.50f, 0.30f };

#endif // TAPE_ECHO_PARAMS_H
