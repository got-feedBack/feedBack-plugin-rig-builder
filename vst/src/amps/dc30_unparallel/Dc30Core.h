#ifndef DC30_CORE_H
#define DC30_CORE_H
//
// Dc30Core — Matchless DC30 (parody "Unparallel"), circuit-real, modelled on the
// AC30 BoxDC30Core (same EL84 class-A / no-NFB family) + the DC30's TWO real
// channels and the EF86 pentode:
//   CHANNEL 1 "Brilliant" (Top Boost): 2x 12AX7 -> DC30 top-boost stack (220k/56pF
//     treble, 1M/.022 bass, no mid).
//   CHANNEL 2 "EF86": one EF86 PENTODE (higher gain, fatter/darker) -> 6-position
//     Tone cap rotary.
// Shared: CUT (250k/.047uF treble shunt, post power), 12AX7 LTP PI, 4x EL84 class-A
// cathode-bias NO global NFB, GZ34-ish B+ nodes, OT + fallback 2x12. (schematic
// amps/Matchless DC30 (BTQ-30)/Matchless-DC30-Old-Schematic.pdf.) Reuses the AC30
// OT / speaker / Biquad blocks from BoxDC30Core.
//
#include "../en30/BoxDC30Core.h"
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace dc30 {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }
using Biquad = boxdc30::Biquad;
using Ac30OutputTransformer = boxdc30::Ac30OutputTransformer;
using Ac30FallbackSpeaker = boxdc30::Ac30FallbackSpeaker;
static inline float audioA(float v){ return boxdc30::ac30pot::audioA(v); }

struct Dc30Core {
    float sr = 48000.0f;
    rbtube::HP1 inputCoupling;
    rbtube::TubeStage v1, v2, v3;                 // Ch1 12AX7 cascade (+ v3 = shared recovery)
    rbtube::TubeStageT<rbtube::TubeEF86> ef86;    // Ch2 EF86 pentode
    rbtube::Miller12AX7 inputMiller, millerV2, millerV3;
    rbtube::MillerEF86 ef86Miller;
    rbtube::CouplingCapGridLeak coupleToV2, coupleToV3, coupleToPi;
    rbtube::PhaseInverterLTP12AX7 phaseInverter;  // 12AX7 LTP (Vox)
    rbtube::MultiNodeBPlus supply;
    rbtube::PowerAmpPP power;                      // 4x EL84 class-A, NO NFB
    rbtube::ToneStackYeh tonestack;               // Ch1 DC30 Top Boost stack
    Biquad bright, cutLP, ef86Tone;
    Ac30OutputTransformer outputTransformer;
    Ac30FallbackSpeaker fallbackSpeaker;

    // panel params
    bool ch2=false;
    float pCh1Vol=0.6f, pCh2Vol=0.5f, pBass=0.5f, pTreble=0.6f, pTone=0.5f,
          pCut=0.5f, pMaster=0.7f, pCabSim=1.0f;

    float inGain=1, inScale=4, preGain=1, gainOut=1;
    float inGainE=1, inScaleE=3, preGainE=1, gainOutE=1;
    float masterElectrical=0.0f;
    float lastPowerLoad=0,lastScreenLoad=0,lastPreampLoad=0;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void reset(){ inputCoupling.reset(); inputMiller.reset(); v1.reset();v2.reset();v3.reset(); ef86.reset();
        millerV2.reset();millerV3.reset(); ef86Miller.reset(); coupleToV2.reset();coupleToV3.reset();coupleToPi.reset();
        phaseInverter.reset(); supply.reset(); power.reset(); tonestack.reset(); bright.reset();cutLP.reset();ef86Tone.reset();
        outputTransformer.reset(); fallbackSpeaker.reset(); lastPowerLoad=lastScreenLoad=lastPreampLoad=0; }

    void recalc(){
        inputCoupling.set(sr, 12.0f);
        // Ch1 12AX7 cascade (AC30 top-boost values) + v3 also serves as the EF86 recovery.
        v1.set(sr, 0, 250.0f, 40.0f, 86.0f,  2700.0f);
        v2.set(sr, 1, 250.0f, 40.0f, 132.0f, 1500.0f);
        v3.set(sr, 1, 250.0f, 40.0f, 194.0f,  820.0f);
        // EF86 PENTODE (Rp 330k, Rk 2.2k bypassed -> high gain, soft asymmetric clip).
        ef86.setWithPlate(sr, 0, 300.0f, 40.0f, 3.0f, 2200.0f, 330000.0f);

        const float ch1v   = audioA(pCh1Vol);     // CH1 Brilliant volume = drive
        const float ch2v   = audioA(pCh2Vol);     // CH2 EF86 volume = drive
        const float master = audioA(pMaster);
        const float treble = 0.62f + 0.38f * audioA(pTreble);
        const float bass   = audioA(pBass);
        const float driveVol1 = 0.65f*std::sqrt(ch1v) + 0.35f*ch1v;
        const float driveVolE = 0.65f*std::sqrt(ch2v) + 0.35f*ch2v;
        masterElectrical = master;

        // Ch1 gain ladder (clone BoxDC30 — keeps V1 clean, V2/V3 break up like a cranked AC30)
        inScale = 2.0f * (0.7f + 0.6f * ch1v);
        preGain = 0.45f * (0.35f + 7.8f * driveVol1);
        gainOut = 0.60f + 0.70f * driveVol1;
        // Ch2 EF86: higher gain, breaks up earlier/fatter than Ch1
        inGainE  = 0.40f + 1.6f * pCh2Vol;
        inScaleE = 2.6f * (0.7f + 0.8f * ch2v);
        preGainE = 0.50f * (0.35f + 8.5f * driveVolE);
        gainOutE = 0.55f + 0.70f * driveVolE;
        inGain = 0.40f + 1.6f * pCh1Vol;

        inputMiller.set(sr, 68000.0f, 55.0f, 10.0f);
        millerV2.set(sr, 47000.0f + 90000.0f*(1.0f-ch1v), 52.0f, 8.0f);
        millerV3.set(sr, 180000.0f, 58.0f, 5.0f);
        ef86Miller.set(sr, 68000.0f, 38.0f, 8.0f);
        coupleToV2.set(sr, 1000000.0f, 22.0e-9f, 220000.0f, 0.16f, 0.54f, 1.35f);
        coupleToV3.set(sr, 1000000.0f, 22.0e-9f, 180000.0f, 0.14f, 0.62f, 1.70f);
        coupleToPi.set(sr, 1000000.0f, 47.0e-9f, 10000.0f + 250000.0f*(1.0f-master), 0.18f, 0.45f, 1.20f);

        // CH1 DC30 Top Boost stack: Treble 220k pot/56pF cap, Bass 1M/.022uF, no mid
        // control (mid leg 10k pinned, slope 100k — the proven AC30 Top-Boost response),
        // just the smaller 220k treble pot vs the AC30's 1M.
        tonestack.setComponents(220.0e3, 1.0e6, 10.0e3, 100.0e3, 56.0e-12, 22.0e-9, 22.0e-9);
        tonestack.update(sr, treble, 1.0f, bass);
        // CH2 EF86 6-position Tone: a treble-shunt sweep 56pF(bright)->.01uF(dark)
        // through 1M5. Fatter/darker than Ch1 even at bright.
        ef86Tone.lowpass(sr, 1800.0f + 5200.0f * pTone, 0.70f);
        // CH1 brightness = the 560pF+180pF "Brilliant" bright coupling into the Volume
        // pot (the chimey top-boost voice); baked as a fixed top shelf, eased as Vol cranks.
        bright.highShelf(sr, 2800.0f, ch2 ? 0.0f : (5.0f * (1.0f - 0.4f * ch1v)));

        // CUT: 250k + .047uF treble shunt AFTER the power amp (stronger/lower than the
        // AC30's 220k/4n7 -> reaches further down). Higher knob = darker.
        cutLP.lowpass(sr, 22000.0f - 18000.0f * std::sqrt(clamp01(pCut)), 0.7f);

        supply.setGZ34Ac30(sr, ch2 ? driveVolE : driveVol1);
        phaseInverter.setVoxAc30(sr, 1.15f + 2.55f*master + 0.85f*(ch2?driveVolE:driveVol1), 0.92f, 0.075f);
        power.set(sr, 3.0f + 15.5f*(ch2?driveVolE:driveVol1) + 2.5f*master, -7.5f, 0.32f);
        power.out = 0.0085f;
        outputTransformer.set(sr, ch2?driveVolE:driveVol1);
        fallbackSpeaker.set(sr, treble, std::sqrt(clamp01(pCut)), ch2?driveVolE:driveVol1);
    }

    inline float process(float x){
        const rbtube::SupplyScales bplus = supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        if (!ch2) {
            // CHANNEL 1 "Brilliant" / Top Boost
            x = inputCoupling.process(x * inGain);
            x = bright.process(x);                         // Brilliant bright coupling (chime)
            x = v1.process(inputMiller.process(x) * inScale * bplus.preamp);
            x = tonestack.process(x);
            x = v2.process(coupleToV2.process(millerV2.process(x), preGain * bplus.preamp));
            x = v3.process(coupleToV3.process(millerV3.process(x), preGain * bplus.preamp));
            x = x * gainOut;
        } else {
            // CHANNEL 2 "EF86" pentode -> Tone -> v3 recovery
            x = inputCoupling.process(x * inGainE);
            x = ef86.process(ef86Miller.process(x) * inScaleE * bplus.preamp);
            x = ef86Tone.process(x);
            x = v3.process(coupleToV3.process(millerV3.process(x), preGainE * bplus.preamp));
            x = x * gainOutE;
        }
        x = coupleToPi.process(x, 1.0f);
        lastPreampLoad = std::fabs(x) * (0.20f + 0.40f * (ch2?pCh2Vol:pCh1Vol));
        x = phaseInverter.process(x * bplus.screen);
        lastScreenLoad = std::fabs(x) * (0.35f + 0.60f * masterElectrical);
        x = power.process(x * bplus.power * bplus.screen);
        lastPowerLoad = std::fabs(x) * (0.55f + 0.95f * masterElectrical);
        x = cutLP.process(x);                              // CUT after power (real DC30)
        x = outputTransformer.process(x);
        const float cab = fallbackSpeaker.process(x);
        x += pCabSim * (0.65f * cab - x);

        // Loudness flattening (plugin-level), per channel, decreasing with drive ->
        // ~-16 dBFS cranked, clean stays quieter. (Same method as BoxDC30 gcDb.)
        // ⚠️ Low channel Volume collapses the preamp ~50 dB (clean + quiet, as a real
        // amp does) -> the OLD makeup (cap +24, gentle curve) left it inaudible (~-43
        // dBFS = "no suena a ganancia baja"). A MUCH steeper curve + a +42 cap pulls the
        // quiet low-Vol settings up to audible while the cranked top stays ~-16.
        const float dv = ch2 ? pCh2Vol : pCh1Vol;
        float gcDb = ch2 ? (37.0f - 88.0f*dv + 52.0f*dv*dv)
                         : (52.0f - 120.0f*dv + 66.0f*dv*dv);
        if (gcDb > 42.0f) gcDb = 42.0f; else if (gcDb < -12.0f) gcDb = -12.0f;
        return x * std::pow(10.0f, 0.05f * gcDb);
    }
};

} // namespace dc30
#endif // DC30_CORE_H
