#ifndef PHASER_363_PARAMS_H
#define PHASER_363_PARAMS_H

enum Phaser363ParamId
{
    kRate = 0,
    kParamCount
};

static const char* const kPhaser363Names[kParamCount] = {
    "Rate",
};

static const char* const kPhaser363Symbols[kParamCount] = {
    "rate",
};

static const float kPhaser363Min[kParamCount] = { 0.0f };
static const float kPhaser363Max[kParamCount] = { 1.0f };
static const float kPhaser363Def[kParamCount] = { 0.25f };

#endif // PHASER_363_PARAMS_H
