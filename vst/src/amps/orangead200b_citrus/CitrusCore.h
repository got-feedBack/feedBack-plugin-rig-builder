#ifndef CITRUS_CORE_H
#define CITRUS_CORE_H
//
// CitrusCore — Orange AD200B (Mk III) all-tube bass head, circuit-real on the shared
// tube_stage.hpp framework. "The simplest, purest bass amplifier": a short signal
// path. Topology reference: the Orange Graphic-series schematic (ECC83 preamp + LTP
// PI + 4x power tubes; that older Graphic uses EL34); the AD200B MkIII bass head uses
// 4x 6550 from the factory (~200W, huge clean headroom).
//
//   IN (Passive/Active pad) -> input coupling (deep lows, bass)
//      -> V1 ECC83 (Gain) -> Orange PASSIVE SUBTRACTIVE tone stack
//         (Bass/Middle/Treble: full up = flat, turning down CUTS that band)
//      -> V2 ECC83 driver -> 12AX7 long-tail-pair phase inverter
//      -> 4x 6550 push-pull (real Koren 6550 table; stiff supply = clean, creamy
//         power-tube overdrive only when Master is pushed) -> OT voicing -> out
//
// Replaces the old half-real build (nodal 12AX7 + tanh "pushPull" power, no OS) with
// the framework: real TubeStages, a real LTP PI, the real 6550 power table, + 2x OS
// (in the plugin). Runs oversampled (CitrusPlugin wraps it 2x).
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace citrus {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// RBJ biquad (shelves/peaking), denormal-flushed via rbtube::dn.
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void lowShelf(float sr,float f,float dB){ float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/2*std::sqrt((A+1/A)+2),rA=std::sqrt(A);
        float a0=(A+1)+(A-1)*c+2*rA*al; b0=A*((A+1)-(A-1)*c+2*rA*al)/a0; b1=2*A*((A-1)-(A+1)*c)/a0; b2=A*((A+1)-(A-1)*c-2*rA*al)/a0;
        a1=-2*((A-1)+(A+1)*c)/a0; a2=((A+1)+(A-1)*c-2*rA*al)/a0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/2*std::sqrt((A+1/A)+2),rA=std::sqrt(A);
        float a0=(A+1)-(A-1)*c+2*rA*al; b0=A*((A+1)+(A-1)*c+2*rA*al)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-2*rA*al)/a0;
        a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-2*rA*al)/a0; }
    void peaking(float sr,float f,float Q,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*Q),a0=1+al/A;
        b0=(1+al*A)/a0; b1=(-2*c)/a0; b2=(1-al*A)/a0; a1=(-2*c)/a0; a2=(1-al/A)/a0; }
};

struct CitrusCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;              // input coupling (deep lows, bass)
    rbtube::TubeStage v1, v2;           // ECC83 gain + driver
    Biquad bqBass, bqMid, bqTreble;     // Orange passive subtractive tone stack
    rbtube::PhaseInverterLTP12AX7 pi;   // 12AX7 long-tail-pair phase inverter
    rbtube::PowerAmp6550 power;         // 4x 6550 push-pull (factory AD200B MkIII tube)
    rbtube::LP1 otVoice;               // output-transformer top roll (pre-cab)

    // params (0..1)
    float pGain=0.4f, pBass=1.0f, pMid=1.0f, pTreble=1.0f, pMaster=0.7f;
    bool  pActive=false;
    float inScale=1.f, gain=1.f, piDrive=6.f, outLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setGain(float v){ pGain=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setMaster(float v){ pMaster=clamp01(v); recalc(); }
    void setActive(bool b){ pActive=b; recalc(); }

    void reset(){ inCoupling.reset(); v1.reset(); v2.reset();
        bqBass.reset(); bqMid.reset(); bqTreble.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, 4.0f);                        // deep lows (bass amp)
        // Orange ~stiff/high B+ ECC83 stages.
        v1.set(sr, 1, 320.0f, 40.0f, 16.0f, 1500.0f);    // V1 input/Gain
        v2.set(sr, 1, 320.0f, 40.0f, 40.0f, 1500.0f);    // V2 driver

        // Gain (12AX7 input drive). Active jack pads hot active basses.
        const float pad  = pActive ? 0.55f : 1.0f;
        const float gAud = rbtube::PotTaper::audio(pGain, 1.30f);
        inScale = pad;
        gain    = 0.6f + 3.4f * gAud;                    // V1 drive (clean low, grinds cranked)
        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);

        // Orange PASSIVE SUBTRACTIVE tone stack: dB = (knob-1)*range -> full up (1.0)
        // = 0 dB (factory flat), turning down only CUTS.
        bqBass.lowShelf(sr, 110.0f, (pBass - 1.0f) * 20.0f);
        bqMid.peaking(sr, 550.0f, 0.9f, (pMid - 1.0f) * 10.0f);   // 1k mid pot -> subtle
        bqTreble.highShelf(sr, 3500.0f, (pTreble - 1.0f) * 20.0f);

        // 4x 6550 push-pull (~200W). Very stiff supply (LOW sag), big clean headroom,
        // creamy power overdrive when Master is pushed. Low OT HP keeps the bass.
        const float master = rbtube::PotTaper::audio(pMaster, 1.15f);
        power.set(sr, 0.5f + 2.4f*master, -43.5f, 0.06f, 26.0f, 9000.0f);
        power.out = 0.0052f;
        power.biasShift = 1.9f;
        otVoice.set(sr, 9000.0f);

        // Loudness makeup — gain-dependent (designed from the harness sweep; tuned in
        // calibrate_amp_core.py).
        outLevel = std::pow(10.0f, 0.05f * (14.0f + 11.0f * pGain));
    }

    inline float process(float x){
        x = inCoupling.process(x * inScale);
        x = v1.process(x * gain);                        // V1 + Gain
        x = bqBass.process(x); x = bqMid.process(x); x = bqTreble.process(x);  // subtractive stack
        x = v2.process(x);                               // V2 driver
        x = pi.process(x * piDrive);                     // 12AX7 LTP PI
        x = power.process(x);                            // 4x KT88 PP
        x = otVoice.process(x);
        return x * outLevel;
    }
};

} // namespace citrus
#endif // CITRUS_CORE_H
