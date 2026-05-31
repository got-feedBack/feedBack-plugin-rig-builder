#pragma once
// Rocksmith EQ5 bands, modeled on the Mesa Boogie 5-band graphic EQ. The Mesa
// uses real series-LC resonators (≈60/240/750/2200/6600 Hz) summed in parallel
// — same resonant-bandpass model as the gyrator EQs, but with BROAD bands
// (low Q) which is the Mesa's signature. Freqs are RS's (the cap/L values).
static const int kEqBands = 5;
static const float kEqFreqs[kEqBands] = { 63.f, 250.f, 750.f, 2200.f, 5700.f };
static const char* const kEqNames[kEqBands] = { "63", "250", "750", "2200", "5700" };
#define EQ_PLUGIN_LABEL "EQ5"
#define EQ_UNIQUE_ID    d_cconst('R','E','Q','5')
#define EQ_Q  0.9f
#define EQ_DB 15.0f
