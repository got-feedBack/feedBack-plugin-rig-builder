#ifndef MAJOR_CORE_H
#define MAJOR_CORE_H

// Marshall Major 200W white-box model. The local 1966/200W schematics agree on
// the audible backbone used here: two ECC83 input channels and their real volume
// attenuators, ECC83 V2 before the passive stack, ECC82 LTP, four KT88s around
// 585V with roughly -81V fixed bias, and a very stiff solid-state supply.
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace majoramp {

static constexpr float kPi=3.14159265358979f;
static inline float clamp01(float v){return v<0?0:(v>1?1:v);}

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){const float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;x2=x1;x1=x;y2=y1;y1=rbtube::dn(y);return y;}
    void reset(){x1=x2=y1=y2=0;}
    void highShelf(float sr,float f,float dB){f=std::fmin(f,sr*.49f);const float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*.70710678f,rA=std::sqrt(A),t=2*rA*al;const float aa0=(A+1)-(A-1)*c+t;b0=A*((A+1)+(A-1)*c+t)/aa0;b1=-2*A*((A-1)+(A+1)*c)/aa0;b2=A*((A+1)+(A-1)*c-t)/aa0;a1=2*((A-1)-(A+1)*c)/aa0;a2=((A+1)-(A-1)*c-t)/aa0;}
};

struct MajorCore {
    float sr=96000.f;
    rbtube::HP1 inputCoupling,spkHP;
    rbtube::LP1 otVoice,spkLP1,spkLP2;
    rbtube::TubeStage vBright,vNormal,v2;
    rbtube::Miller12AX7 millBright,millNormal,millV2;
    rbtube::CouplingCapGridLeak coupleBright,coupleNormal,couplePi,couplePower;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AU7 pi;
    rbtube::PowerAmpKT88 power;
    rbtube::MultiNodeBPlus supply;
    Biquad brightShelf,presenceShelf,outTilt,spkPresence;

    float pPres=.5f,pBass=.5f,pMid=.55f,pTreble=.6f,pV1=.68f,pV2=0.f,pInput=.5f,pCab=1.f;
    float vol1=.6f,vol2=0.f,outLevel=1.f;
    float lastPowerLoad=0,lastScreenLoad=0,lastPreampLoad=0;

    void setSampleRate(float s){sr=s>1000.f?s:96000.f;recalc();reset();}
    void setPresence(float v){pPres=clamp01(v);recalc();}
    void setBass(float v){pBass=clamp01(v);recalc();}
    void setMiddle(float v){pMid=clamp01(v);recalc();}
    void setTreble(float v){pTreble=clamp01(v);recalc();}
    void setVolume1(float v){pV1=clamp01(v);recalc();}
    void setVolume2(float v){pV2=clamp01(v);recalc();}
    void setInput(float v){pInput=clamp01(v);recalc();}
    void setCabSim(float v){pCab=clamp01(v);}

    void recalc(){
        // Inputs are DC-coupled through 68k stoppers and 1M grid leaks. The 3Hz
        // guard only removes sub-audio/DC; the old synthetic 100Hz cut was not in the amp.
        inputCoupling.set(sr,3.f);
        vBright.set(sr,1,250.f,40.f,2.2f,820.f); // V1 ECC83, shared 820R/250u
        vNormal.set(sr,1,250.f,40.f,2.2f,820.f);
        v2.set(sr,1,320.f,44.f,13.5f,470.f);     // V2 ECC83, 470R/25u, 100k plate
        millBright.set(sr,68000.f,54.f,7.f);millNormal.set(sr,68000.f,54.f,7.f);
        millV2.set(sr,470000.f,52.f,7.f);
        // Both V1 plates use .022u coupling, 1M audio volumes and 470k mixers.
        coupleBright.set(sr,1000000.f,22e-9f,470000.f,.26f,.055f,.30f);
        coupleNormal.set(sr,1000000.f,22e-9f,470000.f,.26f,.055f,.30f);
        couplePi.set(sr,270000.f,47e-9f,2700.f,.30f,.045f,.28f);
        couplePower.set(sr,68000.f,470e-9f,5600.f,.34f,.035f,.24f);

        // Real 1M log pots — but with a small floor on Volume I's channel: the
        // game maps "Gain low" to CLEAN, not silence (true zero muted the amp
        // and read as broken). Volume II keeps a true zero (it's the unjumpered
        // Normal channel, off by default).
        vol1=.055f+.945f*rbtube::PotTaper::audio(pV1,1.32f);
        vol2=rbtube::PotTaper::audio(pV2,1.32f);
        brightShelf.highShelf(sr,3200.f,2.0f);    // small high-channel wiring/cap difference

        // 1966 PA stack: 250k treble, 1M bass, 25k middle, 56k slope,
        // 250p/.022u/.022u. V2 drives it before the ECC82 PI.
        tone.setComponents(250e3,1e6,25e3,56e3,250e-12,22e-9,22e-9);
        tone.update(sr,pTreble,pMid,pBass);
        presenceShelf.highShelf(sr,3000.f,8.f*pPres); // 5k/.68u NFB presence, boost-only

        // Major V3 ECC82: approximately 30k equal plate loads and 2.7k shared
        // cathode section, at the high-voltage driver node.
        pi.setComponents(sr,1.f,1.f,350.f,30000.f,30000.f,2700.f,12.f,.025f);

        // Four KT88s, 585V plates and about -81V grid bias in the voltage chart.
        // -78V stays inside the generated KT88 table while preserving that cold point.
        power.set(sr,2.15f,-78.f,.025f,35.f,11500.f);
        power.out=.0085f;power.biasShift=.45f;
        otVoice.set(sr,13500.f);outTilt.highShelf(sr,2800.f,2.f);

        // Bridge rectifier and the large 375u/200u reservoir chain make this much
        // stiffer than a Plexi. Separate screen/preamp nodes still move slightly.
        supply.set(sr,10.f,375.f,1500.f,200.f,10000.f,50.f,.045f,.035f,.025f,.08f);
        spkHP.set(sr,90.f);spkLP1.set(sr,4600.f);spkLP2.set(sr,5500.f);spkPresence.highShelf(sr,2700.f,2.5f);

        const float knob=std::fmax(pV1,pV2);
        // Post-power compensation keeps low-volume clean settings audible without
        // changing where V1/V2/PI/KT88 distortion occurs. Exact zero remains mute.
        // +2.5 dB re-fit 2026-07-14 (Brit DI): the sweep sat ~2.5-4 dB under the
        // family target; now ~-15.9 RMS cranked, clean floor ~-26, peaks < -2.5.
        outLevel=std::pow(10.f,.05f*(8.5f+10.f*std::exp(-3.f*knob)));
    }

    void reset(){inputCoupling.reset();otVoice.reset();spkHP.reset();spkLP1.reset();spkLP2.reset();
        vBright.reset();vNormal.reset();v2.reset();millBright.reset();millNormal.reset();millV2.reset();
        coupleBright.reset();coupleNormal.reset();couplePi.reset();couplePower.reset();tone.reset();pi.reset();power.reset();supply.reset();
        brightShelf.reset();presenceShelf.reset();outTilt.reset();spkPresence.reset();lastPowerLoad=lastScreenLoad=lastPreampLoad=0.f;}

    inline float process(float x){
        const rbtube::SupplyScales bplus=supply.process(lastPowerLoad,lastScreenLoad,lastPreampLoad);
        x=inputCoupling.process(x);
        const float brightWeight=pInput<=.5f?1.f:1.f-(pInput-.5f)*2.f;
        const float normalWeight=pInput>=.5f?1.f:pInput*2.f;
        // Volumes are after V1, exactly as drawn; they no longer change V1's operating point.
        float b=vBright.process(millBright.process(brightShelf.process(x))*2.6f*bplus.preamp);
        float n=vNormal.process(millNormal.process(x)*2.6f*bplus.preamp);
        b=coupleBright.process(b,vol1)*brightWeight;
        n=coupleNormal.process(n,vol2)*normalWeight;
        float y=.72f*(b+n);
        y=v2.process(millV2.process(y*2.15f)*bplus.preamp);
        y=tone.process(y);
        y=couplePi.process(y,2.6f);
        lastPreampLoad=std::fabs(y)*(.20f+.65f*std::fmax(vol1,vol2));
        y=pi.process(y*bplus.screen*5.2f);
        y=couplePower.process(y,1.65f);
        lastScreenLoad=std::fabs(y)*(.25f+.60f*std::fmax(vol1,vol2));
        y=power.process(presenceShelf.process(y)*bplus.power*bplus.screen);
        lastPowerLoad=std::fabs(y)*(.40f+.70f*std::fmax(vol1,vol2));
        y=outTilt.process(otVoice.process(y))*outLevel;
        if(pCab>.0001f){float c=spkPresence.process(spkLP2.process(spkLP1.process(spkHP.process(y))))*1.5f;y+=pCab*(c-y);}
        return rbtube::dn(y);
    }
};

} // namespace majoramp
#endif
