#ifndef BOX_DC30_CORE_H
#define BOX_DC30_CORE_H
//
// BoxDC30Core - Vox AC30 Top Boost (parody "BOX AC30"), REBUILT Guitarix-style:
// the signal flow is a clean feed-forward cascade of Guitarix-style tube stages
// (anti-alias -> Koren tube table -> per-stage DC-block), so it is stable at any
// (oversampled) rate -- unlike the old white-box EN30Core which blew up at 192k.
// Component-level voicing (AC30 Top Boost stack, Cut, bright, EL84 push-pull power
// + sag, output transformer) sits BETWEEN the tube stages as ordinary stable blocks.
// Tubes use OUR Koren tables (public model). Drop-in param interface == EN30Core.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace boxdc30 {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

static inline float gainLoudnessMakeupDb(float gain)
{
    // Measured against tools/measure_amp_loudness.py after the AC30C2 A500K pot
    // taper and 0.45x preGain calibration.  RS Gain is distortion amount in
    // Slopsmith, not output volume, so this post-circuit makeup keeps clean,
    // edge-of-breakup and cranked settings close in perceived loudness.  The
    // smooth high-gain trim compensates the extra guitar-density loudness that
    // appears when the Top Boost path starts compressing hard.
    static const float kDb[11] = {
        31.0f, 23.0f, 13.8f, 6.65f, 1.25f, -2.50f,
        -4.70f, -5.40f, -5.35f, -5.00f, -4.45f
    };
    const float p = 10.0f * clamp01(gain);
    int i = (int)p;
    if (i >= 10)
        i = 10;
    const float f = p - (float)i;
    const float makeup = (i >= 10) ? kDb[10] : kDb[i] + (kDb[i + 1] - kDb[i]) * f;
    const float density = clamp01((clamp01(gain) - 0.32f) / 0.68f);
    const float smooth = density * density * (3.0f - 2.0f * density);
    return makeup - 3.2f * smooth;
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
    float coreDrive = 1.0f;
    float coreMix = 0.12f;

    void set(float sr, float hot) {
        otHp.set(sr, 38.0f);                                      // finite OT primary inductance
        otLeakLp.set(sr, 22000.0f);                               // OT leakage/iron loss only, no speaker rolloff
        fluxLp.set(sr, 24.0f);                                    // low-frequency core flux memory
        coreDrive = 1.0f + 0.10f * hot;
        coreMix = 0.10f + 0.05f * hot;
    }

    inline float process(float x) {
        float y = otHp.process(x);
        const float flux = fluxLp.process(y);
        const float core = std::tanh((y - 0.16f * flux) * coreDrive);
        y = (1.0f - coreMix) * y + coreMix * core;                 // soft OT core bend, not a hard limiter
        return otLeakLp.process(y);
    }

    void reset() {
        otHp.reset(); otLeakLp.reset(); fluxLp.reset();
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

struct BoxDC30Core {
    float sr = 48000.0f;
    rbtube::HP1 inputCoupling;
    rbtube::TubeStage v1, v2, v3;       // three real cathode-biased 12AX7 stages
    rbtube::Miller12AX7 inputMiller, millerV2, millerV3; // 12AX7 Cgp/Cgk Miller loading
    rbtube::CouplingCapGridLeak coupleToV2, coupleToV3, coupleToPi;
    rbtube::PhaseInverterLTP12AX7 phaseInverter; // AC30 long-tail-pair 12AX7 splitter
    rbtube::MultiNodeBPlus supply;         // GZ34 + power/screen/preamp filter nodes
    rbtube::PowerAmpPP power;            // real class-A push-pull EL84 (no NFB, AC30)
    rbtube::ToneStackYeh tonestack;     // real Vox Top Boost R/C tone network (Yeh model)
    Biquad bright, cutLP;
    Ac30OutputTransformer outputTransformer;
    Ac30FallbackSpeaker fallbackSpeaker;
    // params (0..1), interface identical to EN30Core
    float pInput=0.5f, pNVol=0.7f, pTBVol=0.7f, pTreble=0.5f, pBass=0.5f, pBright=0.5f,
          pCut=0.5f, pMaster=0.7f, pRevTone=0.5f, pRevLevel=0.0f, pSpeed=0.5f, pDepth=0.0f,
          pCabSim=1.0f;
    float inGain=1, masterDrive=1, outLevel=1, lfoPhase=0, lfoInc=0;
    float inScale=4, preGain=1, gainOut=1;     // audio->grid-volts + inter-stage pre-gains
    float masterElectrical=0.0f;               // VR10 A500K electrical wiper position after A taper
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
    void reset(){ inputCoupling.reset(); inputMiller.reset(); v1.reset(); v2.reset(); v3.reset(); millerV2.reset(); millerV3.reset();
        coupleToV2.reset(); coupleToV3.reset(); coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        tonestack.reset(); bright.reset(); cutLP.reset();
        outputTransformer.reset(); fallbackSpeaker.reset(); lfoPhase=0;
        lastPowerLoad=lastScreenLoad=lastPreampLoad=0; }

    void recalc(){
        inputCoupling.set(sr, 12.0f);                              // input grid-leak coupling
        // THREE real cathode-biased 12AX7 stages, AC30 Top Boost values (same Ri/Rk/
        // fck Guitarix uses for the 12ax7): V1 grid-leak 68k Rk2.7k, V2/V3 250k with
        // Rk 1.5k/820. Each self-biases (Vk0 solved) and saturates on its own load
        // line; the cathode loop makes it breathe. The front-panel pots now use the
        // actual AC30C2 schematic values/tapers instead of calibrated linear-ish
        // knob curves.
        inGain      = 0.40f + 1.6f * pInput;                       // input drive
        v1.set(sr, 0, 250.0f, 40.0f, 86.0f,  2700.0f);             // V1 (68k grid-leak)
        v2.set(sr, 1, 250.0f, 40.0f, 132.0f, 1500.0f);             // V2 (250k)
        v3.set(sr, 1, 250.0f, 40.0f, 194.0f, 820.0f);              // V3 (250k, hottest)
        const float vol    = ac30pot::audioA(pTBVol);              // VR2 A500K Top Boost Volume
        const float nvol   = ac30pot::audioA(pNVol);               // VR1 A500K Normal Volume
        const float master = ac30pot::audioA(pMaster);             // VR10 A500K Master Volume
        const float treble = 0.62f + 0.38f * ac30pot::audioA(pTreble); // VR3 A1M Treble, brightened baseline so the default voicing matches the bright amp-only reference (was audioA alone = too dark by default)
        const float bass   = ac30pot::audioA(pBass);               // VR4 A1M Bass
        const float cut    = ac30pot::toneCutBleed(pCut);          // VR9 B220K + C80 4n7 Tone Cut
        const float tbSourceOhms = 47000.0f + ac30pot::wiperSourceOhms(ac30pot::kVr2TbVol, vol);
        const float masterSourceOhms = 10000.0f + ac30pot::wiperSourceOhms(ac30pot::kVr10Master, master);
        const float driveVol = 0.65f * std::sqrt(vol) + 0.35f * vol;
        masterElectrical = master;
        inScale     = 2.0f * (0.7f + 0.6f * nvol);                 // audio -> grid volts into V1 (keep V1 cleaner)
        // The A500K pot gives the real voltage divider and source impedance, but the
        // downstream 12AX7/PI model still needs enough volts to match a cranked AC30.
        // driveVol is only that calibration term: it keeps low settings clean while
        // moving breakup from 3 o'clock down to the real AC30 noon/1-o'clock region.
        preGain     = 0.45f * (0.35f + 7.8f * driveVol); // 0.45x: raise the breakup threshold for the calibrated -12 dBFS input (was distorting too early at low/mid TB Vol)
        gainOut     = 0.60f + 0.70f * driveVol;                    // post-V3 level into the PI/power amp
        // 12AX7 Miller loading. Guitarix uses fixed ~6.5 kHz low-pass sections around
        // its tubestages; here the cutoff comes from Cgk + Cgp*(1+Av) and the driving
        // resistance. That keeps the Top Boost chime but makes the HF loss tube/circuit
        // derived instead of an arbitrary inter-stage EQ.
        inputMiller.set(sr,  68000.0f, 55.0f, 10.0f);              // input grid stopper + V1 Miller, ~22 kHz
        millerV2.set(sr, tbSourceOhms, 52.0f, 8.0f);              // VR2 A500K wiper source impedance into V2
        millerV3.set(sr, 180000.0f, 58.0f, 5.0f);                 // V2/coupling source impedance into V3
        // Coupling caps + grid-leak charging. These are the AC30 blocking-distortion
        // points: when V2/V3/PI grids conduct, the coupling cap charges and the next
        // stage recovers through its grid leak instead of staying as an ideal HPF.
        coupleToV2.set(sr, 1000000.0f, 22.0e-9f, 220000.0f, 0.16f, 0.54f, 1.35f);
        coupleToV3.set(sr, 1000000.0f, 22.0e-9f, 180000.0f, 0.14f, 0.62f, 1.70f);
        coupleToPi.set(sr, 1000000.0f, 47.0e-9f, masterSourceOhms, 0.18f, 0.45f, 1.20f);
        // Vox AC30C2 Top Boost tone stack, from Vox_ac30c2.pdf:
        // VR3 A1M, VR4 A1M, R47 10K fixed mid/ground resistor, R19 100K slope,
        // C23 56pF, C28/C38 22nF. Bright cap and Cut are separate real networks.
        const float brightCapHz = 1.0f / (2.0f * kPi * std::fmax(22000.0f, tbSourceOhms) * 120.0e-12f);
        bright.highShelf(sr, std::fmax(2600.0f, std::fmin(11000.0f, brightCapHz)),
                         8.0f * pBright * (1.0f - 0.65f * vol));  // TB bright cap, strongest at lower VR2 settings
        tonestack.setComponents(ac30pot::kVr3Treble, ac30pot::kVr4Bass, 10.0e3, 100.0e3,
                                56.0e-12, 22.0e-9, 22.0e-9);
        tonestack.update(sr, treble, 1.0f, bass);
        cutLP.lowpass(sr, 26000.0f - 20000.0f * std::sqrt(cut), 0.7f); // Tone Cut: ~26k(no cut)->6k(full); was 7.6k->0.9k (parked at 6.5k = dark)
        // GZ34 supply + long-tail-pair PI + EL84 class-A push-pull. The PI now clips
        // and unbalances before the EL84s; B+ droops through power/screen/preamp nodes.
        supply.setGZ34Ac30(sr, driveVol);
        phaseInverter.setVoxAc30(sr, 1.15f + 2.55f * master + 0.85f * driveVol, 0.92f, 0.075f);
        power.set(sr, 3.0f + 15.5f * driveVol + 2.5f * master, -7.5f, 0.30f); // cathode-bias sag remains local to EL84s
        power.out   = 0.0085f;                                     // scale plate-volt differential to signal
        masterDrive = 1.0f;
        outLevel    = 0.89f * (1.0f - 0.40f * pTBVol);             // plugin loudness comp follows panel knob, not electrical taper
        // Output transformer is always part of the amp. The speaker block is a
        // bypassable fallback for auditioning the VST without an external cabinet/IR.
        outputTransformer.set(sr, driveVol);
        fallbackSpeaker.set(sr, treble, cut, driveVol);
        lfoInc = (3.0f + 8.0f * pSpeed) / sr;                      // tremolo 3..11 Hz
    }

    inline float process(float x){
        const rbtube::SupplyScales bplus = supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        x = inputCoupling.process(x * inGain);
        x = bright.process(x);
        x = v1.process(inputMiller.process(x) * inScale * bplus.preamp); // audio -> grid volts -> V1
        x = tonestack.process(x);                                  // real Vox tone stack between V1 and V2
        x = v2.process(coupleToV2.process(millerV2.process(x), preGain * bplus.preamp)); // Miller + blocking cap -> V2 grid
        x = v3.process(coupleToV3.process(millerV3.process(x), preGain * bplus.preamp)); // Miller + blocking cap -> V3 grid
        x = coupleToPi.process(x * gainOut, 1.0f);
        lastPreampLoad = std::fabs(x) * (0.20f + 0.40f * pTBVol);
        x = phaseInverter.process(x * bplus.screen);
        lastScreenLoad = std::fabs(x) * (0.35f + 0.60f * masterElectrical);
        x = power.process(x * bplus.power * bplus.screen);
        lastPowerLoad = std::fabs(x) * (0.55f + 0.95f * masterElectrical);
        x = cutLP.process(x);                                      // Cut AFTER power amp (real AC30: tames the output treble, post-distortion)
        x = outputTransformer.process(x);
        const float cab = fallbackSpeaker.process(x);
        x += pCabSim * (0.65f * cab - x);
        if (pDepth > 0.0f){                                        // tremolo (amplitude)
            lfoPhase += lfoInc; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
            float lfo = 0.5f * (1.0f + std::sin(2.0f * kPi * lfoPhase));
            x *= 1.0f - 0.9f * pDepth * lfo;
        }
        // Loudness flattening is plugin-level normalization, not part of the AC30
        // circuit. Keep it tied to panel position; tying it to the A500K electrical
        // taper over-boosts mid knob settings and makes Gain act like a volume pot.
        const float gcDb = gainLoudnessMakeupDb(pTBVol);
        return x * outLevel * std::pow(10.0f, 0.05f * gcDb);
    }
};

} // namespace boxdc30
#endif // BOX_DC30_CORE_H
