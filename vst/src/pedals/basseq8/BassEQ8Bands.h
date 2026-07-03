#pragma once
// the game Bass EQ8 bands, modeled on the Boss GE-7B gyrator topology
// (same circuit as the GE-7, bass-tuned caps). Freqs are RS's.
static const int kEqBands = 8;
static const float kEqFreqs[kEqBands] = { 30.f, 75.f, 185.f, 460.f, 1100.f, 2700.f, 6800.f, 16000.f };
static const char* const kEqNames[kEqBands] = { "30", "75", "185", "460", "1100", "2700", "6800", "16000" };
#define EQ_PLUGIN_LABEL "Bass EQ8"
#define EQ_UNIQUE_ID    d_cconst('R','B','8','B')
#define EQ_Q  1.4f
#define EQ_DB 15.0f
#define EQ_ACR 210
#define EQ_ACG 206
#define EQ_ACB 194
#define EQ_NAME1 "Bass"
#define EQ_NAME2 "Equalizer"
