#ifndef NYR_BS103_PARAMS_H
#define NYR_BS103_PARAMS_H

// NYR BS103 — monophonic bass synth.
// Original design inspired by the function of a pitch-tracking bass synth:
// detect the played note, resynthesise oscillators (+ Voice + sub-octave)
// through a resonant 4-pole low-pass with a per-note filter envelope and
// chorus/LFO modulation, then blend with dry.
enum BassSynthParamId
{
    kMix = 0,        // dry / synth blend
    kSub,            // sub-octave level
    kCutoff,         // filter cutoff (base)
    kResonance,      // filter resonance
    kEnvelope,       // filter-envelope amount/speed (0 = off/static)
    kShape,          // oscillator waveform: triangle / saw / square
    kVoice,          // extra harmonically-tuned oscillators (body/thickness)
    kMod,            // chorus + filter-LFO modulation amount
    kLevel,          // output level
    kParamCount
};

static const char* const kBassSynthNames[kParamCount] = {
    "Mix", "Sub", "Cutoff", "Resonance", "Envelope", "Shape", "Voice", "Mod", "Level",
};

static const char* const kBassSynthSymbols[kParamCount] = {
    "mix", "sub", "cutoff", "resonance", "envelope", "shape", "voice", "mod", "level",
};

static const float kBassSynthMin[kParamCount] = { 0,0,0,0,0,0,0,0,0 };
static const float kBassSynthMax[kParamCount] = { 1,1,1,1,1,1,1,1,1 };
// Softer-by-default voicing: some dry in the mix, saw wave, rounded cutoff,
// gentle resonance, a little Voice body + gentle Mod movement.
static const float kBassSynthDef[kParamCount] = {
    0.65f,  // Mix
    0.40f,  // Sub
    0.45f,  // Cutoff
    0.28f,  // Resonance
    0.30f,  // Envelope
    0.45f,  // Shape (saw)
    0.00f,  // Voice (button: off by default = clean/in-tune; toggle on for body)
    0.15f,  // Mod
    0.50f,  // Level
};

#endif // NYR_BS103_PARAMS_H
