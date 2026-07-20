#ifndef BOX_DC30_CORE_H
#define BOX_DC30_CORE_H
//
// BoxDC30Core - Vox AC30 Top Boost (parody "BOX AC30"), REBUILT Guitarix-style:
// separate Normal/Top Boost input paths feed the real LTP/EL84 output topology.
// Each nonlinear stage uses anti-aliasing, a Koren tube table and a per-stage
// DC block, so it remains stable at any supported oversampled rate.
// Component-level voicing (AC30 Top Boost stack, Cut, bright, EL84 push-pull power
// + sag, output transformer) sits BETWEEN the tube stages as ordinary stable blocks.
// Tubes use OUR Koren tables (public model). Drop-in param interface == EN30Core.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace boxdc30 {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

static inline float gainLoudnessMakeupDb(float gain, bool topBoost)
{
    // Static post-circuit calibration measured with the Brit DI at 48 kHz,
    // Cab Sim off, Master 0.60. Each table holds its channel at -18.6 dBFS RMS
    // over the real A500K Volume sweep. Since this gain is applied after the
    // PI, EL84s and OT, it cannot alter breakup, sag or harmonic structure.
    static const float kNormalDb[11] = {
        12.115f, 4.905f, 2.144f, 0.093f,-1.592f,-3.032f,
        -4.277f,-5.348f,-6.238f,-6.899f,-7.304f
    };
    static const float kTopBoostDb[11] = {
         8.556f, 4.328f, 2.447f, 0.685f,-1.029f,-2.681f,
        -4.177f,-5.435f,-6.411f,-7.076f,-7.489f
    };
    const float* const kDb = topBoost ? kTopBoostDb : kNormalDb;
    const float p = 10.0f * clamp01(gain);
    int i = (int)p;
    if (i >= 10)
        i = 10;
    const float f = p - (float)i;
    return (i >= 10) ? kDb[10] : kDb[i] + (kDb[i + 1] - kDb[i]) * f;
}

namespace ac30pot {
static constexpr float kAudio15Exp = 2.73696559f; // A taper, approx 15% electrical at half rotation
static constexpr float kVr1NormalVol = 500000.0f; // AC30C2 VR1 A500K
static constexpr float kVr2TbVol     = 500000.0f; // AC30C2 VR2 A500K
static constexpr float kVr3Treble    = 1000000.0f; // AC30C2 VR3 A1M
static constexpr float kVr4Bass      = 1000000.0f; // AC30C2 VR4 A1M
static constexpr float kVr9Cut       = 220000.0f; // AC30C2 VR9 B220K
static constexpr float kVr10Master   = 500000.0f; // AC30C2 VR10 A500K
static constexpr float kToneCutCap   = 4.7e-9f;   // AC30C2 C80 4n7

static inline float audioA(float v) {
    return std::pow(clamp01(v), kAudio15Exp);
}

static inline float linearB(float v) {
    return clamp01(v);
}

static inline float wiperSourceOhms(float potOhms, float electricalPos) {
    const float e = 0.001f + 0.998f * clamp01(electricalPos);
    const float upper = potOhms * (1.0f - e);
    const float lower = potOhms * e;
    return (upper * lower) / std::fmax(1.0f, upper + lower);
}

static inline float toneCutBleed(float knob) {
    // VR9 is linear B220K in the AC30C2. Treat clockwise rotation as lowering the
    // effective series resistance into C80, so more knob means more treble bleed.
    const float kMinSeries = 4700.0f;
    const float r = kMinSeries + kVr9Cut * (1.0f - linearB(knob));
    const float xC4k = 1.0f / (2.0f * kPi * 4000.0f * kToneCutCap);
    const float g = 1.0f / (r + xC4k);
    const float gMin = 1.0f / (kMinSeries + kVr9Cut + xC4k);
    const float gMax = 1.0f / (kMinSeries + xC4k);
    return clamp01((g - gMin) / (gMax - gMin));
}
} // namespace ac30pot

// RBJ biquad (peaking / shelves / low-pass)
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void norm(float a0){ b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0; }
    void peaking(float sr,float f,float Q,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=1+al*A;b1=-2*c;b2=1-al*A; float a0=1+al/A;a1=-2*c;a2=1-al/A; norm(a0); }
    void lowShelf(float sr,float f,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)-(A-1)*c+2*rA*al); b1=2*A*((A-1)-(A+1)*c); b2=A*((A+1)-(A-1)*c-2*rA*al);
        float a0=(A+1)+(A-1)*c+2*rA*al; a1=-2*((A-1)+(A+1)*c); a2=(A+1)+(A-1)*c-2*rA*al; norm(a0); }
    void highShelf(float sr,float f,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)+(A-1)*c+2*rA*al); b1=-2*A*((A-1)+(A+1)*c); b2=A*((A+1)+(A-1)*c-2*rA*al);
        float a0=(A+1)-(A-1)*c+2*rA*al; a1=2*((A-1)-(A+1)*c); a2=(A+1)-(A-1)*c-2*rA*al; norm(a0); }
    void lowpass(float sr,float f,float Q){ f=fminf(f,sr*0.49f); float w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=(1-c)/2;b1=1-c;b2=(1-c)/2; float a0=1+al;a1=-2*c;a2=1-al; norm(a0); }
};

struct Ac30OutputTransformer {
    rbtube::HP1 otHp;
    rbtube::LP1 otLeakLp, fluxLp;
    Biquad windingChime, windingAir;
    float coreDrive = 1.0f;
    float coreMix = 0.12f;

    void set(float sr, float hot) {
        otHp.set(sr, 38.0f);                                      // finite OT primary inductance
        otLeakLp.set(sr, 22000.0f);                               // OT leakage/iron loss only, no speaker rolloff
        fluxLp.set(sr, 24.0f);                                    // low-frequency core flux memory
        // Unloaded digital line-outs were several dB darker than the Ruby
        // direct reference even with Cab Sim disabled.  These shelves model
        // the rising AC30 OT/reactive-load voltage response; they are not a
        // speaker rolloff and remain ahead of any user-selected cabinet IR.
        windingChime.highShelf(sr, 3200.0f, 5.0f);
        windingAir.highShelf(sr, 8000.0f, 8.0f);
        // The direct Ruby renders show little change at low Volume but a large
        // increase in upper harmonics when the PI/EL84 section is cranked. OT
        // magnetisation is therefore driven by the actual panel Volume, with a
        // cubic onset that leaves the clean and noon settings mostly intact.
        const float saturated = hot * hot * hot;
        coreDrive = 1.0f + 3.0f * saturated;
        coreMix = 0.10f + 0.24f * saturated;
    }

    inline float process(float x) {
        float y = otHp.process(x);
        const float flux = fluxLp.process(y);
        // Divide by drive to preserve the small-signal gain. The change is a
        // nonlinear transfer, not an output-volume boost.
        const float core = std::tanh((y - 0.16f * flux) * coreDrive) / coreDrive;
        y = (1.0f - coreMix) * y + coreMix * core;                 // soft OT core bend, not a hard limiter
        y = otLeakLp.process(y);
        y = windingChime.process(y);
        return windingAir.process(y);
    }

    void reset() {
        otHp.reset(); otLeakLp.reset(); fluxLp.reset();
        windingChime.reset(); windingAir.reset();
    }
};

struct Ac30FallbackSpeaker {
    rbtube::HP1 hp;
    Biquad coneRes, lowMidDip, alnicoChime, fizzShelf, upperDamp, coneLp;

    void set(float sr, float treble, float cut, float hot) {
        // Fallback cab voice kept in sync with our synthetic Box_2x12_Alnico IR
        // family. It is not fitted to, copied from, or dependent on any captured IR.
        hp.set(sr, 50.0f);
        coneRes.peaking(sr, 76.0f, 0.82f, 3.2f + 0.5f * hot);       // body to fill the thin lows vs the line-out ref
        lowMidDip.peaking(sr, 480.0f, 0.80f, -2.2f);               // shallower hollow (was masking 315-630)
        alnicoChime.peaking(sr, 2900.0f, 0.82f, 2.4f + 1.2f * treble - 0.5f * cut); // eased (the brighter treble baseline already lifts the upper-mids)
        fizzShelf.highShelf(sr, 4500.0f, 2.4f + 1.3f * treble - 0.3f * hot);
        upperDamp.peaking(sr, 6900.0f, 0.88f, 0.0f);
        coneLp.lowpass(sr, 18000.0f + 1600.0f * treble - 1100.0f * hot, 0.707f);
    }

    inline float process(float x) {
        float y = hp.process(x);
        y = coneRes.process(y);
        y = lowMidDip.process(y);
        y = alnicoChime.process(y);
        y = fizzShelf.process(y);
        y = upperDamp.process(y);
        return coneLp.process(y);
    }

    void reset() {
        hp.reset(); coneRes.reset(); lowMidDip.reset();
        alnicoChime.reset(); fizzShelf.reset(); upperDamp.reset(); coneLp.reset();
    }
};

// V2 cathode follower from the AC30C2 Top Boost path. It is a low-gain buffer
// that develops grid-current compression when driven hard; it is not another
// common-cathode gain stage.
struct Ac30CathodeFollower {
    rbtube::LP1 gridPole;
    float current = 0.0f, attack = 0.0f, release = 0.0f;
    float drive = 1.0f, shapeMix = 0.0f;

    void set(float sr, float volume) {
        gridPole.set(sr, 19000.0f);
        attack = 1.0f - std::exp(-1.0f / (0.0022f * sr));
        release = 1.0f - std::exp(-1.0f / (0.055f * sr));
        const float v = clamp01(volume);
        // Keep the first half open and chimey; grid-current density rises
        // mainly over the upper half of the real Volume rotation.
        drive = 1.0f + 8.0f * v * v * v * v;
        shapeMix = v * v;
    }

    inline float process(float x) {
        const float g = gridPole.process(x) * drive;
        const float positive = std::fmax(0.0f, g - 0.24f);
        current += (positive - current) * (positive > current ? attack : release);
        const float y = g / (1.0f + 0.68f * current);
        // A cathode follower clips asymmetrically when its grid is driven
        // positive.  Different positive/negative headroom preserves the AC30
        // attack while generating the even+odd upper harmonics heard in Ruby.
        const float shaped = y >= 0.0f
            ? 0.72f * std::tanh(y / 0.72f)
            : 1.08f * std::tanh(y / 1.08f);
        // Do not divide the follower output by its drive. That normalization
        // made the circuit feed less level into the recovery/PI at Volume 10
        // than around noon, suppressing the harmonic growth measured in Ruby.
        // Loudness compensation belongs after the complete nonlinear chain.
        return y + shapeMix * (shaped - y);
    }

    void reset() { gridPole.reset(); current = 0.0f; }
};

// C80 4.7 nF + VR9 B220K bridge the two phase-inverter outputs. In the mono
// differential representation, that is a shelving treble shunt: lows pass,
// while the high-frequency differential component is progressively cancelled.
struct Ac30ToneCut {
    rbtube::LP1 low;
    float amount = 0.0f;

    void set(float sr, float knob) {
        low.set(sr, 780.0f);
        const float k = clamp01(knob);
        // Ruby's half-position transfer is much closer to the real B220K
        // network than the old square-root shortcut. Keep the same full-cut
        // endpoint while opening the useful first half of the rotation.
        amount = 0.84f * std::pow(k, 1.30f);
    }

    inline float process(float x) {
        const float l = low.process(x);
        return x + amount * (l - x);
    }

    void reset() { low.reset(); }
};

struct BoxDC30Core {
    float sr = 48000.0f;
    rbtube::HP1 inputCoupling;
    rbtube::TubeStage normalV1, topV1;  // the two independent halves of V1
    rbtube::Miller12AX7 normalMiller, topMiller;
    rbtube::CouplingCapGridLeak normalVolumeCouple, topVolumeCouple, coupleToPi;
    Ac30CathodeFollower topFollower;    // V2, drives the passive Top Boost stack
    rbtube::PhaseInverterLTP12AX7 phaseInverter; // AC30 long-tail-pair 12AX7 splitter
    rbtube::MultiNodeBPlus supply;         // GZ34 + power/screen/preamp filter nodes
    rbtube::PowerAmpPP power;            // real class-A push-pull EL84 (no NFB, AC30)
    rbtube::ToneStackYeh tonestack;     // real Vox Top Boost R/C tone network (Yeh model)
    Biquad bright, rubyChime, rubyAir;
    Ac30ToneCut toneCut;
    Ac30OutputTransformer outputTransformer;
    Ac30FallbackSpeaker fallbackSpeaker;
    // params (0..1), interface identical to EN30Core
    float pInput=0.5f, pNVol=0.7f, pTBVol=0.7f, pTreble=0.5f, pBass=0.5f, pBright=0.5f,
          pCut=0.5f, pMaster=0.7f, pRevTone=0.5f, pRevLevel=0.0f, pSpeed=0.5f, pDepth=0.0f,
          pCabSim=1.0f;
    float inGain=1, outLevel=1, lfoPhase=0, lfoInc=0;
    float normalElectrical=0.0f, topElectrical=0.0f;
    float masterElectrical=0.0f;               // VR10 A500K electrical wiper position after A taper
    float masterDriveElectrical=0.0f;          // calibrated PI/power excitation coordinate
    float lastPowerLoad=0, lastScreenLoad=0, lastPreampLoad=0;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setInput(float v){ pInput=clamp01(v); recalc(); }
    void setNormalVol(float v){ pNVol=clamp01(v); recalc(); }
    void setTBVol(float v){ pTBVol=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setBright(float v){ pBright=clamp01(v); recalc(); }
    void setCut(float v){ pCut=clamp01(v); recalc(); }
    void setMaster(float v){ pMaster=clamp01(v); recalc(); }
    void setRevTone(float v){ pRevTone=clamp01(v); }
    void setRevLevel(float v){ pRevLevel=clamp01(v); }
    void setSpeed(float v){ pSpeed=clamp01(v); recalc(); }
    void setDepth(float v){ pDepth=clamp01(v); }
    void setCabSim(float v){ pCabSim=clamp01(v); recalc(); }
    float outputMakeup() const {
        const float nGate = clamp01((1.0f - pInput) * 2.0f);
        const float tbGate = clamp01(pInput * 2.0f);
        const float nMakeup = gainLoudnessMakeupDb(pNVol, false);
        const float tbMakeup = gainLoudnessMakeupDb(pTBVol, true);
        const float nWeight = nGate * (0.020f + 0.980f * pNVol);
        const float tbWeight = tbGate * (0.020f + 0.980f * pTBVol);
        const float weightSum = std::fmax(1.0e-6f, nWeight + tbWeight);
        const float gcDb = (nWeight * nMakeup + tbWeight * tbMakeup) / weightSum;
        return std::pow(10.0f, 0.05f * gcDb);
    }
    void reset(){ inputCoupling.reset(); normalV1.reset(); topV1.reset(); normalMiller.reset(); topMiller.reset();
        normalVolumeCouple.reset(); topVolumeCouple.reset(); coupleToPi.reset(); topFollower.reset();
        phaseInverter.reset(); supply.reset(); power.reset();
        tonestack.reset(); bright.reset(); rubyChime.reset(); rubyAir.reset(); toneCut.reset();
        outputTransformer.reset(); fallbackSpeaker.reset(); lfoPhase=0;
        lastPowerLoad=lastScreenLoad=lastPreampLoad=0; }

    void recalc(){
        inputCoupling.set(sr, 12.0f);                              // input grid-leak coupling
        // V1 has two independent 12AX7 halves. From Vox_ac30c2.pdf: Normal uses
        // R14 220k + C7 47n; Top Boost uses R12 100k and C9 470p into VR2,
        // then V2 as a cathode follower. There are no two extra common-cathode
        // stages between the tone stack and the phase inverter.
        inGain = 1.0f;
        normalV1.setWithPlate(sr, 0, 250.0f, 40.0f, 86.0f, 1500.0f, 220000.0f);
        topV1.setWithPlate(sr, 0, 250.0f, 40.0f, 86.0f, 1500.0f, 100000.0f);
        const float vol    = ac30pot::audioA(pTBVol);              // VR2 A500K Top Boost Volume
        const float nvol   = ac30pot::audioA(pNVol);               // VR1 A500K Normal Volume
        const float master = ac30pot::audioA(pMaster);             // VR10 A500K Master Volume
        const float treble = 0.62f + 0.38f * ac30pot::audioA(pTreble); // VR3 A1M Treble, brightened baseline so the default voicing matches the bright amp-only reference (was audioA alone = too dark by default)
        const float bass   = ac30pot::audioA(pBass);               // VR4 A1M Bass
        const float cut    = ac30pot::toneCutBleed(pCut);          // used by fallback cab voicing only
        const float tbSourceOhms = 47000.0f + ac30pot::wiperSourceOhms(ac30pot::kVr2TbVol, vol);
        const float normalSourceOhms = 47000.0f + ac30pot::wiperSourceOhms(ac30pot::kVr1NormalVol, nvol);
        const float masterSourceOhms = 10000.0f + ac30pot::wiperSourceOhms(ac30pot::kVr10Master, master);
        normalElectrical = 0.020f + 0.980f * nvol;
        topElectrical = 0.020f + 0.980f * vol;
        const float nGate = clamp01((1.0f - pInput) * 2.0f);
        const float tbGate = clamp01(pInput * 2.0f);
        // The channel Volume controls must increase drive monotonically. The
        // previous Ruby fit subtracted a high-knob correction from the circuit
        // drive itself, so the PI/power drive peaked near 75% and then fell at
        // 100%. Keep circuit excitation monotonic; loudness correction belongs
        // only in the post-circuit makeup table below.
        const float activePanelVol = clamp01(nGate * pNVol + tbGate * pTBVol);
        const float driveVol = 0.10f + 0.27f * std::sqrt(activePanelVol);
        masterElectrical = master;
        // Ruby references use Master 0.60. The earlier circuit fit used 0.72,
        // leaving the PI/EL84 chain too clean at the actual reference setting.
        // Keep the physical A500K loading above, but map the audible drive
        // coordinate so 0.60 reaches the measured PI/power excitation.
        masterDriveElectrical = std::pow(pMaster, 1.75f);
        normalMiller.set(sr, 56000.0f, 60.0f, 9.0f);
        topMiller.set(sr, 56000.0f, 52.0f, 8.0f);
        normalVolumeCouple.set(sr, 1000000.0f, 47.0e-9f, normalSourceOhms, 0.18f, 0.36f, 1.20f);
        // C9 470 pF is the series plate-to-Top-Boost-volume coupling shown in
        // the preamp schematic. It is the source of the Brilliant channel's
        // deliberate bass cut; C15/C82 belong to its bypass/loading network.
        topVolumeCouple.set(sr, 1000000.0f, 470.0e-12f, tbSourceOhms, 0.16f, 0.40f, 1.20f);
        coupleToPi.set(sr, 1000000.0f, 47.0e-9f, masterSourceOhms, 0.18f, 0.45f, 1.20f);
        topFollower.set(sr, pTBVol);
        // Vox AC30C2 Top Boost tone stack, from Vox_ac30c2.pdf:
        // VR3 A1M, VR4 A1M, R47 10K fixed mid/ground resistor, R19 100K slope,
        // C23 56pF, C28/C38 22nF. Bright cap and Cut are separate real networks.
        const float brightCapHz = 1.0f / (2.0f * kPi * std::fmax(22000.0f, tbSourceOhms) * 120.0e-12f);
        bright.highShelf(sr, std::fmax(2600.0f, std::fmin(11000.0f, brightCapHz)),
                         4.0f + 6.0f * pBright * (1.0f - 0.65f * vol)); // C9/C82 base brilliance + optional extra bright amount
        // The aligned Ruby renders retain more unloaded Top Boost voltage above
        // 4 kHz, especially at low Volume. These shelves represent the measured
        // direct-output/OT response and are disabled on the Normal input.
        const float topWeight = tbGate;
        rubyChime.highShelf(sr, 3800.0f, topWeight * (1.2f + 2.3f * (1.0f - pTBVol)));
        rubyAir.highShelf(sr, 8500.0f, topWeight * (5.0f + 1.0f * (1.0f - pTBVol)));
        tonestack.setComponents(ac30pot::kVr3Treble, ac30pot::kVr4Bass, 10.0e3, 100.0e3,
                                56.0e-12, 22.0e-9, 22.0e-9);
        // ToneStackYeh order is (treble, middle-node, low-node). In the Vox
        // network the real Bass pot is the active middle/body node; the low
        // leg is fixed because the AC30 has no third tone control.
        tonestack.update(sr, treble, bass, 0.5f);
        toneCut.set(sr, pCut);
        // GZ34 supply + long-tail-pair PI + EL84 class-A push-pull. The PI now clips
        // and unbalances before the EL84s; B+ droops through power/screen/preamp nodes.
        supply.setGZ34Ac30(sr, driveVol);
        const float driven = activePanelVol * activePanelVol;
        const float cranked = driven * driven;
        const float breakupShoulder = driven * (1.0f - activePanelVol);
        // Split the extra excitation between a broad upper-half rise and the
        // final cranked region.  The total at 10 stays unchanged, while noon
        // now reaches Ruby's measured breakup instead of remaining nearly
        // identical to the clean waveform.
        phaseInverter.setVoxAc30(sr, 1.25f + 2.30f * driveVol
                                 + 1.20f * driven + 3.50f * cranked
                                 + 5.5f * breakupShoulder,
                                 0.92f, 0.075f);
        power.set(sr, 3.8f + 10.3f * driveVol + 1.5f * masterDriveElectrical
                       + 2.5f * driven + 6.2f * cranked
                       + 11.0f * breakupShoulder,
                       -7.5f, 0.30f, 38.0f, 19000.0f);
        power.out   = 0.0085f;                                     // scale plate-volt differential to signal
        outLevel = 0.72f;
        // Output transformer is always part of the amp. The speaker block is a
        // bypassable fallback for auditioning the VST without an external cabinet/IR.
        outputTransformer.set(sr, activePanelVol);
        fallbackSpeaker.set(sr, treble, cut, driveVol);
        lfoInc = (3.0f + 8.0f * pSpeed) / sr;                      // tremolo 3..11 Hz
    }

    inline float process(float x){
        const rbtube::SupplyScales bplus = supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        const float input = inputCoupling.process(x * inGain);
        const float nGate = clamp01((1.0f - pInput) * 2.0f);
        const float tbGate = clamp01(pInput * 2.0f);

        float normal = normalV1.process(normalMiller.process(input) * 1.55f * bplus.preamp);
        const float normalDrive = 0.50f + 3.0f *
            (0.35f * std::sqrt(pNVol) + 0.65f * normalElectrical)
            - 0.50f * pNVol * pNVol
            + 0.85f * (4.0f * pNVol * (1.0f - pNVol));
        normal = normalVolumeCouple.process(normal, normalDrive);

        float top = topV1.process(topMiller.process(bright.process(input)) * 2.40f * bplus.preamp);
        // The A500K wiper voltage is retained in topElectrical/source loading.
        // This downstream grid-drive calibration represents the fixed V1 plate
        // gain and cathode-follower transfer. It rises monotonically: Ruby is
        // already near breakup by noon, then adds density through the last half.
        const float topGridDrive = 2.0f + 5.8f * std::sqrt(pTBVol);
        top = topVolumeCouple.process(top, topGridDrive);
        top = topFollower.process(top);
        top = tonestack.process(top);
        top = rubyChime.process(top);
        top = rubyAir.process(top);
        // U1B is a clean recovery stage after the lossy stack. Keep its rails
        // broad enough that the 12AX7/PI/EL84 stages create the distortion.
        // U1B recovery: R40 820k / R37 470k gives approximately 2.7x closed-
        // loop gain. Keep broad op-amp rails so it recovers stack loss without
        // becoming another guitar-clipping stage.
        top = 2.8f * std::tanh((2.70f * top) * (1.0f / 2.8f));

        x = nGate * normal + tbGate * top;
        x = coupleToPi.process(x, 0.42f + 1.75f * masterDriveElectrical);
        lastPreampLoad = std::fabs(x) * (0.18f + 0.34f * (nGate * pNVol + tbGate * pTBVol));
        x = phaseInverter.process(x * bplus.screen);
        // C80/VR9 bridge the two PI plates before the EL84 grids. Filtering at
        // this point lets Tone Cut alter power-stage excitation as it does in
        // the real amp, rather than acting as a post-power equalizer.
        x = toneCut.process(x);
        lastScreenLoad = std::fabs(x) * (0.35f + 0.60f * masterElectrical);
        x = power.process(x * masterElectrical * bplus.power * bplus.screen);
        lastPowerLoad = std::fabs(x) * (0.55f + 0.95f * masterElectrical);
        x = outputTransformer.process(x);
        const float cab = fallbackSpeaker.process(x);
        x += pCabSim * (0.65f * cab - x);
        if (pDepth > 0.0f){                                        // tremolo (amplitude)
            lfoPhase += lfoInc; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
            float lfo = 0.5f * (1.0f + std::sin(2.0f * kPi * lfoPhase));
            x *= 1.0f - 0.9f * pDepth * lfo;
        }
        // Loudness calibration is deliberately returned separately by
        // outputMakeup().  Applying it here would change how hard the wrapper's
        // final nonlinear protection is driven, so a level correction would
        // also change breakup.  This function returns circuit output only.
        return x * outLevel;
    }
};

} // namespace boxdc30
#endif // BOX_DC30_CORE_H
