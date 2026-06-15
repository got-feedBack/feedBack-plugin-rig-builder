#pragma once
#include <cmath>
// the game "Amp Eq" pedal -> Fender '59 Bassman 5F6-A passive FMV tone stack.
// This is NOT three independent bands: it is the real passive Bass/Mid/Treble
// network whose controls INTERACT (the famous Fender mid scoop, treble/bass
// coupling through the slope resistor). Modeled as the analog transfer function
// of the circuit (Yeh & Smith, DAFx-06) discretized via the bilinear transform.
//
// RS knob names (must match 1:1): Bass, Mid, Treble (pot positions, RS 0-100 ->
// 0..1) + BassFreq, TrebleFreq, MidShift (RS ~1.0 nominal -> VST 0.5 = stock
// Fender corners; they scale the tone-stack capacitors C2/C1/C3 to move the
// corner frequencies, exactly like swapping the cap value on the real board).
enum { aBass, aMid, aTreble, aBassFreq, aMidShift, aTrebleFreq, aNumParams };

static const char* const kAmpNames[aNumParams] = {
    "Bass", "Mid", "Treble", "BassFreq", "MidShift", "TrebleFreq"
};

// '59 Bassman component values (Yeh's reference set).
//   R1 = treble pot, R2 = bass pot, R3 = mid pot, R4 = slope resistor
//   C1 = treble cap, C2 = bass cap, C3 = mid cap
static const float AEQ_R1 = 250e3f;
static const float AEQ_R2 = 1e6f;
static const float AEQ_R3 = 25e3f;
static const float AEQ_R4 = 56e3f;
static const float AEQ_C1 = 0.25e-9f;   // 250 pF treble cap
static const float AEQ_C2 = 20e-9f;     // 20 nF bass cap
static const float AEQ_C3 = 20e-9f;     // 20 nF mid cap

// Tone stack has real insertion loss (deep mid scoop). Modest recovery gain so
// the band sits near unity at the edges, like the amp's make-up stage. The
// chain make-up + per-song Chain volume absorb the rest.
static const float AEQ_MAKEUP = 1.5f;   // +3.5 dB

// Pot position (clamp away from the rails — Yeh's closed form is singular at
// exactly 0 and 1).
static inline float aeqPot(float v) { return v < 0.001f ? 0.001f : (v > 0.999f ? 0.999f : v); }

// Freq/Shift knob -> capacitor multiplier. 0.5 = stock cap (nominal corner).
// Knob up (toward 1) -> smaller cap -> higher corner; +/- one octave.
static inline float aeqCapMul(float v) { return powf(2.0f, (0.5f - v) * 2.0f); }

#define AEQ_PLUGIN_LABEL "Amp EQ"
