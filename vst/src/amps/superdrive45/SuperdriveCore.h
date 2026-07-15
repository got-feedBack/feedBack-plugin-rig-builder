#ifndef SUPERDRIVE_CORE_H
#define SUPERDRIVE_CORE_H

// Ganddi SuperDrive 45 component model. The available SD80 drawing supplies
// the shared preamp values; the SD45 Series II manual supplies the 2x KT66 and
// tube-rectifier power configuration. The 80's four-output-tube/solid-state
// supply is deliberately not copied into this 45 W model.
#include "SuperdriveParams.h"
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace superdrive45 {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float gainMakeupDb(float gain) {
    // Re-fit 2026-07-15 (Brit DI): the sweep sat ~8 dB under the -16 dBFS
    // family target. Clean end PEAK-matched (~-3.5 dBFS), cranked ~-16 RMS.
    static const float kDb[11]={17.f,15.1f,13.2f,11.2f,9.3f,7.4f,6.1f,5.8f,5.1f,4.3f,3.f};
    const float x=10.f*clamp01(gain);int i=(int)x;if(i>=10)return kDb[10];
    return kDb[i]+(kDb[i+1]-kDb[i])*(x-(float)i);
}

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ const float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y);return y; }
    void reset(){x1=x2=y1=y2=0;}
    void norm(float a0){b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0;}
    void peaking(float sr,float f,float q,float db){f=std::fmin(f,sr*.49f);const float A=std::pow(10.f,db/40.f),w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*q);b0=1+al*A;b1=-2*c;b2=1-al*A;const float z=1+al/A;a1=-2*c;a2=1-al/A;norm(z);}
    void highShelf(float sr,float f,float db){f=std::fmin(f,sr*.49f);const float A=std::pow(10.f,db/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*.5f*std::sqrt((A+1/A)+2),r=std::sqrt(A);b0=A*((A+1)+(A-1)*c+2*r*al);b1=-2*A*((A-1)+(A+1)*c);b2=A*((A+1)+(A-1)*c-2*r*al);const float z=(A+1)-(A-1)*c+2*r*al;a1=2*((A-1)-(A+1)*c);a2=(A+1)-(A-1)*c-2*r*al;norm(z);}
    void lowpass(float sr,float f,float q){f=std::fmin(f,sr*.49f);const float w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*q);b0=(1-c)*.5f;b1=1-c;b2=b0;const float z=1+al;a1=-2*c;a2=1-al;norm(z);}
    void highpass(float sr,float f,float q){f=std::fmin(f,sr*.49f);const float w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*q);b0=(1+c)*.5f;b1=-(1+c);b2=b0;const float z=1+al;a1=-2*c;a2=1-al;norm(z);}
};

struct SuperdriveCore {
    float sr=96000.0f;
    float p[kParamCount]{};
    float driveE=.2f,rhythmE=.2f,masterE=.2f;
    float lastPower=0,lastScreen=0,lastPreamp=0;

    rbtube::HP1 inputCoupling;
    rbtube::TubeStage v1,vLead2,vLead3,vRhythm,vPost;
    rbtube::Miller12AX7 m1,mLead2,mLead3,mRhythm,mPost;
    rbtube::CouplingCapGridLeak cLead2,cLead3,cRhythm,cPost,cPi;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterCathodyne12AX7 pi;
    rbtube::MultiNodeBPlus supply;
    rbtube::PowerAmpKT66 power;
    Biquad briteShelf,modernScoop,modernAir,cabHp,cabBody,cabBite,cabLp;

    void setSampleRate(float s){sr=s;recalc();reset();}
    void setParam(int i,float v){if(i>=0&&i<kParamCount){p[i]=clamp01(v);recalc();}}
    void initDefaults(){for(int i=0;i<kParamCount;++i)p[i]=kSuperDef[i];recalc();reset();}

    void reset(){inputCoupling.reset();v1.reset();vLead2.reset();vLead3.reset();vRhythm.reset();vPost.reset();m1.reset();mLead2.reset();mLead3.reset();mRhythm.reset();mPost.reset();cLead2.reset();cLead3.reset();cRhythm.reset();cPost.reset();cPi.reset();tone.reset();pi.reset();supply.reset();power.reset();briteShelf.reset();modernScoop.reset();modernAir.reset();cabHp.reset();cabBody.reset();cabBite.reset();cabLp.reset();lastPower=lastScreen=lastPreamp=0;}

    void recalc(){
        inputCoupling.set(sr,12.0f);
        // SD80 preamp drawing: input 220k/2k7+47u, lead 100k/820R+.68u,
        // rhythm 220k/4k7+.68u, post-EQ driver 100k/2k7.
        v1.setWithPlate(sr,0,300.f,42.f,1.25f,2700.f,220000.f);
        vLead2.setWithPlate(sr,1,285.f,40.f,285.f,820.f,100000.f);
        vLead3.setWithPlate(sr,1,280.f,42.f,50.f,4700.f,220000.f);
        vRhythm.setWithPlate(sr,1,280.f,42.f,50.f,4700.f,220000.f);
        vPost.setWithPlate(sr,1,300.f,42.f,20000.f,2700.f,100000.f);

        driveE=.025f+.975f*rbtube::PotTaper::audio(p[kDrive],2.0f);
        rhythmE=.025f+.975f*rbtube::PotTaper::audio(p[kRhythm],2.0f);
        masterE=.02f+.98f*rbtube::PotTaper::audio(p[kMaster],2.0f);
        m1.set(sr,10000.f,55.f,8.f);
        mLead2.set(sr,90000.f,52.f,8.f); mLead3.set(sr,180000.f,55.f,8.f);
        mRhythm.set(sr,180000.f,52.f,8.f); mPost.set(sr,125000.f,52.f,8.f);
        cLead2.set(sr,1000000.f,22e-9f,10000.f,.18f,.28f,1.0f);
        cLead3.set(sr,1000000.f,10e-9f,220000.f,.16f,.34f,1.2f);
        cRhythm.set(sr,1000000.f,10e-9f,220000.f,.18f,.24f,.8f);
        cPost.set(sr,1000000.f,22e-9f,250000.f,.18f,.30f,1.0f);
        cPi.set(sr,1000000.f,22e-9f,220000.f,.18f,.28f,1.0f);

        // SD80 shared stack: 500k treble/bass, 50k mid, 56k slope,
        // 220pF treble and 22nF bass/mid capacitors.
        tone.setComponents(500e3,500e3,50e3,56e3,220e-12,22e-9,22e-9);
        const bool modern=p[kModern]>=.5f && p[kChannel]>=.5f;
        tone.update(sr,p[kTreble],modern?.33f*p[kMid]:p[kMid],p[kBass]);
        modernScoop.peaking(sr,720.f,.72f,modern?-5.0f:0.0f);
        modernAir.highShelf(sr,2600.f,modern?3.0f:0.0f);
        briteShelf.highShelf(sr,2300.f,(p[kBrite]>=.5f&&p[kChannel]<.5f)?7.0f:0.0f);

        // The drawing's post-EQ gain stage feeds a split-load inverter. The
        // unequal 220k/10k values are retained in the cathodyne helper.
        pi.set(sr,2.0f,.9f,300.f,220000.f,10000.f,2.5f,.03f);
        supply.set(sr,115.f,50.f,1000.f,47.f,9400.f,22.f,.20f,.13f,.07f,.20f);
        power.set(sr,4.8f,-40.f,.18f,38.f,15000.f);
        power.out=.010f;

        cabHp.highpass(sr,68.f,.72f);cabBody.peaking(sr,120.f,.8f,2.0f);
        cabBite.peaking(sr,2850.f,.8f,3.0f);cabLp.lowpass(sr,14500.f,.66f);
    }

    inline float process(float x){
        const auto b=supply.process(lastPower,lastScreen,lastPreamp);
        x=inputCoupling.process(x);
        x=v1.process(m1.process(x)*2.05f*b.preamp);
        const bool lead=p[kChannel]>=.5f;
        if(lead){
            x=cLead2.process(x,0.35f+3.0f*driveE);
            x=vLead2.process(mLead2.process(x)*b.preamp);
            x=cLead3.process(x,0.55f+2.7f*driveE);
            x=vLead3.process(mLead3.process(x)*b.preamp);
        }else{
            x=briteShelf.process(x);
            x=cRhythm.process(x,0.35f+2.4f*rhythmE);
            x=vRhythm.process(mRhythm.process(x)*b.preamp);
        }
        x=tone.process(x);x=modernAir.process(modernScoop.process(x));
        x=cPost.process(x,0.45f+2.6f*masterE);
        x=vPost.process(mPost.process(x)*b.preamp);
        x=cPi.process(x,1.15f);
        lastPreamp=.12f*std::fabs(x);
        x=pi.process(x*b.screen);
        lastScreen=.35f*std::fabs(x);
        x=power.process(x*b.power*b.screen);
        lastPower=.55f*std::fabs(x);
        const float amp=x;
        float cab=cabLp.process(cabBite.process(cabBody.process(cabHp.process(amp))));
        x=amp+p[kCabSim]*(cab-amp);
        const float makeup=std::pow(10.f,.05f*gainMakeupDb(lead?p[kDrive]:p[kRhythm]));
        return x*.78f*makeup;
    }
};

} // namespace superdrive45
#endif
