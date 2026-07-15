#ifndef JUBILEE_CORE_H
#define JUBILEE_CORE_H

// Marshall 2555 Silver Jubilee white-box model, derived from the local 2555 STD
// schematic. The signal path keeps both halves of V2, the mixed LED/1N4007
// clipper, the Jubilee-specific tandem-Bass EQ, the ECC83 LTP and 4x EL34 power.
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace jubilee {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ const float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void norm(float a0){ b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0; }
    void peaking(float sr,float f,float q,float dB){ f=std::fmin(f,sr*.49f); const float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*q); b0=1+al*A;b1=-2*c;b2=1-al*A;const float aa0=1+al/A;a1=-2*c;a2=1-al/A;norm(aa0); }
    void highShelf(float sr,float f,float dB){ f=std::fmin(f,sr*.49f);const float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*.70710678f,rA=std::sqrt(A),t=2*rA*al; b0=A*((A+1)+(A-1)*c+t);b1=-2*A*((A-1)+(A+1)*c);b2=A*((A+1)+(A-1)*c-t);const float aa0=(A+1)-(A-1)*c+t;a1=2*((A-1)-(A+1)*c);a2=(A+1)-(A-1)*c-t;norm(aa0); }
    void lowShelf(float sr,float f,float dB){ f=std::fmin(f,sr*.49f);const float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*.70710678f,rA=std::sqrt(A),t=2*rA*al; b0=A*((A+1)-(A-1)*c+t);b1=2*A*((A-1)-(A+1)*c);b2=A*((A+1)-(A-1)*c-t);const float aa0=(A+1)+(A-1)*c+t;a1=-2*((A-1)+(A+1)*c);a2=(A+1)+(A-1)*c-t;norm(aa0); }
};

// Lead clip: LED3+D1+D2 in one direction and LED2+D3 in the other. Pulling
// Rhythm Clip adds D4/D5 (1N4007 anti-parallel) and C6=2.2nF at the node.
// The mixed strings are solved as equivalent Shockley branches, preserving the
// unequal knee voltage and differential resistance instead of counting every
// device as a 1N4148.
struct JubileeClipper {
    rbtube::LP1 rhythmCap;
    float node=0.f;
    static float current(float v,float isAmp,float nVt){
        const float e=std::fmax(-55.f,std::fmin(55.f,v/nVt));
        return isAmp*(std::exp(e)-1.f);
    }
    void set(float sr){
        // R8 10k with C6 2.2nF -> 7.23kHz when Rhythm Clip is engaged.
        rhythmCap.set(sr,1.f/(2.f*kPi*10000.f*2.2e-9f));
    }
    float process(float vin,bool rhythm){
        const float drive=rhythm ? rhythmCap.process(vin) : vin;
        const float sourceR=10000.f; // R8
        // Equivalent mixed-string parameters, calibrated around the real mA range.
        constexpr float leadPosIs=1.0e-12f, leadPosNVt=.120f; // LED + 2x 1N4007
        constexpr float leadNegIs=1.0e-12f, leadNegNVt=.100f; // LED + 1x 1N4007
        constexpr float rhythmIs=2.5e-9f, rhythmNVt=.090f;    // 1N4007 each way
        // The branch current is monotonic, so bisection is slower than Newton but
        // cannot overshoot on a hard pick transient or inherit a bad prior sample.
        float lo=-3.5f,hi=3.8f;
        for(int i=0;i<22;++i){
            const float mid=.5f*(lo+hi);
            float f=(mid-drive)/sourceR;
            f += current(mid,leadPosIs,leadPosNVt)-current(-mid,leadNegIs,leadNegNVt);
            if(rhythm)
                f += current(mid,rhythmIs,rhythmNVt)-current(-mid,rhythmIs,rhythmNVt);
            if(f>0.f) hi=mid; else lo=mid;
        }
        node=.5f*(lo+hi);
        return rbtube::dn(node);
    }
    void reset(){ node=0.f; rhythmCap.reset(); }
};

// The 2555 EQ is not the conventional three-cap Marshall stack. This model
// keeps the audible poles from C26=4.7n, C27/C9=100n, C10=10n, C11=470p,
// C7=2.2n and C8=220p, including the 1M tandem Bass control.
struct JubileeToneStack {
    Biquad bassShelf,bassNotch,midPeak,trebleShelf;
    rbtube::HP1 c26Coupling;
    float insertion=.24f;
    void update(float sr,float treble,float middle,float bass){
        const float b=rbtube::PotTaper::audio(bass,1.35f);
        const float m=rbtube::PotTaper::audio(middle,1.35f);
        const float t=clamp01(treble); // VR4 is 220k linear
        const float bassHz=1.f/(2.f*kPi*10000.f*100e-9f);       // R14/C9 = 159Hz
        const float bassKnee=1.f/(2.f*kPi*100000.f*2.2e-9f);   // R15/C7 = 723Hz
        const float midLo=1.f/(2.f*kPi*100000.f*10e-9f);       // VR5/C10 = 159Hz
        const float midHi=1.f/(2.f*kPi*100000.f*470e-12f);     // VR5/C11 = 3.39kHz
        const float midHz=std::sqrt(midLo*midHi);                // ~735Hz
        const float trebleHz=1.f/(2.f*kPi*220000.f*220e-12f);  // VR4/C8 = 3.29kHz
        c26Coupling.set(sr,1.f/(2.f*kPi*100000.f*4.7e-9f));    // 4.7n feed, ~339Hz
        bassShelf.lowShelf(sr,bassHz,-9.f+15.f*b);
        bassNotch.peaking(sr,bassKnee,.75f,-3.5f+5.f*b);
        midPeak.peaking(sr,midHz,.72f,-10.f+15.f*m);
        trebleShelf.highShelf(sr,trebleHz,-11.f+19.f*t);
        insertion=.20f+.09f*(.35f*b+.30f*m+.35f*t);
    }
    float process(float x){
        // C27 supplies the full-band leg while C26 feeds the upper EQ branch.
        const float upper=c26Coupling.process(x);
        float y=bassNotch.process(bassShelf.process(x));
        y=midPeak.process(y+.22f*upper);
        return trebleShelf.process(y)*insertion;
    }
    void reset(){ bassShelf.reset();bassNotch.reset();midPeak.reset();trebleShelf.reset();c26Coupling.reset(); }
};

struct JubileeCore {
    float sr=96000.f;
    rbtube::HP1 inputCoupling;
    rbtube::LP1 otVoice;
    rbtube::TubeStage v1a,v1b,v2a,v2b;
    rbtube::Miller12AX7 millV1a,millV1b,millV2a,millV2b;
    rbtube::CouplingCapGridLeak coupleV1,coupleLoop,couplePi;
    JubileeClipper clip;
    JubileeToneStack tone;
    Biquad gainBright,presenceShelf,outTilt,spkPresence;
    rbtube::PhaseInverterLTP12AX7 pi;
    rbtube::PowerAmpEL34 power;
    rbtube::MultiNodeBPlus supply;
    rbtube::HP1 spkHP;
    rbtube::LP1 spkLP1,spkLP2;

    float pGain=.65f,pLead=.70f,pBass=.5f,pMid=.55f,pTreble=.55f,pPres=.55f,pMaster=.6f;
    float pRhythm=0.f,pCab=1.f;
    float gainPot=.5f,leadPot=.7f,masterPot=.6f,outLevel=1.f;
    float lastPowerLoad=0,lastScreenLoad=0,lastPreampLoad=0;

    void setSampleRate(float s){ sr=s>1000.f?s:96000.f; recalc(); reset(); }
    void setGain(float v){ pGain=clamp01(v);recalc(); }
    void setLeadMaster(float v){ pLead=clamp01(v);recalc(); }
    void setBass(float v){ pBass=clamp01(v);recalc(); }
    void setMiddle(float v){ pMid=clamp01(v);recalc(); }
    void setTreble(float v){ pTreble=clamp01(v);recalc(); }
    void setPresence(float v){ pPres=clamp01(v);recalc(); }
    void setMaster(float v){ pMaster=clamp01(v);recalc(); }
    void setRhythmClip(float v){ pRhythm=v>=.5f?1.f:0.f;recalc(); }
    void setCabSim(float v){ pCab=clamp01(v); }

    void recalc(){
        inputCoupling.set(sr,1.f/(2.f*kPi*1e6f*47e-9f)); // C2=.047u / R2=1M
        v1a.set(sr,1,250.f,40.f,300.f,2700.f);            // R1 2k7 / C1 .68u
        v1b.set(sr,1,250.f,40.f,22.f,1500.f);             // R3 1k5 / C4 10u
        // V2A and V2B are unbypassed gain stages; V2B uses its real 220k plate/470R cathode.
        v2a.setWithPlate(sr,1,300.f,44.f,18000.f,1500.f,100000.f);
        v2b.setWithPlate(sr,1,300.f,52.f,18000.f,470.f,220000.f);
        millV1a.set(sr,68000.f,55.f,8.f); millV1b.set(sr,100000.f,55.f,8.f);
        millV2a.set(sr,47000.f,48.f,8.f); millV2b.set(sr,100000.f,48.f,8.f);
        coupleV1.set(sr,220000.f,22e-9f,100000.f,.20f,.08f,.50f); // C3/R6/R27
        coupleLoop.set(sr,1000000.f,100e-9f,100000.f,.24f,.07f,.40f); // C24/C25 loop
        couplePi.set(sr,1000000.f,22e-9f,100000.f,.28f,.06f,.32f); // C28 into V3

        gainPot=rbtube::PotTaper::audio(pGain,1.30f);     // VR1 1M log
        leadPot=clamp01(pLead);                            // VR2 220k linear
        masterPot=rbtube::PotTaper::audio(pMaster,1.18f); // VR3 1M log
        gainBright.highShelf(sr,2500.f,6.f*(1.f-gainPot)); // C19 470p across VR1
        clip.set(sr);
        tone.update(sr,pTreble,pMid,pBass);
        presenceShelf.highShelf(sr,3100.f,8.f*pPres);      // VR7 22k/C14 in NFB

        // V3 is ECC83, with 82k/100k plates and the 470R + 10k long-tail network.
        pi.setComponents(sr,1.f,1.f,330.f,82000.f,100000.f,470.f,10.f,-.055f);
        power.set(sr,2.25f,-38.f,.075f,30.f,11000.f);
        power.out=.011f; power.biasShift=1.2f;
        otVoice.set(sr,15500.f);
        outTilt.highShelf(sr,2700.f,3.5f);

        // Solid-state bridge, 2x50u reservoir, choke and 10k/50u preamp dropper.
        supply.set(sr,55.f,100.f,180.f,50.f,10000.f,50.f,.12f,.085f,.055f,.11f);
        spkHP.set(sr,95.f);spkLP1.set(sr,4400.f);spkLP2.set(sr,5200.f);spkPresence.highShelf(sr,2600.f,3.f);
        // Post-power loudness trim only: it does not alter any distortion point.
        // Re-fit 2026-07-14 (Brit DI): the previous curve overboosted the clean
        // end (Gain 0 peaked at -0.05 dBFS). Clean end is PEAK-matched
        // (~-3.5 dBFS); the driven half stays at family loudness (~-14..-16 RMS).
        outLevel=std::pow(10.f,.05f*(7.7f+6.0f*std::exp(-5.5f*pGain)));
    }

    void reset(){ inputCoupling.reset();otVoice.reset();v1a.reset();v1b.reset();v2a.reset();v2b.reset();
        millV1a.reset();millV1b.reset();millV2a.reset();millV2b.reset();coupleV1.reset();coupleLoop.reset();couplePi.reset();
        clip.reset();tone.reset();gainBright.reset();presenceShelf.reset();outTilt.reset();pi.reset();power.reset();supply.reset();
        spkHP.reset();spkLP1.reset();spkLP2.reset();spkPresence.reset();lastPowerLoad=lastScreenLoad=lastPreampLoad=0.f; }

    inline float process(float x){
        const rbtube::SupplyScales bplus=supply.process(lastPowerLoad,lastScreenLoad,lastPreampLoad);
        x=inputCoupling.process(x);
        float y=v1a.process(millV1a.process(x)*2.7f*bplus.preamp);
        // Drive floor .38 (was .05): a real 1M log pot at zero is near-mute, but
        // the game maps "Gain low" to CLEAN, not silence — .05 left the amp
        // ~-40 dBFS and GATING at min Gain. .38 = audible clean floor.
        y=coupleV1.process(gainBright.process(y),.38f+3.55f*gainPot);
        y=v1b.process(millV1b.process(y)*bplus.preamp);
        y=clip.process(y*(1.10f+2.5f*gainPot),pRhythm>=.5f);
        // Lead Master is a real post-clip 220k linear attenuator: zero must mute.
        y=v2a.process(millV2a.process(y*(1.65f*leadPot))*bplus.preamp);
        y=coupleLoop.process(y,1.25f);
        y=v2b.process(millV2b.process(y)*bplus.preamp);
        y=tone.process(y);
        y=couplePi.process(y,1.8f*masterPot);               // Output Master before V3
        lastPreampLoad=std::fabs(y)*(.25f+.75f*gainPot);
        y=pi.process(y*bplus.screen*4.8f);
        lastScreenLoad=std::fabs(y)*(.30f+.70f*masterPot);
        y=power.process(presenceShelf.process(y)*bplus.power*bplus.screen);
        lastPowerLoad=std::fabs(y)*(.45f+.75f*masterPot);
        y=outTilt.process(otVoice.process(y))*outLevel;
        if(pCab>.0001f){ float c=spkPresence.process(spkLP2.process(spkLP1.process(spkHP.process(y))))*1.5f; y+=pCab*(c-y); }
        return rbtube::dn(y);
    }
};

} // namespace jubilee
#endif
