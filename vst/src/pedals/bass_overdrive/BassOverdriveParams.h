#ifndef BASS_OVERDRIVE_PARAMS_H
#define BASS_OVERDRIVE_PARAMS_H

// Rocksmith "Bass Overdrive" -> Darkglass Microtubes B3K (CMOS bass overdrive).
// B3K topology: a clean path blended with a distortion path. The distortion is
// CMOS-inverter clipping (asymmetric diodes: 1N4148 + 1N5817 Schottky). RS knobs
// map 1:1 to the B3K controls:
//   Blend  = clean/dist mix
//   Gain   = Drive (gain into the CMOS clipper)
//   Filter = Grunt (how much low end feeds the distortion — tight vs grunty)
//   Tone   = Attack (treble/presence of the distorted signal)
enum BassOverdriveParamId { kBlend = 0, kGain, kFilter, kTone, kParamCount };

static const char* const kBassOverdriveNames[kParamCount]   = { "Blend", "Gain", "Filter", "Tone" };
static const char* const kBassOverdriveSymbols[kParamCount] = { "blend", "gain", "filter", "tone" };

static const float kBassOverdriveMin[kParamCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const float kBassOverdriveMax[kParamCount] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float kBassOverdriveDef[kParamCount] = { 0.60f, 0.70f, 0.40f, 0.50f };

#endif // BASS_OVERDRIVE_PARAMS_H
