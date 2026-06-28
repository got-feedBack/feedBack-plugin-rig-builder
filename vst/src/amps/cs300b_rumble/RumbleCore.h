#ifndef RUMBLE_CORE_H
#define RUMBLE_CORE_H
//
// RumbleCore — Fender Rumble Bass (1995) DUAL-CHANNEL all-tube bass head, circuit-
// real on the shared tube_stage.hpp framework. Schematic: Fender drawings 048406
// (preamp) + 048411 (power amp), Rev C/D.
//
//   IN -> input coupling (big bass caps -> very low corner, keeps the lows)
//      -> CH A: V6A 12AX7 -> V6B 12AX7 -> TMB(A) -> Mid-Cut(A) -> ·A Volume   (vintage)
//      -> CH B: V1A 12AX7 -> V1B 12AX7 -> TMB(B) -> Mid-Cut(B) -> ·B Volume   (hotter)
//      -> MIX blend (0=A, 1=B) -> V3 12AT7 recovery/driver
//      -> 12AT7 long-tail-pair phase inverter (Fender AB763 topology)
//      -> 6x6550 push-pull power (very stiff supply, low sag, big clean headroom)
//      -> output-transformer voicing -> out
//
// Both channels carry the real passive Fender TMB (Treble/Bass 250k, Mid 25k; treble
// cap 150pF, bass/mid caps .047uF, slope 47k) and a MID switch (NORM/CUT scoop). The
// six 6550 grids are driven via two 12BH7 cathode-follower drivers in the real amp
// (~unity current buffers); they are folded into the PI->power drive here. Values
// from the 1995 service drawings; runs oversampled (RumblePlugin wraps it 2x).
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace rumble {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// RBJ peaking biquad (denormal-flushed) — the Mid-Cut scoop.
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void bypass(){ b0=1;b1=b2=a1=a2=0; }
    void peaking(float sr,float f,float Q,float dB){ if(f>sr*0.49f)f=sr*0.49f;
        float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*Q),a0=1+al/A;
        b0=(1+al*A)/a0; b1=(-2*c)/a0; b2=(1-al*A)/a0; a1=(-2*c)/a0; a2=(1-al/A)/a0; }
};

// One preamp channel: 2x 12AX7 gain -> Fender TMB -> Mid-Cut switch.
struct Channel {
    rbtube::TubeStage s1, s2;       // 12AX7 input + 2nd gain
    rbtube::ToneStackYeh tone;      // Fender TMB
    Biquad midCut;
    float drive=1.f; bool cut=false;
    void config(float sr, float fck1, float fck2){
        s1.set(sr, 1, 254.0f, 40.0f, fck1, 1500.0f);
        s2.set(sr, 1, 254.0f, 40.0f, fck2, 1500.0f);
        tone.setComponents(250.0e3, 250.0e3, 25.0e3, 47.0e3, 150.0e-12, 47.0e-9, 47.0e-9);
    }
    void setTone(float sr, float tre, float mid, float bass){ tone.update(sr, tre, mid, bass); }
    void setMidCut(float sr, bool on){ cut=on; if(on) midCut.peaking(sr,450.0f,0.7f,-8.0f); else midCut.bypass(); }
    void reset(){ s1.reset(); s2.reset(); tone.reset(); midCut.reset(); }
    inline float process(float x){
        x = s1.process(x * drive);
        x = s2.process(x);
        x = tone.process(x);
        if (cut) x = midCut.process(x);
        return x;
    }
};

struct RumbleCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;              // input coupling cap + grid leak (big -> deep lows)
    Channel chA, chB;                   // dual preamp channels (A vintage / B hotter)
    rbtube::TubeStage v3;               // shared 12AT7 recovery/driver after the MIX
    rbtube::PhaseInverterLTP12AT7 pi;   // 12AT7 long-tail-pair phase inverter
    rbtube::PowerAmp6550 power;         // 6x6550 push-pull (stiff supply, low sag)
    rbtube::LP1 otVoice;                // gentle output-transformer top roll (pre-cab)

    // params (0..1)
    float pAVol=0.5f, pATre=0.5f, pABass=0.5f, pAMid=0.5f;
    float pBVol=0.5f, pBTre=0.5f, pBBass=0.5f, pBMid=0.5f;
    bool  pAMidCut=false, pBMidCut=false;
    float pMix=0.5f;
    float inScale=0.9f, piDrive=6.0f, outLevel=1.f, gA=1.f, gB=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setAVol(float v){ pAVol=clamp01(v); recalc(); }
    void setATreble(float v){ pATre=clamp01(v); recalc(); }
    void setABass(float v){ pABass=clamp01(v); recalc(); }
    void setAMiddle(float v){ pAMid=clamp01(v); recalc(); }
    void setAMidCut(bool b){ pAMidCut=b; recalc(); }
    void setBVol(float v){ pBVol=clamp01(v); recalc(); }
    void setBTreble(float v){ pBTre=clamp01(v); recalc(); }
    void setBBass(float v){ pBBass=clamp01(v); recalc(); }
    void setBMiddle(float v){ pBMid=clamp01(v); recalc(); }
    void setBMidCut(bool b){ pBMidCut=b; recalc(); }
    void setMix(float v){ pMix=clamp01(v); recalc(); }

    void reset(){ inCoupling.reset(); chA.reset(); chB.reset(); v3.reset();
        pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, 5.0f);                       // deep lows (bass amp, no guitar HP)
        // Channel A = vintage (looser, slightly less gain); B = a touch tighter/hotter.
        chA.config(sr, 16.0f, 26.0f);
        chB.config(sr, 20.0f, 34.0f);
        chA.setTone(sr, pATre, pAMid, pABass); chA.setMidCut(sr, pAMidCut);
        chB.setTone(sr, pBTre, pBMid, pBBass); chB.setMidCut(sr, pBMidCut);

        // Channel Volumes = the drive into each channel (Fender non-master Volume,
        // audio taper). B is voiced ~1.4x hotter than A.
        chA.drive = 0.30f + 2.6f * rbtube::PotTaper::audio(pAVol, 1.30f);
        chB.drive = 0.40f + 3.6f * rbtube::PotTaper::audio(pBVol, 1.30f);

        // MIX blend gains (equal-ish power crossfade between the two channel outs).
        gA = std::sqrt(1.0f - pMix);
        gB = std::sqrt(pMix);

        inScale = 0.9f;                                  // grid volts into the channels
        v3.set(sr, 1, 254.0f, 40.0f, 50.0f, 1500.0f);    // shared recovery/driver
        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);

        // 6x6550 push-pull. Fender bass head -> very stiff supply (LOW sag), fixed
        // bias ~-43.5V, low OT high-pass so the fundamental survives, gentle OT LP.
        power.set(sr, 1.6f, -43.5f, 0.07f, 28.0f, 9000.0f);
        power.out = 0.0055f;
        power.biasShift = 2.0f;
        otVoice.set(sr, 7500.0f);

        // Loudness makeup (gain-dependent on the louder channel). Tuned in
        // calibrate_amp_core.py to the amp-family reference.
        const float vmax = (pAVol > pBVol) ? pAVol : pBVol;
        outLevel = std::pow(10.0f, 0.05f * (14.0f + 11.0f * vmax));
    }

    inline float process(float x){
        x = inCoupling.process(x * inScale);
        const float a = chA.process(x) * gA;
        const float b = chB.process(x) * gB;
        float y = a + b;                                 // MIX blend
        y = v3.process(y);                               // shared recovery/driver
        y = pi.process(y * piDrive);                     // 12AT7 LTP PI
        y = power.process(y);                            // 6x6550 PP
        y = otVoice.process(y);
        return y * outLevel;
    }
};

} // namespace rumble
#endif // RUMBLE_CORE_H
