#ifndef RB_GUITAR_AMP_CORE_HPP
#define RB_GUITAR_AMP_CORE_HPP
//
// Shared clean-framework guitar-amp core (single Gain knob family). Same proven
// pattern as Jcm800Core / PlexiCore / the bass amps: a 12AX7 input + a GAIN-driven
// 12AX7 with CONTROLLED drive, the amp's tone stack, a recovery 12AX7, a 12AX7 LTP
// PI and a configurable power-amp tube, with a makeup that DECREASES with Gain so
// the Gain knob is drive (not volume). Fixes the over-gained cascades that
// saturated to ~100% THD. Templated on the power-tube trait (rbtube::TubeEL34 etc).
// Configure() sets the per-amp voicing; runs 2x oversampled (the plugin wraps it).
//
#include "tube_stage.hpp"
#include <cmath>

namespace rbgtr {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
    void peak(float sr,float f,float dB,float Q){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q);
        float a0=1+al/A; b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
};

template<class PowerTrait>
struct AmpCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v2, v3;
    Biquad brightShelf, presenceShelf, voiceScoop;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AT7 pi;
    rbtube::PowerAmpPPT<PowerTrait> power;
    rbtube::LP1 otVoice;

    // per-amp voicing (set by configure)
    double cTr=250e3, cBa=1e6, cMi=25e3, cSl=33e3, ccT=500e-12, ccB=22e-9, ccM=22e-9;
    float gainBase=0.30f, gainSpan=6.0f, makeupBase=13.0f, presFreq=3000.0f, brightAmt=4.0f;
    float scoopFreq=650.0f, scoopDb=0.0f, powerDrive=1.6f, powerBias=-38.0f;

    // params
    float pGain=.5f,pBass=.5f,pMid=.5f,pTreble=.5f,pPres=.5f,pVol=.6f;
    float gDrive=1.f, piDrive=6.f, outLevel=1.f;

    void configure(double tr,double ba,double mi,double sl,double cT,double cB,double cM,
                   float gBase,float gSpan,float mkBase,float pFreq,float brAmt,
                   float scF=650.f,float scDb=0.f,float pwDrive=1.6f,float pBias=-38.0f){
        cTr=tr;cBa=ba;cMi=mi;cSl=sl;ccT=cT;ccB=cB;ccM=cM;
        gainBase=gBase;gainSpan=gSpan;makeupBase=mkBase;presFreq=pFreq;brightAmt=brAmt;
        scoopFreq=scF;scoopDb=scDb;powerDrive=pwDrive;powerBias=pBias;
    }
    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setGain(float v){ pGain=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setVolume(float v){ pVol=clamp01(v); recalc(); }

    void reset(){ inCoupling.reset(); v1.reset(); v2.reset(); v3.reset(); brightShelf.reset();
        presenceShelf.reset(); voiceScoop.reset(); tone.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, 30.0f);
        v1.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v2.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v3.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);
        gDrive = gainBase + gainSpan * rbtube::PotTaper::audio(pGain, 1.30f);
        brightShelf.highShelf(sr, 2000.0f, brightAmt * (1.0f - pGain));
        tone.setComponents(cTr,cBa,cMi,cSl,ccT,ccB,ccM);
        tone.update(sr, pTreble, pMid, pBass);
        voiceScoop.peak(sr, scoopFreq, scoopDb, 0.8f);
        presenceShelf.highShelf(sr, presFreq, (pPres-0.5f)*10.0f);
        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);
        const float vol = rbtube::PotTaper::audio(pVol, 1.15f);
        power.set(sr, powerDrive*(0.4f + 0.9f*vol), powerBias, 0.06f, 30.0f, 11000.0f);
        power.out = 0.011f;
        otVoice.set(sr, 9000.0f);
        outLevel = std::pow(10.0f, 0.05f * (makeupBase - 7.0f * pGain));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1.process(x);
        y = v2.process(brightShelf.process(y) * gDrive);
        y = tone.process(y);
        if (scoopDb != 0.0f) y = voiceScoop.process(y);
        y = presenceShelf.process(y);
        y = v3.process(y);
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        return y * outLevel;
    }
};

} // namespace rbgtr
#endif // RB_GUITAR_AMP_CORE_HPP
