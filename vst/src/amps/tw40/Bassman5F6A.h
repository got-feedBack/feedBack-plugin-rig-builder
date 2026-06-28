#ifndef BASSMAN_5F6A_CORE_H
#define BASSMAN_5F6A_CORE_H
//
// Bassman5F6A — Fender '59 Bassman 5F6-A (tweed), circuit-real on the PROVEN
// PlexiCore framework. The old TW40Core gain ladder was fragile (it ramped
// recovery+PI+power together and the 5881 Koren table fell below conduction ->
// silent-or-fuzz, never a clean middle). Here the PI + power run at FIXED drive
// (always conducting, with headroom) and the breakup ramp comes purely from the
// preamp Volume(s), exactly like the real amp. Two jumperable 12AY7 channels
// (Bright + Normal) fix the dead Normal knob.
//
//   IN -> coupling -> Bright(12AY7)+Normal(12AY7) jumpered -> Bassman FMV tone
//   (Yeh) -> presence -> 12AX7 recovery -> 12AX7 LTP PI -> 2x 5881 -> OT roll
//   -> 4x10 voice. Runs 2x oversampled. Schematic: Fender_bassman_5f6a.pdf.
//
#include "TW40Params.h"
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace tw40 {

static constexpr float kPi5 = 3.14159265358979f;
static inline float clamp01b(float v){ return v<0?0:(v>1?1:v); }

struct BqB {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi5*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
    void peaking(float sr,float f,float Q,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi5*f/sr,c=std::cos(w),al=std::sin(w)/(2*Q);
        float a0=1+al/A; b0=(1+al*A)/a0; b1=(-2*c)/a0; b2=(1-al*A)/a0; a1=(-2*c)/a0; a2=(1-al/A)/a0; }
    void highPass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kPi5*f/sr,c=std::cos(w),al=std::sin(w)/(2*Q);
        float a0=1+al; b0=((1+c)*0.5f)/a0; b1=(-(1+c))/a0; b2=((1+c)*0.5f)/a0; a1=(-2*c)/a0; a2=(1-al)/a0; }
};

struct Bassman5F6A {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStageAY7 vBright, vNormal;      // 12AY7 input channels
    rbtube::TubeStage v3;                        // 12AX7 recovery
    BqB brightShelf, presenceShelf, cabHp, cabBody, cabBite, outTilt;
    rbtube::ToneStackYeh tone;                   // Bassman FMV (Yeh)
    rbtube::PhaseInverterLTP12AX7 pi;
    rbtube::PowerAmp5881 power;                  // 2x 5881
    rbtube::LP1 otVoice;

    float pInput=0.5f, pBrightVol=0.58f, pNormalVol=0.42f, pTreble=0.6f, pBass=0.5f, pMid=0.55f, pPres=0.45f, pCabSim=1.0f;
    float gB=1.f, gN=1.f, v3Drive=1.f, piDrive=6.f, outLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setParam(int idx, float v){
        v=clamp01b(v);
        switch(idx){
            case kInput:     pInput=v; break;
            case kBrightVol: pBrightVol=v; break;
            case kNormalVol: pNormalVol=v; break;
            case kTreble:    pTreble=v; break;
            case kBass:      pBass=v; break;
            case kMiddle:    pMid=v; break;
            case kPresence:  pPres=v; break;
            case kCabSim:    pCabSim=v; break;
            default: break;
        }
        recalc();
    }
    void reset(){ inCoupling.reset(); vBright.reset(); vNormal.reset(); v3.reset();
        brightShelf.reset(); presenceShelf.reset(); cabHp.reset(); cabBody.reset(); cabBite.reset(); outTilt.reset();
        tone.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, 40.0f);
        vBright.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
        vNormal.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
        v3.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);

        // 5F6-A Volumes ARE the gain (clean -> mild crunch; more headroom than a Plexi).
        gB = 0.30f + 15.0f * rbtube::PotTaper::audio(pBrightVol, 1.30f);
        gN = 0.30f + 12.0f * rbtube::PotTaper::audio(pNormalVol, 1.30f);
        const float drv = (pBrightVol > pNormalVol ? pBrightVol : pNormalVol);
        v3Drive = 1.0f + 8.0f * rbtube::PotTaper::audio(drv, 1.30f);
        brightShelf.highShelf(sr, 2000.0f, 5.0f * (1.0f - pBrightVol));   // 100pF bright cap

        // Bassman FMV tone stack (Yeh): Treble 250k/250pF, Bass 1M/20nF, Mid 25k/20nF, slope 56k.
        tone.setComponents(250e3, 1e6, 25e3, 56e3, 250e-12, 20e-9, 20e-9);
        tone.update(sr, pTreble, pMid, pBass);
        presenceShelf.highShelf(sr, 3000.0f, (pPres-0.5f)*10.0f);

        piDrive = 6.0f;
        pi.setMarshall(sr, 1.35f, 0.88f);                    // FIXED PI drive
        // 2x 5881, FIXED drive. CRITICAL: PowerAmpPPT's `bias` is the grid OPERATING
        // POINT (the 6V6 5E3 uses ~-13) -- the old -38 put the 5881 in deep cutoff so
        // only transients passed (silence/gating). A warm ~-14 keeps it conducting:
        // clean at low signal, breaks up only when the preamp drives it hard.
        power.set(sr, 5.2f, -14.0f, 0.40f, 70.0f, 11000.0f);   // drive the 5881 push-pull harder (that's where a cranked Bassman grinds)
        power.out = 0.018f;
        otVoice.set(sr, 12000.0f);

        // open-tweed 4x10 voice (bright, tight-ish lows)
        cabHp.highPass(sr, 70.0f, 0.72f);
        cabBody.peaking(sr, 120.0f, 0.80f, 1.5f + 1.8f * pBass);
        cabBite.highShelf(sr, 3500.0f, 2.0f);
        outTilt.highShelf(sr, 2600.0f, 1.5f);

        // Gentle loudness makeup. NEVER positive-boost low Volume (slams the output).
        outLevel = std::pow(10.0f, 0.05f * (6.0f - 8.0f * drv));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        const float jb = (pInput <= 0.5f) ? 1.0f : (1.0f - (pInput-0.5f)*2.0f);   // bright weight
        const float jn = (pInput >= 0.5f) ? 1.0f : (pInput*2.0f);                  // normal weight
        const float b = vBright.process(brightShelf.process(x) * gB);
        const float n = vNormal.process(x * gN);
        float y = 0.6f * (jb*b + jn*n);
        y = tone.process(y);
        y = presenceShelf.process(y);
        y = v3.process(y * v3Drive);
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        const float amp = y;
        float cab = cabBite.process(cabBody.process(cabHp.process(amp)));
        y = amp + pCabSim * (cab - amp);
        y = outTilt.process(y);
        return y * outLevel;
    }
};

} // namespace tw40
#endif // BASSMAN_5F6A_CORE_H
