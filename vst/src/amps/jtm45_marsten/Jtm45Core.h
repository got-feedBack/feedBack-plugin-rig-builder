#ifndef JTM45_CORE_H
#define JTM45_CORE_H
//
// Jtm45Core — Marshall JTM45, circuit-real on the shared
// tube_stage.hpp framework with CONTROLLED gain staging (clean at low Loudness ->
// roar at high). Same proven pattern as Jcm800Core / the bass amps; rewritten from
// the over-gained cascade that saturated every signal to ~100% THD.
//
//   IN -> coupling -> Bright + Normal 12AX7 channels (Loudness I / II, jumpered)
//   -> Plexi tone stack (Yeh) -> presence -> recovery 12AX7 -> 12AX7 LTP PI
//   -> 4x EL34 (non-master: the Loudness knobs drive the whole amp) -> OT roll.
//   Runs 2x oversampled.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace jtm45 {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
};

struct Jtm45Core {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage vBright, vNormal, v3;
    Biquad brightShelf, presenceShelf;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AT7 pi;
    rbtube::PowerAmp5881 power;
    rbtube::LP1 otVoice;

    float pPres=.5f,pBass=.5f,pMid=.55f,pTreble=.62f,pL1=.62f,pL2=.0f,pInput=.5f;
    float gB=1.f,gN=1.f,piDrive=6.f,outLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setLoudness1(float v){ pL1=clamp01(v); recalc(); }
    void setLoudness2(float v){ pL2=clamp01(v); recalc(); }
    void setInput(float v){ pInput=clamp01(v); recalc(); }

    void reset(){ inCoupling.reset(); vBright.reset(); vNormal.reset(); v3.reset();
        brightShelf.reset(); tone.reset(); presenceShelf.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, 30.0f);
        vBright.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        vNormal.set(sr, 1, 250.0f, 40.0f, 40.0f, 1500.0f);
        v3.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);

        // Loudness knobs = the drive (non-master). Controlled span: clean -> roar.
        gB = 0.30f + 5.0f * rbtube::PotTaper::audio(pL1, 1.30f);
        gN = 0.30f + 5.0f * rbtube::PotTaper::audio(pL2, 1.30f);
        brightShelf.highShelf(sr, 2200.0f, 5.0f);

        // Plexi tone stack (Yeh): Treble 250k/500pF, Bass 1M/22nF, Mid 25k/22nF, slope 33k.
        tone.setComponents(250e3, 1e6, 25e3, 33e3, 270e-12, 22e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);
        presenceShelf.highShelf(sr, 3000.0f, (pPres-0.5f)*10.0f);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);
        power.set(sr, 1.6f, -38.0f, 0.06f, 30.0f, 11000.0f);   // 4x EL34 (non-master)
        power.out = 0.011f;
        otVoice.set(sr, 9000.0f);

        const float drv = (pL1 > pL2 ? pL1 : pL2);
        outLevel = std::pow(10.0f, 0.05f * (13.5f - 7.0f * drv));
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
        y = v3.process(y);
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        return y * outLevel;
    }
};

} // namespace jtm45
#endif // JTM45_CORE_H
