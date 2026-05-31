#pragma once
static const int kEqBands = 8;
static const float kEqFreqs[kEqBands] = { 50.f, 100.f, 200.f, 400.f, 800.f, 1600.f, 3200.f, 6400.f };
static const char* const kEqNames[kEqBands] = { "50", "100", "200", "400", "800", "1600", "3200", "6400" };
#define EQ_PLUGIN_LABEL "EQ8"
#define EQ_UNIQUE_ID    d_cconst('R','E','Q','8')
#define EQ_Q  1.4f
#define EQ_DB 15.0f
