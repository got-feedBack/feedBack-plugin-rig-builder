#ifndef TW26_CORE_H
#define TW26_CORE_H
//
// TW26Core - BENDER DELUXE / Fender '57 Deluxe (5E3 tweed), REBUILT circuit-real
// (same method as BOX DC30 / see REAL_TUBE_AMP_GUIDE.md). Real cathode-biased tube
// stages with pure Koren transfer tables + the physical cathode auto-bias loop:
//
//   in -> V1 12AY7 (low-mu ~44, warm, early soft breakup) -> single tweed Tone
//   network -> V2 12AX7 recovery/gain -> 2x 6V6 push-pull power (cathode-biased,
//   NO global NFB, heavy 5Y3 tube-rectifier sag) -> 1x12 tweed speaker.
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
    rbtube::TubeStageAY7 v1;            // 12AY7 first stage (warm, low-mu)
    rbtube::TubeStage    v2;            // 12AX7 recovery/gain
    rbtube::LP1 couple12;              // inter-stage Miller rolloff
    rbtube::PowerAmp6V6  power;         // 2x 6V6 push-pull, cathode-biased, no NFB
    Biquad brightSh, bassSh, spkBody, spkRoll;
    rbtube::TweedTone tweedTone;        // real 5E3 single Tone control (R10/C4/C5 circuit)
    // params (0..1), interface identical to the old TW26Core
    float pTone=0.6f, pInst=0.45f, pMic=0.0f, pBright=1.0f, pBass=0.5f, pPres=0.5f;
    float inScale=1, preGain=1, gainOut=1, outLevel=1;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setTone(float v){ pTone=clamp01(v); recalc(); }
    void setInstVol(float v){ pInst=clamp01(v); recalc(); }
    void setMicVol(float v){ pMic=clamp01(v); recalc(); }
    void setBright(float v){ pBright=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void reset(){ inputCoupling.reset(); v1.reset(); v2.reset(); couple12.reset(); power.reset();
        brightSh.reset(); bassSh.reset(); tweedTone.reset(); spkBody.reset(); spkRoll.reset(); }

    void recalc(){
        inputCoupling.set(sr, 12.0f);
        // Real cathode-biased stages (self-bias solved). V1 12AY7 68k grid (bypassed
        // cathode -> low fck, full gain), V2 12AX7 250k recovery.
        v1.set(sr, 0, 250.0f, 40.0f, 20.0f,  1500.0f);            // 12AY7 first stage
        v2.set(sr, 1, 250.0f, 40.0f, 80.0f,  1500.0f);            // 12AX7 recovery
        couple12.set(sr, 9000.0f);
        // The 5E3 VOLUME is the gain: InstVol drives the cascade; MicVol jumpers in body.
        float vol = std::pow(pInst, 1.9f);                        // steep taper: clean below ~7, cooks hard 7->10
        float mic = pMic;
        inScale = 2.8f * (0.8f + 0.5f * mic);                     // input drive (live-guitar sensitivity) + mic body
        preGain = 0.4f + 4.6f * vol + 0.5f * mic;                 // V1->V2 drive (hot ceiling for cranked tweed grind)
        gainOut = 0.6f + 1.0f * vol;
        // single tweed Tone control (one knob: dark<->bright tilt) + game Bright/Bass shelves
        tweedTone.update(sr, pTone);                              // real 5E3 Tone circuit
        brightSh.highShelf(sr, 2500.0f, 6.0f * pBright);          // Bright input (cap)
        bassSh.lowShelf(sr, 180.0f, -3.0f + 8.0f * pBass);        // hidden Bass shelf
        // 2x 6V6 push-pull, cathode-biased, NO NFB, heavy 5Y3 sag (big bloom)
        power.set(sr, 4.0f + 30.0f * vol, -13.0f, 0.60f, 85.0f, 9000.0f);
        power.out = 0.009f;
        power.biasShift = 3.0f;
        outLevel = 0.5f * (1.0f - 0.40f * vol);                   // level comp across the volume range
        // 1x12 tweed speaker (mild, pre-cab) + Presence top lift
        spkBody.peaking(sr, 130.0f, 0.8f, 2.0f);
        spkRoll.highShelf(sr, 3500.0f, 2.0f + 6.0f * pPres);      // Presence
    }

    inline float process(float x){
        x = inputCoupling.process(x);
        x = brightSh.process(x);
        x = v1.process(x * inScale);                              // 12AY7 first stage
        x = tweedTone.process(x);                                 // real 5E3 Tone after V1
        x = bassSh.process(x);
        x = v2.process(couple12.process(x) * preGain);            // 12AX7 recovery
        x *= gainOut;
        x = power.process(x);                                     // 6V6 push-pull
        x = spkRoll.process(spkBody.process(x));                  // 1x12 voicing
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
