#ifndef MAZ38_CORE_H
#define MAZ38_CORE_H

// Mr. Y 38: Maz-family preamp from the local Maz 18 Jr drawing, followed by
// the Maz 38 Senior NR configuration (4x EL84, solid-state rectification, no
// reverb). Values not visible in that drawing are kept out of the comments.
#include "Maz38Params.h"
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace maz38 {
static constexpr float kPi=3.14159265358979f;
static inline float clamp01(float v){return v<0?0:(v>1?1:v);}

struct Bq {float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    float process(float x){float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;x2=x1;x1=x;y2=y1;y1=rbtube::dn(y);return y;}void reset(){x1=x2=y1=y2=0;}void norm(float z){b0/=z;b1/=z;b2/=z;a1/=z;a2/=z;}
    void peak(float sr,float f,float q,float db){f=std::fmin(f,sr*.49f);float A=std::pow(10.f,db/40),w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*q);b0=1+al*A;b1=-2*c;b2=1-al*A;float z=1+al/A;a1=-2*c;a2=1-al/A;norm(z);}
    void lp(float sr,float f,float q){f=std::fmin(f,sr*.49f);float w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*q);b0=(1-c)*.5f;b1=1-c;b2=b0;float z=1+al;a1=-2*c;a2=1-al;norm(z);}
    void hp(float sr,float f,float q){f=std::fmin(f,sr*.49f);float w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*q);b0=(1+c)*.5f;b1=-(1+c);b2=b0;float z=1+al;a1=-2*c;a2=1-al;norm(z);}
};

struct CathodeFollower {
    rbtube::LP1 gridPole;float current=0,attack=0,release=0;
    void set(float sr){gridPole.set(sr,18000.f);attack=1-std::exp(-1.f/(.0025f*sr));release=1-std::exp(-1.f/(.05f*sr));}
    float process(float x){float g=gridPole.process(x),pos=std::fmax(0.f,g-.42f);current+=(pos-current)*(pos>current?attack:release);float y=g/(1+.5f*current);return .88f*y+.12f*std::tanh(1.25f*y);}
    void reset(){gridPole.reset();current=0;}
};

// Re-fit 2026-07-14 (Brit DI): PEAK-matched — the Maz stays clean/chimey across
// the sweep (crest ~16-22 dB), so the family alignment holds peaks ~-3.5 dBFS
// instead of forcing -16 RMS (which either clipped the clean end or left the
// amp 7 dB under family). Normalized so d[10]=0; residual lives in the 0.525
// output trim in process().
static inline float volumeMakeupDb(float v){static const float d[11]={8.69f,7.81f,6.92f,5.04f,3.82f,2.61f,1.39f,1.16f,0.94f,0.47f,0.0f};float x=10*clamp01(v);int i=(int)x;if(i>=10)return d[10];return d[i]+(d[i+1]-d[i])*(x-i);}

struct Maz38Core {
    float sr=96000,p[kParamCount]{},volE=.2f,masterE=.2f,lastPower=0,lastScreen=0,lastPreamp=0;
    rbtube::HP1 inputCoupling;
    rbtube::TubeStage v1,v2;
    rbtube::Miller12AX7 m1,m2;
    rbtube::CouplingCapGridLeak cV2,cPi;
    CathodeFollower follower;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AX7 pi;
    rbtube::MultiNodeBPlus supply;
    rbtube::PowerAmpPP power;
    Bq cutLp,cabHp,cabBody,cabChime,cabLp;

    void setSampleRate(float s){sr=s;recalc();reset();}
    void setParam(int i,float v){if(i>=0&&i<kParamCount){p[i]=clamp01(v);recalc();}}
    void reset(){inputCoupling.reset();v1.reset();v2.reset();m1.reset();m2.reset();cV2.reset();cPi.reset();follower.reset();tone.reset();pi.reset();supply.reset();power.reset();cutLp.reset();cabHp.reset();cabBody.reset();cabChime.reset();cabLp.reset();lastPower=lastScreen=lastPreamp=0;}
    void recalc(){
        inputCoupling.set(sr,12.f);
        // Parallel V1 halves: 68k plate, 680R/680n cathode. One table stage with
        // the parallel plate load preserves the operating point and doubled gm.
        v1.setWithPlate(sr,0,250.f,38.f,344.f,680.f,68000.f);
        v2.setWithPlate(sr,1,250.f,40.f,7.75f,820.f,100000.f);
        volE=.025f+.975f*rbtube::PotTaper::audio(p[kVolume],2.0f);
        masterE=.02f+.98f*rbtube::PotTaper::audio(p[kMaster],2.0f);
        m1.set(sr,68000.f,48.f,8.f);m2.set(sr,180000.f,52.f,8.f);
        cV2.set(sr,1000000.f,1e-9f,220000.f,.18f,.28f,.9f);
        cPi.set(sr,1000000.f,10e-9f,250000.f,.18f,.30f,1.f);
        follower.set(sr);
        tone.setComponents(250e3,250e3,10e3,56e3,250e-12,100e-9,47e-9);
        tone.update(sr,p[kTreble],p[kMiddle],p[kBass]);
        pi.setComponents(sr,2.0f,.90f,199.f,100000.f,100000.f,1200.f,7.f,.025f);
        // Solid-state rectifier and four EL84s: low source resistance/sag. The
        // PP table is one differential pair; reduced drive plus doubled output
        // represents the four-tube current/headroom without duplicating color.
        supply.set(sr,18.f,47.f,1000.f,47.f,11200.f,22.f,.08f,.06f,.035f,.12f);
        power.set(sr,9.0f,-7.5f,.10f,42.f,17000.f);power.out=.013f;
        cutLp.lp(sr,22000.f-17000.f*std::sqrt(p[kCut]),.70f);
        cabHp.hp(sr,72.f,.72f);cabBody.peak(sr,115.f,.8f,1.8f);cabChime.peak(sr,2850.f,.85f,3.2f);cabLp.lp(sr,15000.f,.68f);
    }
    float process(float x){
        auto b=supply.process(lastPower,lastScreen,lastPreamp);
        x=inputCoupling.process(x);x=v1.process(m1.process(x)*1.85f*b.preamp);
        x=cV2.process(x,.28f+3.15f*volE);x=v2.process(m2.process(x)*b.preamp);
        x=follower.process(x);x=tone.process(x);
        x=cPi.process(x,.35f+2.5f*masterE);lastPreamp=.12f*std::fabs(x);
        x=pi.process(x*b.screen);x=cutLp.process(x);lastScreen=.32f*std::fabs(x);
        x=power.process(x*b.power*b.screen);lastPower=.52f*std::fabs(x);
        const float amp=x;float cab=cabLp.process(cabChime.process(cabBody.process(cabHp.process(amp))));x=amp+p[kCabSim]*(cab-amp);
        return x*.525f*std::pow(10.f,.05f*volumeMakeupDb(p[kVolume]));
    }
};
}
#endif
