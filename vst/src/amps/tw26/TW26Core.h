#ifndef TW26_CORE_H
#define TW26_CORE_H
//
// TW26Core - BENDER DELUXE / Fender '57 Deluxe (5E3 tweed), REBUILT circuit-real
// (same method as BOX AC30 / see REAL_TUBE_AMP_GUIDE.md). Real cathode-biased tube
// stages with pure Koren transfer tables + the physical cathode auto-bias loop:
//
//   in -> V1A/V1B 12AY7 instrument+mic channels -> interactive volume/tone
//   network -> V2A 12AX7 recovery -> V2B cathodyne PI -> 2x 6V6 push-pull
//   power (cathode-biased, NO global NFB, heavy 5Y3 tube-rectifier sag) ->
//   1x12 tweed speaker.
//
// The 5E3 VOLUME *is* the gain (it dirties as you turn up); the single Tone is the
// only real EQ; jumpering the Mic channel fills body/mids. Bright/Bass/Presence have
// no 5E3 pot -> they are voicing shelves driven by the game transform. Tubes use OUR
// Koren tables (public model), not Guitarix GPL code.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace tw26 {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// RBJ biquad (peaking / shelves / low-pass), denormal-flushed.
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

struct TW26Core {
    float sr = 48000.0f;
    rbtube::HP1 inputCoupling;
    rbtube::TubeStageAY7 instV1, micV1; // 12AY7 channel triodes (warm, low-mu)
    rbtube::TubeStage    v2;            // V2A 12AX7 recovery/gain
    rbtube::Miller12AY7 instMiller, micMiller;
    rbtube::Miller12AX7 millerV2;       // Volume/Tone source -> 12AX7 Miller load
    rbtube::CouplingCapGridLeak coupleToV2, coupleToPi;
    rbtube::PhaseInverterCathodyne12AX7 phaseInverter; // V2B split-load PI
    rbtube::MultiNodeBPlus supply;      // 5Y3 + 16uF nodes + 4k7/22k droppers
    rbtube::PowerAmp6V6  power;         // 2x 6V6 push-pull, cathode-biased, no NFB
    Biquad brightSh, bassSh, spkBody, spkRoll;
    rbtube::TweedTone tweedTone;        // real 5E3 single Tone control (R10/C4/C5 circuit)
    // params (0..1), interface identical to the old TW26Core
    float pTone=0.6f, pInst=0.45f, pMic=0.0f, pBright=1.0f, pBass=0.5f, pPres=0.5f, pCabSim=1.0f;
    float inScale=1, preGain=1, gainOut=1, outLevel=1;
    float instPot=0, micPot=0, tonePot=0;
    float lastPowerLoad=0, lastScreenLoad=0, lastPreampLoad=0;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setTone(float v){ pTone=clamp01(v); recalc(); }
    void setInstVol(float v){ pInst=clamp01(v); recalc(); }
    void setMicVol(float v){ pMic=clamp01(v); recalc(); }
    void setBright(float v){ pBright=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setCabSim(float v){ pCabSim=clamp01(v); }
    void reset(){ inputCoupling.reset(); instMiller.reset(); micMiller.reset(); instV1.reset(); micV1.reset(); v2.reset();
        millerV2.reset(); coupleToV2.reset(); coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        brightSh.reset(); bassSh.reset(); tweedTone.reset(); spkBody.reset(); spkRoll.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f; }

    void recalc(){
        inputCoupling.set(sr, 12.0f);
        // Real cathode-biased stages (self-bias solved). Fender '57 Deluxe schematic:
        // V1 12AY7 shared 820R/25uF cathode, 100k plates; V2A 12AX7 1k5/25uF.
        instV1.set(sr, 0, 250.0f, 40.0f, 7.8f, 820.0f);
        micV1.set(sr,  0, 250.0f, 40.0f, 7.8f, 820.0f);
        v2.set(sr,    1, 250.0f, 40.0f, 4.2f, 1500.0f);
        instMiller.set(sr, 68000.0f, 24.0f, 8.0f);
        micMiller.set(sr,  68000.0f, 24.0f, 8.0f);
        millerV2.set(sr,  180000.0f, 52.0f, 8.0f);

        // 1M audio volume controls and single 1M tone control. The electric taper is
        // real; the drive constants are calibrated so breakup still arrives around
        // the 5E3's real knob range instead of becoming a sterile linear sweep.
        instPot = rbtube::PotTaper::audio(pInst, 1.28f);
        micPot  = rbtube::PotTaper::audio(pMic,  1.28f);
        tonePot = rbtube::PotTaper::audio(pTone, 1.18f);
        inScale = 3.25f;
        preGain = 0.75f + 11.0f * instPot + 5.4f * micPot;
        gainOut = 0.82f + 1.65f * instPot + 0.45f * micPot;

        // 0.1uF coupling caps into ~1M grid leaks in the 5E3 preamp path; 0.022uF
        // into the cathodyne grid. Positive-grid drive now charges/recover caps
        // instead of passing through ideal HPFs.
        coupleToV2.set(sr, 1000000.0f, 100.0e-9f, 68000.0f, 0.11f, 0.58f, 1.65f);
        coupleToPi.set(sr, 1000000.0f, 22.0e-9f, 220000.0f, 0.13f, 0.52f, 1.45f);
        phaseInverter.set(sr, 0.95f + 2.85f * instPot + 0.70f * micPot,
                          0.92f, 250.0f, 56000.0f, 56000.0f, 2.3f, 0.0f);

        // 5Y3 supply: 16uF reservoir/screen/preamp nodes, 4k7 and 22k droppers.
        supply.set(sr, 420.0f, 16.0f, 4700.0f, 16.0f, 22000.0f, 16.0f,
                   0.34f + 0.12f * instPot, 0.24f + 0.08f * instPot,
                   0.13f + 0.04f * instPot, 0.26f);
        // single tweed Tone control (one knob: dark<->bright tilt) + game Bright/Bass shelves
        tweedTone.update(sr, tonePot);                             // real 5E3 Tone circuit
        brightSh.highShelf(sr, 2500.0f, 6.0f * pBright);          // Bright input (cap)
        bassSh.lowShelf(sr, 180.0f, -3.0f + 8.0f * pBass);        // hidden Bass shelf
        // 2x 6V6 push-pull, cathode-biased, NO NFB, heavy 5Y3 sag (big bloom)
        power.set(sr, 2.0f + 15.5f * instPot + 4.0f * micPot, -13.0f, 0.42f, 85.0f, 9000.0f);
        power.out = 0.009f;
        power.biasShift = 3.0f;
        outLevel = 0.52f * (1.0f - 0.28f * instPot);               // level comp across the volume range
        // 1x12 tweed speaker (mild, pre-cab) + Presence top lift
        spkBody.peaking(sr, 130.0f, 0.8f, 2.0f);
        spkRoll.highShelf(sr, 3500.0f, 2.0f + 6.0f * pPres);      // Presence
    }

    inline float process(float x){
        const rbtube::SupplyScales bplus = supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        x = inputCoupling.process(x);
        const float instIn = brightSh.process(x);
        const float micIn = bassSh.process(x * 0.92f);
        float inst = instV1.process(instMiller.process(instIn) * inScale * bplus.preamp);
        float mic = micV1.process(micMiller.process(micIn) * (inScale * 0.92f) * bplus.preamp);
        x = inst * (0.10f + 2.35f * instPot) + mic * (0.03f + 1.65f * micPot);
        x = tweedTone.process(x);                                 // real 5E3 Tone after V1
        x = bassSh.process(x);
        x = v2.process(millerV2.process(coupleToV2.process(x, preGain)) * bplus.preamp);
        x = coupleToPi.process(x, gainOut);
        lastPreampLoad = std::fabs(x) * (0.22f + 0.70f * instPot);
        x = phaseInverter.process(x * bplus.screen);
        lastScreenLoad = std::fabs(x) * (0.35f + 0.65f * instPot);
        x = power.process(x * bplus.power * bplus.screen);        // 6V6 push-pull
        lastPowerLoad = std::fabs(x) * (0.50f + 0.80f * instPot);
        const float ampOnly = x;
        const float cab = spkRoll.process(spkBody.process(ampOnly)); // 1x12 voicing
        x = ampOnly + pCabSim * (cab - ampOnly);
        // loudness flattening vs the Volume/gain: a CLEAN post-output makeup (fit to
        // hold ~constant RMS across the InstVol sweep — the tweed Volume IS the gain,
        // so without this it swings ~24 dB). Anchored ~0 dB at Vol 0.5; applied after
        // all distortion so it's pure volume (no tone/crest change).
        float gcDb = 22.574f - 56.269f * pInst + 22.852f * pInst * pInst;
        if (gcDb > 16.0f) gcDb = 16.0f; else if (gcDb < -12.0f) gcDb = -12.0f;
        return x * outLevel * std::pow(10.0f, 0.05f * gcDb);
    }
};

} // namespace tw26
#endif // TW26_CORE_H
