#ifndef DUAL_RECT_PARAMS_H
#define DUAL_RECT_PARAMS_H

/*
 * DUAL RECTIFIER (Mesa/Boogie 3-Channel Dual/Triple Rectifier Solo Head) — the
 * FULL front panel, 1:1, from the local schematic (Boogie_3ch_dual_rectifier.pdf).
 *
 * Three independent channels, each with Gain / Treble / Mid / Bass / Presence /
 * Master + a voicing mode:
 *   CH1 GREEN  : Clean / Pushed                 (clean Fender-ish tone stack)
 *   CH2 ORANGE : Raw / Vintage / Modern         (cascaded hi-gain Recto stack)
 *   CH3 RED    : Raw / Vintage / Modern         (the hottest — the metal sound)
 * Globals: Channel select, Output (master output), Rectifier Bold(silicon)/
 * Spongy(tube) — the "Dual Rectifier" feature (silicon = tight, tube = saggy).
 *
 * the game mapping (rs_knob_to_vst_param.json): the 5 RS knobs map 1:1 onto the
 * Red channel (the signature high-gain voice), with the channel pinned to RED in
 * MODERN mode and a Bold (tight) rectifier:
 *   Gain->Red Gain, Treble->Red Treble, Mid->Red Mid, Bass->Red Bass,
 *   Pres->Red Presence. Output + the other two channels sit at musical defaults
 *   (_static) and stay editable by hand.
 */
enum DualRectParamId
{
    // --- globals ---
    kChannel = 0,   // channel select: Green(0) / Orange(0.5) / Red(1)
    kOutput,        // master Output level
    kRectifier,     // Spongy/tube(0) .. Bold/silicon(1) — sag amount
    // --- CH1 GREEN (clean) ---
    kC1Gain, kC1Treble, kC1Mid, kC1Bass, kC1Presence, kC1Master,
    kC1Mode,        // Clean(0) / Pushed(1)
    // --- CH2 ORANGE ---
    kC2Gain, kC2Treble, kC2Mid, kC2Bass, kC2Presence, kC2Master,
    kC2Mode,        // Raw(0) / Vintage(0.5) / Modern(1)
    // --- CH3 RED (the game high-gain channel) ---
    kC3Gain, kC3Treble, kC3Mid, kC3Bass, kC3Presence, kC3Master,
    kC3Mode,        // Raw(0) / Vintage(0.5) / Modern(1)
    kParamCount
};

static const char* const kDualRectNames[kParamCount] = {
    "Channel", "Output", "Rectifier",
    "Green Gain", "Green Treble", "Green Mid", "Green Bass", "Green Presence", "Green Master", "Green Mode",
    "Orange Gain", "Orange Treble", "Orange Mid", "Orange Bass", "Orange Presence", "Orange Master", "Orange Mode",
    "Red Gain", "Red Treble", "Red Mid", "Red Bass", "Red Presence", "Red Master", "Red Mode",
};

static const char* const kDualRectSymbols[kParamCount] = {
    "channel", "output", "rectifier",
    "c1gain", "c1treble", "c1mid", "c1bass", "c1presence", "c1master", "c1mode",
    "c2gain", "c2treble", "c2mid", "c2bass", "c2presence", "c2master", "c2mode",
    "c3gain", "c3treble", "c3mid", "c3bass", "c3presence", "c3master", "c3mode",
};

static const float kDualRectMin[kParamCount] = {
    0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0 };
static const float kDualRectMax[kParamCount] = {
    1,1,1, 1,1,1,1,1,1,1, 1,1,1,1,1,1,1, 1,1,1,1,1,1,1 };
// Manual-insert default: Red channel, Modern mode, Bold (tight) rectifier, a
// classic high-gain Recto setting (scooped mids); Green/Orange at usable values.
static const float kDualRectDef[kParamCount] = {
    1.00f, 0.60f, 1.00f,                                  // Channel=Red, Output, Rectifier=Bold
    0.35f, 0.55f, 0.55f, 0.50f, 0.40f, 0.55f, 0.00f,      // Green
    0.65f, 0.60f, 0.45f, 0.55f, 0.45f, 0.55f, 1.00f,      // Orange
    0.72f, 0.62f, 0.38f, 0.55f, 0.50f, 0.55f, 1.00f,      // Red (Modern)
};

#endif // DUAL_RECT_PARAMS_H
