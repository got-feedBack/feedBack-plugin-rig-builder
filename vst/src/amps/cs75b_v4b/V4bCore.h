#ifndef V4B_CORE_H
#define V4B_CORE_H
//
// V4bCore — Ampeg V-4B all-tube bass head, circuit-real (framework tube_stage.hpp).
// Full preamp/driver/PI lineup from the V-4B schematic (12/71-223): V1/V2 12AX7,
// V3 12DW7 (driver), V4 12AU7 phase inverter, V5-V8 4× 7027A power (~100W). 7027A ≈
// 6L6GC → PowerAmp6L6GC. The 12AU7 LTP PI is the V-4B's stiffer/cleaner splitter
// that only hardens when pushed (vs going straight into the power stage).
//
//   IN → input coupling (HP) → ·inScale → V1 12AX7 gain
//      → Ultra Lo contour → Bass / Midrange(LC, 3-pos Frequency) / Treble stack
//      → Ultra Hi presence → ·preGain → V2 12AX7 → V3 12DW7 driver (high-mu section)
//      → ·gainOut → V4 12AU7 long-tail-pair PI → 4×7027A push-pull (sag + OT) → out
//
// 12DW7 = 12AX7 section (modeled, V3 gain) + 12AU7 section (cathode follower, ~unity
// → treated as transparent). The 12AU7 also drives the V4 LTP phase inverter.
// Tone stack is the V-4B passive network (Bass/Treble shelves + LC mid with the
// 3-position Frequency selector 300/900/2500 Hz) — not a TMB, so its own block.
//
// Component values: Ampeg V-4B preamp + power boards. Runs at the oversampled rate
// (plugin wraps it 2×). Calibrated by character (loudness matched to the SVT/GK).
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace v4b {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// 3-position Midrange-selector centre frequencies (Hz): L101 + SW3 caps.
static const float kMidFreqs[3] = { 300.f, 900.f, 2500.f };

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void bypass(){ b0=1;b1=b2=a1=a2=0; }
    void norm(float a0){ b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0; }
    void peaking(float sr,float f,float Q,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=1+al*A;b1=-2*c;b2=1-al*A; float a0=1+al/A;a1=-2*c;a2=1-al/A; norm(a0); }
    void lowShelf(float sr,float f,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)+2); float rA=sqrtf(A);
        b0=A*((A+1)-(A-1)*c+2*rA*al); b1=2*A*((A-1)-(A+1)*c); b2=A*((A+1)-(A-1)*c-2*rA*al);
        float a0=(A+1)+(A-1)*c+2*rA*al; a1=-2*((A-1)+(A+1)*c); a2=(A+1)+(A-1)*c-2*rA*al; norm(a0); }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)+2); float rA=sqrtf(A);
        b0=A*((A+1)+(A-1)*c+2*rA*al); b1=-2*A*((A-1)+(A+1)*c); b2=A*((A+1)+(A-1)*c-2*rA*al);
        float a0=(A+1)-(A-1)*c+2*rA*al; a1=2*((A-1)-(A+1)*c); a2=(A+1)-(A-1)*c-2*rA*al; norm(a0); }
    void lowpass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=(1-c)/2;b1=1-c;b2=(1-c)/2; float a0=1+al;a1=-2*c;a2=1-al; norm(a0); }
};

struct V4bCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v2;            // V1/V2 12AX7 gain
    Biquad ulLow, ulMid, ulHigh;         // Ultra Lo contour
    Biquad uhShelf;                      // Ultra Hi presence
    Biquad bqBass, bqMid, bqTreble;      // passive tone stack
    rbtube::TubeStage v3;                // V3 12DW7 high-mu (12AX7-type) gain/driver section
    rbtube::PhaseInverterV4B phaseInverter;  // V4 12AU7 long-tail-pair phase inverter
    rbtube::PowerAmp6L6GC power;         // 4×7027A push-pull (~100W)
    Biquad otVoice;

    float pGain=0.5f, pBass=0.5f, pMid=0.5f, pFreq=0.5f, pTreble=0.5f, pMaster=0.7f;
    bool  pPad=false, pUltraLo=false, pUltraHi=false;
    float inGain=1.f, inScale=1.f, preGain=1.f, gainOut=1.f, outLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setGain(float v){ pGain=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMidrange(float v){ pMid=clamp01(v); recalc(); }
    void setFreq(float v){ pFreq=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setMaster(float v){ pMaster=clamp01(v); recalc(); }
    void setPad(bool b){ pPad=b; recalc(); }
    void setUltraLo(bool b){ pUltraLo=b; recalc(); }
    void setUltraHi(bool b){ pUltraHi=b; recalc(); }

    void reset(){ inCoupling.reset(); v1.reset(); v2.reset(); v3.reset();
        ulLow.reset(); ulMid.reset(); ulHigh.reset(); uhShelf.reset();
        bqBass.reset(); bqMid.reset(); bqTreble.reset();
        phaseInverter.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, 5.0f);
        v1.set(sr, 0, 250.0f, 40.0f, 4.8f, 1500.0f);     // V1 12AX7 (68k grid-leak)
        v2.set(sr, 1, 250.0f, 40.0f, 4.8f, 1500.0f);     // V2 12AX7 (250k)
        v3.set(sr, 1, 250.0f, 80.0f, 8.0f, 1500.0f);     // V3 12DW7 high-mu section, driver (less gain → higher divider)

        const float pad   = pPad ? 0.178f : 1.0f;
        const float gAud  = rbtube::PotTaper::audio(pGain, 1.20f);
        inGain  = pad;
        inScale = 0.9f + 3.4f * gAud;                 // 4.6→3.4 (2026-06-23): a HOT real bass DI
                                                      // over-drove V1; trimmed for a cleaner default.
        preGain = 0.85f + 0.55f * gAud;
        gainOut = 1.0f;                                   // scale V3 output into the V4 phase inverter

        if (pUltraLo){ ulLow.lowShelf(sr,50.f,6.0f); ulMid.peaking(sr,500.f,0.8f,-9.0f); ulHigh.highShelf(sr,8000.f,4.0f); }
        else { ulLow.bypass(); ulMid.bypass(); ulHigh.bypass(); }
        if (pUltraHi) uhShelf.highShelf(sr,4000.f,6.0f); else uhShelf.bypass();

        bqBass.lowShelf(sr, 70.f, (pBass-0.5f)*30.f);
        int sel=(int)(pFreq*3.0f); if(sel>2)sel=2; if(sel<0)sel=0;   // 3-pos selector
        bqMid.peaking(sr, kMidFreqs[sel], 0.7f, (pMid-0.5f)*28.f);
        bqTreble.highShelf(sr, 5000.f, (pTreble-0.5f)*30.f);

        // 4×7027A (≈6L6GC) push-pull, ~100W. NFB → low sag; bias ~−45 V (6L6GC
        // table point). Less headroom than the SVT → breaks up a touch earlier.
        const float master = rbtube::PotTaper::audio(pMaster, 1.20f);
        // V4 12AU7 long-tail-pair PI: drive rises with Master + Gain (it hardens when
        // pushed, like the real V-4B splitter). Feeds the 4×7027A power stage.
        phaseInverter.set(sr, 0.4f + 0.7f*master + 0.3f*gAud, 1.0f);
        power.set(sr, 0.55f + 3.0f*master, -45.0f, 0.14f, 30.0f, 9000.0f);
        power.out = 0.0050f;
        otVoice.lowpass(sr, 7500.0f, 0.7f);

        // Loudness makeup (same family target as the SVT: ~−13 clean → ~−9 cranked).
        // Flatten to the SVT/family level (~-12 dB): the new V1/V2 → V3 12DW7 → V4
        // 12AU7 PI → power chain ramps intrinsic level ~20 dB across Gain, so the
        // makeup DECREASES with Gain (Gain reads as growl-amount, loudness ~flat).
        outLevel = std::pow(10.0f, 0.05f * (39.0f - 17.0f * pGain));
        if (pPad) outLevel *= 0.45f;     // pad-aware (cleaner padded input, see SvtCore)
    }

    inline float process(float x){
        x = inCoupling.process(x * inGain);
        x = v1.process(x * inScale);
        if (pUltraLo){ x = ulLow.process(x); x = ulMid.process(x); x = ulHigh.process(x); }
        x = bqBass.process(x); x = bqMid.process(x); x = bqTreble.process(x);
        if (pUltraHi) x = uhShelf.process(x);
        x = v2.process(x * preGain);
        x = v3.process(x);                       // V3 12DW7 driver (high-mu section)
        x = phaseInverter.process(x * gainOut);  // V4 12AU7 long-tail-pair phase inverter
        x = power.process(x);                     // 4×7027A push-pull (PI-driven)
        x = otVoice.process(x);
        return x * outLevel;
    }
};

} // namespace v4b
#endif // V4B_CORE_H
