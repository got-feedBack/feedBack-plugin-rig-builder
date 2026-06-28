#ifndef CITRUS_CORE_HPP
#define CITRUS_CORE_HPP
//
// CitrusCore<PowerTube> — the Orange/"Citrus" family OWN core (AD50/Tiny Terror/
// Rockerverb/OR50/OR100). The shared guitar_amp_core drives ONLY v2 -> its soft
// 1-stage clip COMPRESSES the dynamics (crest ~7) but generates almost no high-order
// harmonics (fizz ~-19 dB vs a real Orange's ~-10) = the "lacks gain/grind" the amps
// had. This drives a DEEP cascade (v2..v4) + a driven power amp (the ENGL/DslCore
// recipe) so the high harmonics ARE there. Templated on the power tube (EL34 / EL84).
//
//   IN -> inCoupling -> V1 -> V2  [DIRTY: -> V3 -> V4] -> tone stack -> scoop
//      -> Presence -> V_recovery (driven) -> 12AX7 LTP PI -> power -> OT roll -> tilt.
//   Thick mid-forward Orange voice; the plugin adds the cab + the amp's switches.
//
#include "tube_stage.hpp"
#include <cmath>

namespace citrus {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void highShelf(float sr,float f,float dB){ if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
    void peak(float sr,float f,float dB,float Q){ if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q);
        float a0=1+al/A; b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
};

template <typename PowerTube>
struct CitrusCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v2, v3, v4, vR;
    Biquad brightShelf, scoop, presenceShelf, outTilt;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AT7 pi;
    PowerTube power;
    rbtube::LP1 otVoice;

    // params
    bool dirty = true;                                  // 2ch amps: false = clean
    float pGain=.6f, pBass=.5f, pMid=.5f, pTreble=.6f, pPres=.5f, pVol=.6f;
    float inBoost = 1.0f;                               // plugin-side push (Sustain etc.)

    // per-amp config
    double cTr=250e3,cBa=470e3,cMi=25e3,cSl=33e3,ccT=250e-12,ccB=22e-9,ccM=22e-9;
    float cfgDirtySpan=9.f, cfgCleanSpan=2.6f, cfgHpDirty=80.f, cfgHpClean=55.f;
    float cfgOt=11000.f, cfgTilt=6.f, cfgScoop=-1.5f, cfgBias=-38.f, cfgPwDrive=3.0f;
    float cfgMkDirty=2.f, cfgMkClean=14.f;
    int   cfgNStages=3;                                 // dirty pre-tone driven stages (2..4)

    float g1=1,g2=1,g3=1,gR=1,piDrive=6,outLevel=1;     // g1->v2, g2->v3, g3->v4, gR->vR

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setTone(double Rt,double Rb,double Rm,double Rsl,double Ct,double Cb,double Cm){ cTr=Rt;cBa=Rb;cMi=Rm;cSl=Rsl;ccT=Ct;ccB=Cb;ccM=Cm; }
    void reset(){ inCoupling.reset(); v1.reset();v2.reset();v3.reset();v4.reset();vR.reset();
        brightShelf.reset();scoop.reset();presenceShelf.reset();outTilt.reset();
        tone.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, dirty ? cfgHpDirty : cfgHpClean);
        v1.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v2.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v3.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
        v4.set(sr, 1, 250.0f, 40.0f, 33.0f, 1500.0f);   // cold late stage -> the grind
        vR.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);

        const float g = rbtube::PotTaper::audio(pGain, 1.30f);
        if (dirty) {
            // Deep cascade -> rich high-order harmonics (the fizz/grind). Drives BEFORE
            // the tone stack (loses ~15 dB), the recovery AFTER.
            const float s = cfgDirtySpan;
            g1 = 0.6f + (0.65f*s) * g;                          // v2
            g2 = (cfgNStages>=3) ? 0.7f + (0.55f*s)*g : 1.0f;   // v3
            g3 = (cfgNStages>=4) ? 0.8f + (0.45f*s)*g : 1.0f;   // v4
            gR = 1.3f + (0.55f*s) * g;                          // recovery
        } else {
            g1 = 0.30f + 1.7f*g;  g2 = 1.0f; g3 = 1.0f;
            gR = 1.0f + 1.0f*g;
        }
        brightShelf.highShelf(sr, 2000.0f, 5.0f * (1.0f - pGain));
        tone.setComponents(cTr,cBa,cMi,cSl,ccT,ccB,ccM);
        tone.update(sr, pTreble, pMid, pBass);
        scoop.peak(sr, 2000.0f, dirty ? cfgScoop : 0.0f, 1.0f);     // mild upper-mid dip
        presenceShelf.highShelf(sr, 3000.0f, (pPres-0.5f)*10.0f);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);
        const float vol = rbtube::PotTaper::audio(pVol, 1.15f);
        // Drive the power tubes (the cranked-Orange squash adds even harmonics + level).
        power.set(sr, (dirty ? cfgPwDrive : 0.5f) + (dirty ? 3.2f : 2.0f)*vol, cfgBias, 0.06f, 30.0f, 11000.0f);
        power.out = 0.012f;
        otVoice.set(sr, cfgOt);
        outTilt.highShelf(sr, 2600.0f, cfgTilt);

        const float mk = dirty ? cfgMkDirty : cfgMkClean;
        outLevel = std::pow(10.0f, 0.05f * (mk - 5.0f * pGain));
    }

    inline float process(float x){
        x = inCoupling.process(x * inBoost);
        float y = v1.process(x);
        y = brightShelf.process(y);
        y = v2.process(y * g1);                     // v2 always
        if (dirty && cfgNStages>=3) y = v3.process(y * g2);
        if (dirty && cfgNStages>=4) y = v4.process(y * g3);
        y = tone.process(y);
        y = scoop.process(y);
        y = presenceShelf.process(y);
        y = vR.process(y * gR);
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        y = outTilt.process(y);
        return y * outLevel;
    }
};

} // namespace citrus
#endif // CITRUS_CORE_HPP
