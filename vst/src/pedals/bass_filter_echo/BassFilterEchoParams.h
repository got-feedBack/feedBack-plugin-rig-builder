#ifndef BASS_FILTER_ECHO_PARAMS_H
#define BASS_FILTER_ECHO_PARAMS_H

// Rocksmith "Bass Filter Echo" -> a vintage tape echo (Space Echo / Echoplex
// flavour). Same four knobs as the Bass Filter Delay, but voiced like tape:
// darker, more saturated repeats with wow & flutter pitch wobble.
//   Time     = echo time
//   Feedback = number of repeats
//   Mix      = wet/dry blend
//   Filter   = tone of the repeats (feedback-loop low-pass; lower = darker,
//              more "tape")
enum BassFilterEchoParamId { kTime = 0, kFeedback, kMix, kFilter, kParamCount };

static const char* const kBassFilterEchoNames[kParamCount]   = { "Time", "Feedback", "Mix", "Filter" };
static const char* const kBassFilterEchoSymbols[kParamCount] = { "time", "feedback", "mix", "filter" };

static const float kBassFilterEchoMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassFilterEchoMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassFilterEchoDef[kParamCount] = { 0.45f, 0.45f, 0.40f, 0.45f };

#endif // BASS_FILTER_ECHO_PARAMS_H
