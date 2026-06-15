#ifndef ROTA_VIBE_PARAMS_H
#define ROTA_VIBE_PARAMS_H
// the game "Rota Vibe" rack -> rotary speaker (Leslie). Knobs:
//   Rate = rotation speed   Depth = Doppler/tremolo depth
//   Mix = dry/wet   Balance = horn (treble) vs rotor (bass) balance
enum RotaVibeParamId { kRate = 0, kDepth, kMix, kBalance, kParamCount };
static const char* const kRotaVibeNames[kParamCount]   = { "Rate", "Depth", "Mix", "Balance" };
static const char* const kRotaVibeSymbols[kParamCount] = { "rate", "depth", "mix", "balance" };
static const float kRotaVibeMin[kParamCount] = { 0,0,0,0 };
static const float kRotaVibeMax[kParamCount] = { 1,1,1,1 };
static const float kRotaVibeDef[kParamCount] = { 0.45f, 0.60f, 0.60f, 0.50f };
#endif
