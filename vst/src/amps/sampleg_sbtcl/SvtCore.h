#ifndef SVT_CORE_H
#define SVT_CORE_H
//
// SvtCore — Ampeg SVT-CL all-tube bass head, circuit-real (framework tube_stage.hpp).
//
// Replaces the previous half-real build (nodal V1 + RBJ tone + a single tanh power
// stage) with the shared framework so the SVT joins the circuit-real family:
//
//   IN → input coupling (HP, big SVT caps → very low corner, keeps the lows)
//      → ·inScale → V1 12AX7 gain  (the SVT growl when Gain is pushed)
//      → Ultra Lo contour (S2A)    → Bass / Midrange(LC, Frequency) / Treble stack
//      → Ultra Hi presence (S4A)   → ·preGain → V2 12AX7 driver
//      → ·gainOut → 6×6550 push-pull power (real Koren differential + sag + OT)
//      → output voicing → out
//
// Why NOT ToneStackYeh: the SVT tone stack is NOT a Fender/Marshall TMB. Bass and
// Treble are separate shelving controls and the Midrange is an LC band whose centre
// is the 5-position Frequency selector (220/450/800/1600/3000 Hz). Per the guide
// ("bloque propio si el circuito no es TMB normal") these stay as their real
// shelving + LC sections (white-boxed biquads), which is the correct topology here.
//
// Power: the SVT runs six 6550 in push-pull with GLOBAL negative feedback → much
// stiffer/cleaner than the AC30 (no-NFB). Modeled as PowerAmp6550 with LOW sag
// (stiff supply) and a low OT high-pass so the fundamental survives (bass amp).
//
// Component values: SVT-CL preamp board 07S519 + power board (6×6550). Runs at the
// oversampled rate; SvtPlugin wraps it 2×. Gain-staging constants are first-draft
// (calibrated by character: bass DI 41–440 Hz, crest monotonic, stable 48/96/192k).
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace svtcl {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// 5-position Midrange-selector centre frequencies (Hz) — the LC band with the
// Frequency-switch caps (.68/.15/.047/.012/.0033 µF) against the mid inductor.
static const float kMidFreqs[5] = { 220.f, 450.f, 800.f, 1600.f, 3000.f };

// RBJ biquad (peaking / shelves / low-pass), denormal-flushed via rbtube::dn.
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

struct SvtCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;             // input coupling cap + grid leak (big SVT cap)
    rbtube::TubeStage v1, v2;           // 12AX7 gain + 12AX7 driver (self-biasing)
    Biquad ulLow, ulMid, ulHigh;        // Ultra Lo contour (S2A: R19-22 220k + C7 470p + C8 .0047µF)
    Biquad uhShelf;                     // Ultra Hi presence (S4A: R37 270k/R38 18k + C13/C15)
    Biquad bqBass, bqMid, bqTreble;     // passive tone stack (P-pots + freq-selector LC)
    rbtube::PowerAmp6550 power;         // 6×6550 push-pull (NFB → tight)
    Biquad otVoice;                     // gentle output-transformer top roll (pre-cab)

    // params (0..1)
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

    void reset(){ inCoupling.reset(); v1.reset(); v2.reset();
        ulLow.reset(); ulMid.reset(); ulHigh.reset(); uhShelf.reset();
        bqBass.reset(); bqMid.reset(); bqTreble.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        // Big SVT coupling caps → keep the deep lows (bass amp, no guitar HP).
        inCoupling.set(sr, 5.0f);
        // V1 12AX7 input/gain (07S519): Rk 1.5k + 22µF bypass → fck ≈ 1/(2π·1500·22µ) ≈ 4.8 Hz
        // (fully bypassed across the audio band). 68k grid-leak (tab 0), B+ ~250V.
        v1.set(sr, 0, 250.0f, 40.0f, 4.8f, 1500.0f);
        // V2 12AX7 driver/recovery: 250k grid-leak (tab 1), Rk 1.5k + bypass.
        v2.set(sr, 1, 250.0f, 40.0f, 4.8f, 1500.0f);

        // P1 250kA Gain — drives V1 from clean to growl. −15 dB pad (R2 27k jack) ≈ 0.178.
        const float pad   = pPad ? 0.178f : 1.0f;
        const float gAud  = rbtube::PotTaper::audio(pGain, 1.20f);   // audio taper: clean low, cooks high
        inGain  = pad;
        inScale = 0.8f + 2.4f * gAud;                 // audio → grid volts into V1 (cleaner on a hot real bass; growl scales with Gain).
                                                      // 4.6→3.4 (2026-06-23): a HOT real bass DI over-drove V1
                                                      // into too much growl at default; trimmed for a cleaner DI.
        preGain = 0.85f + 0.55f * gAud;               // V1 → V2 inter-stage
        // post-V2 into the power amp. Re-tuned 120 → 15 (2026-07-02): at 120 the
        // 6550 grids swung ±125 V around the −48 V bias for a −12 dBFS-peak DI —
        // the power stage ran as a permanent hard clipper (even the −32 dB
        // multitone pinned the rbAmpLvl ceiling, THD −9 dB) and the ~40 dB of
        // clipped-away linear gain became IDLE HISS (+36 dB small-signal noise
        // gain vs +9/+3 on the GK/V-4B: "el sampleg tiene mucho ruido de fondo").
        // The real SVT's GLOBAL NFB linearizes the power stage below clipping, so
        // small-signal gain ≈ signal gain; 15 reproduces that: grid peaks ≈ ±30 V
        // at the calibrated DI (compression only on real peaks). Validated
        // (tune harness, default knobs): DI pk 0.54 crest 12, MT −25.8 dB
        // THD −55 dB (GK-league), idle noise gain −1.9 dB.
        gainOut = 15.0f;

        // ── Ultra Lo (S2A): fixed loudness contour — deep-low + top lift, low-mid scoop.
        if (pUltraLo){ ulLow.lowShelf(sr,50.f,6.0f); ulMid.peaking(sr,500.f,0.8f,-9.0f); ulHigh.highShelf(sr,8000.f,4.0f); }
        else { ulLow.bypass(); ulMid.bypass(); ulHigh.bypass(); }
        // ── Ultra Hi (S4A): presence/bite ≈ 4 kHz high shelf (the SVT clank).
        if (pUltraHi) uhShelf.highShelf(sr,4000.f,6.0f); else uhShelf.bypass();

        // ── Passive tone stack (±15 dB, 0.5 = flat) ──
        bqBass.lowShelf(sr, 70.f, (pBass-0.5f)*30.f);                    // P3 + C20/C21 → low shelf ~70 Hz
        int sel=(int)(pFreq*5.0f); if(sel>4)sel=4; if(sel<0)sel=0;       // 5-pos Frequency selector
        bqMid.peaking(sr, kMidFreqs[sel], 0.7f, (pMid-0.5f)*28.f);       // LC mid band at the selected centre
        bqTreble.highShelf(sr, 5000.f, (pTreble-0.5f)*30.f);            // P5 + C26/C27 → high shelf ~5 kHz

        // ── 6×6550 push-pull. NFB SVT → LOW sag (stiff supply) + low bias drift; the
        //    Master cooks the power tubes. Bias ~−48 V (6550 table operating point).
        const float master = rbtube::PotTaper::audio(pMaster, 1.20f);
        power.set(sr, 0.45f + 2.6f*master, -48.0f, 0.12f, 30.0f, 9000.0f); // (drive, bias, sag, OT HP, OT LP)
        power.out = 0.0045f;                                            // plate-volt differential → signal
        // Gentle pre-cab output voicing (the cab IR adds the speaker; keep this soft).
        otVoice.lowpass(sr, 7500.0f, 0.7f);

        // Loudness makeup. The old gain-dependent RISE (13+12·Gain) compensated a
        // natural RMS that FELL with Gain — an artifact of the slammed power stage
        // (more V1 drive just squared the wave harder). With the linear power
        // stage (gainOut 15) the natural level RISES with Gain like the GK/V-4B,
        // so the makeup is a flat −1 dB anchored to the family contract at
        // default knobs (DI pk ~0.54, MT −25.8 dB ≈ GK's −26). The rig's leveler
        // finishes the loudness like for every other amp.
        outLevel = std::pow(10.0f, 0.05f * -1.0f);
        // Pad-aware makeup: the −15 dB pad cuts pre-NAM drive (cleaner, less
        // compression), so the gain-staged makeup above over-drives the now-cleaner
        // signal into the ceiling. Trim it back when padded → the padded input stays
        // clean and sits a touch below the un-padded level (≈ matches the GK).
        if (pPad) outLevel *= 0.45f;
    }

    inline float process(float x){
        x = inCoupling.process(x * inGain);
        x = v1.process(x * inScale);                       // V1 growl
        if (pUltraLo){ x = ulLow.process(x); x = ulMid.process(x); x = ulHigh.process(x); }
        x = bqBass.process(x); x = bqMid.process(x); x = bqTreble.process(x);
        if (pUltraHi) x = uhShelf.process(x);
        x = v2.process(x * preGain);                       // V2 driver
        x = power.process(x * gainOut);                    // 6×6550 PP
        x = otVoice.process(x);
        return x * outLevel;
    }
};

} // namespace svtcl
#endif // SVT_CORE_H
