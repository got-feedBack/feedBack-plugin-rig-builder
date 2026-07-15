#ifndef SILVER_JUBILEE_PARAMS_H
#define SILVER_JUBILEE_PARAMS_H

/*
 * MARSTEN SILVER JUBILEE = Marshall 2555 Silver Jubilee (25th Anniversary,
 * 50/100W head) — the FULL front panel, 1:1, from the official Marshall 2555
 * STD schematic (file 2555.DGM, iss.3 6-6-88; tubes V1-3 ECC83, V4-7 EL34,
 * PCB JM112D). Parody brand "Marsten" (same family as the Marsten JCM800 /
 * Plexi / DSL100). The in-app face must never read "Marshall".
 *
 * What makes the Jubilee a Jubilee (vs a JCM800): a DIODE CLIPPER in the
 * preamp — LED3+D1+D2 against LED2+D3, asymmetric — sits
 * between the V1 cascade and the Lead Master. That diode grind (not pure tube
 * clipping) is the singing, compressed, even-harmonic-rich Jubilee voice. The
 * GAIN pot's PULL switch engages "Rhythm Clip" (D4/D5 + C6), which tightens
 * the clip for chord work and adds C6's high-frequency shunt.
 *
 * Signal path: IN -> V1A -> GAIN -> V1B -> diode clipper -> LEAD MASTER ->
 * V2A -> [FX loop] -> V2B -> Jubilee EQ (VR4 220k linear, VR5 100k log,
 * tandem VR6 1M log; C26/C27/C8-C11/C7) -> OUTPUT MASTER -> ECC83 V3 LTP ->
 * 4x EL34 (~100W) + PRESENCE (power-amp NFB). Cab Sim is a temporary internal
 * 4x12 voice for auditioning without an external cab/IR.
 *
 * EXTRA gear (not mapped to any RS song yet) — panel is the real 2555.
 */
enum SilverJubileeParamId
{
    kGain = 0,      // INPUT GAIN — drives the V1 cascade into the diode clipper
    kLeadMaster,    // LEAD MASTER (VR2) — post-clipper level into V2
    kBass,          // BASS    tandem 1M log control
    kMiddle,        // MIDDLE  100k log control
    kTreble,        // TREBLE  220k linear / C8 220pF
    kPresence,      // PRESENCE — power-amp NFB
    kMaster,        // OUTPUT MASTER (VR3)
    kRhythmClip,    // GAIN pull switch: 0 = Lead clip, 1 = Rhythm clip (tighter)
    kCabSim,        // fallback 4x12 voice: 0 = amp-only, 1 = internal cab sim
    kParamCount
};

static const char* const kSilverJubileeNames[kParamCount] = {
    "Gain", "Lead Master", "Bass", "Middle", "Treble",
    "Presence", "Master", "Rhythm Clip", "Cab Sim",
};
static const char* const kSilverJubileeSymbols[kParamCount] = {
    "gain", "lead_master", "bass", "middle", "treble",
    "presence", "master", "rhythm_clip", "cabsim",
};
static const float kSilverJubileeMin[kParamCount] = { 0,0,0,0,0,0,0,0,0 };
static const float kSilverJubileeMax[kParamCount] = { 1,1,1,1,1,1,1,1,1 };
// Manual-insert defaults: the classic Jubilee lead voice — Gain past noon into
// the diode clip, Lead Master up, tone centred, Rhythm Clip off (lead mode).
static const float kSilverJubileeDef[kParamCount] = {
    0.65f, 0.70f, 0.50f, 0.55f, 0.55f, 0.55f, 0.60f, 0.00f, 1.00f,
};

#endif // SILVER_JUBILEE_PARAMS_H
