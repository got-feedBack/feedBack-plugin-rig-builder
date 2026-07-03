#ifndef REDHEAD_CORE_H
#define REDHEAD_CORE_H
//
// RedheadCore — SWR Super Redhead, circuit-real-ish on the shared tube_stage.hpp
// framework. HYBRID: a 12AX7 tube front/recovery + TL072 op-amp gain/EQ + a ~350W
// solid-state power amp. Schematic: "Super Redhead (Complete)" (preamp #170007 1990
// + SWR2000 power module Rev D, ±77V BJT). The SWR character is the PREAMP: the tube
// input, the Aural Enhancer contour, and the semi-parametric EQ.
//
//   IN (Passive/Active) -> .47u/1M coupling (very deep lows)
//      -> V1a 12AX7 input -> op-amp GAIN (+Turbo grit)
//      -> EQ: Bass shelf / semi-param MID (Level + sweepable Freq ~100Hz..2kHz) /
//         Treble shelf (+Transparency air)  +  AURAL ENHANCER (deep-low + high boost,
//         low-mid scoop, scaling with the knob)
//      -> V1b 12AX7 recovery -> ·Master -> SS power amp (clean hi-fi, soft ceiling)
//
// 1x 12AX7 (both halves) + op-amp EQ (biquads) + SS power. Runs oversampled (2x).
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace redhead {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void bypass(){ b0=1;b1=b2=a1=a2=0; }
    void lowShelf(float sr,float f,float dB){ float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/2*std::sqrt((A+1/A)+2),rA=std::sqrt(A);
        float a0=(A+1)+(A-1)*c+2*rA*al; b0=A*((A+1)-(A-1)*c+2*rA*al)/a0; b1=2*A*((A-1)-(A+1)*c)/a0; b2=A*((A+1)-(A-1)*c-2*rA*al)/a0;
        a1=-2*((A-1)+(A+1)*c)/a0; a2=((A+1)+(A-1)*c-2*rA*al)/a0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/2*std::sqrt((A+1/A)+2),rA=std::sqrt(A);
        float a0=(A+1)-(A-1)*c+2*rA*al; b0=A*((A+1)+(A-1)*c+2*rA*al)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-2*rA*al)/a0;
        a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-2*rA*al)/a0; }
    void peaking(float sr,float f,float Q,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*Q),a0=1+al/A;
        b0=(1+al*A)/a0; b1=(-2*c)/a0; b2=(1-al*A)/a0; a1=(-2*c)/a0; a2=(1-al/A)/a0; }
};

struct RedheadCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;             // .47u/1M -> very deep lows (bass)
    rbtube::TubeStage v1, v2;          // 12AX7 input + recovery (both halves)
    Biquad bassBq, midBq, trebleBq, transpBq;        // semi-param EQ
    Biquad auralLow, auralHigh, auralScoop;          // Aural Enhancer contour
    rbtube::LP1 ssLp;                  // power-amp/OT band limit

    // params (0..1)
    float pGain=0.5f, pAural=0.3f, pBass=0.5f, pMidLvl=0.5f, pMidFreq=0.5f, pTreble=0.5f, pMaster=0.7f;
    bool  pActive=false, pTurbo=false, pTransp=false;
    float inScale=1.f, gain=1.f, turboG=1.f, master=1.f, outLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setGain(float v){ pGain=clamp01(v); recalc(); }
    void setAural(float v){ pAural=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMidLevel(float v){ pMidLvl=clamp01(v); recalc(); }
    void setMidFreq(float v){ pMidFreq=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setMaster(float v){ pMaster=clamp01(v); recalc(); }
    void setActive(bool b){ pActive=b; recalc(); }
    void setTurbo(bool b){ pTurbo=b; recalc(); }
    void setTransparency(bool b){ pTransp=b; recalc(); }

    void reset(){ inCoupling.reset(); v1.reset(); v2.reset();
        bassBq.reset(); midBq.reset(); trebleBq.reset(); transpBq.reset();
        auralLow.reset(); auralHigh.reset(); auralScoop.reset(); }

    void recalc(){
        inCoupling.set(sr, 3.0f);                         // .47u/1M, very deep (bass)
        v1.set(sr, 1, 250.0f, 40.0f, 14.0f, 1500.0f);     // V1a input (250V plate, 1.5k cath)
        v2.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);     // V1b recovery

        // Op-amp GAIN (500k) + Active pad + Pull-Turbo (extra gain/grit).
        const float pad  = pActive ? 0.55f : 1.0f;
        const float gAud = rbtube::PotTaper::audio(pGain, 1.25f);
        inScale = pad;
        gain    = 0.35f + 1.5f * gAud;                    // op-amp gain (clean hi-fi headroom)
        turboG  = pTurbo ? 1.7f : 1.0f;                   // Turbo: hotter/grittier

        // EQ (op-amp, ±15 dB): Bass shelf, semi-param Mid, Treble shelf + Transparency.
        bassBq.lowShelf(sr, 80.0f, (pBass-0.5f)*30.0f);
        const float midF = 100.0f * std::pow(20.0f, pMidFreq);   // sweep 100 Hz .. 2 kHz
        midBq.peaking(sr, midF, 1.0f, (pMidLvl-0.5f)*30.0f);
        trebleBq.highShelf(sr, 4000.0f, (pTreble-0.5f)*30.0f);
        if (pTransp) transpBq.highShelf(sr, 8000.0f, 4.0f); else transpBq.bypass();

        // AURAL ENHANCER: deep-low boost + high boost + low-mid scoop, all scaling with
        // the knob (0 = flat). The classic SWR "smile" that deepens with rotation.
        const float a = pAural;
        auralLow.lowShelf(sr, 60.0f, a * 7.0f);
        auralHigh.highShelf(sr, 5000.0f, a * 6.0f);
        auralScoop.peaking(sr, 250.0f, 0.9f, a * -5.0f);

        master = rbtube::PotTaper::audio(pMaster, 1.10f);
        ssLp.set(sr, 9000.0f);                            // SS power + OT band limit

        // Loudness makeup — gain-dependent. Hybrid (op-amp gain + SS power) has little
        // internal loss, so a SMALL makeup (unlike the all-tube amps' big +15 dB).
        outLevel = std::pow(10.0f, 0.05f * (6.0f + 8.0f * pGain));
    }

    // Solid-state power amp: clean & linear, soft ceiling (no tube sag). ~350W stays
    // hi-fi until the rails; flat-top 4th-order soft clip near full scale.
    static inline float ssPower(float x){ const float a=std::fabs(x);
        return (a < 1e-6f) ? x : x / std::pow(1.0f + std::pow(a, 4.0f), 0.25f); }

    inline float process(float x){
        x = inCoupling.process(x * inScale);
        x = v1.process(x * gain * turboG);                // V1a + op-amp gain (+Turbo)
        // EQ + Aural Enhancer
        x = bassBq.process(x); x = midBq.process(x); x = trebleBq.process(x);
        if (pTransp) x = transpBq.process(x);
        x = auralLow.process(x); x = auralHigh.process(x); x = auralScoop.process(x);
        x = v2.process(x);                                // V1b recovery
        x = ssPower(x * (0.4f + 1.6f * master));          // SS power (clean + soft ceiling)
        x = ssLp.process(x);
        return x * outLevel;
    }
};

} // namespace redhead
#endif // REDHEAD_CORE_H
