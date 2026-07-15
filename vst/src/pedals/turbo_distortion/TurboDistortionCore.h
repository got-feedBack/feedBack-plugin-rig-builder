#ifndef TURBO_DISTORTION_CORE_H
#define TURBO_DISTORTION_CORE_H
//
// Boss DS-2 component-guided core from boards 7510952000 / 7510954000.
// The audio path is discrete: 2SK118 FET buffers/switches, cascaded 2SC3378 /
// 2SC2458 BJT gain blocks, D14/D15 1S188FM pre-clipping, D11/D12 1SS133
// clipping, the switched Turbo gain network and the transistor tone/output
// stages. There is no op-amp in this circuit.
//
#include <cmath>
#include "../../_shared/semiconductors.hpp"

namespace turbodistortion {
static constexpr float kPi=3.14159265358979323846f;
static inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
static inline float dn(float v){ return std::fabs(v)<1e-15f?0.f:v; }

class HP {
    float a=0.f,x1=0.f,y1=0.f;
public:
    void setRC(float sr,float r,float c){ const float dt=1.f/sr,rc=r*c; a=rc/(rc+dt); }
    void reset(){x1=y1=0.f;}
    float process(float x){const float y=a*(y1+x-x1);x1=x;y1=dn(y);return y1;}
};
class LP {
    float a=0.f,y=0.f;
public:
    void setRC(float sr,float r,float c){a=1.f-std::exp(-1.f/(r*c*sr));}
    void setHz(float sr,float hz){a=1.f-std::exp(-2.f*kPi*hz/sr);}
    void reset(){y=0.f;}
    float process(float x){y+=a*(x-y);return y=dn(y);}
};
class DC {
    float x1=0.f,y1=0.f;
public:
    void reset(){x1=y1=0.f;}
    float process(float x){const float y=x-x1+0.9988f*y1;x1=x;y1=dn(y);return y1;}
};

static inline float bjtCurve(float v,float pk,float nk,float pr,float nr){
    return v>=0.f?pr*(1.f-std::exp(-pk*v)):-nr*(1.f-std::exp(nk*v));
}
static inline float bjt(float x,float drive,float bias,float pk=1.22f,float nk=1.06f,float pr=1.02f,float nr=0.88f){
    return bjtCurve(bias+drive*x,pk,nk,pr,nr)-bjtCurve(bias,pk,nk,pr,nr);
}

class TurboDistortionCore {
    float fs=192000.f,dist=.55f,tone=.5f,level=.5f;
    bool turbo=false;
    HP inputC12,c38,c40,c39,c45,c35,toneC37,outC24;
    LP inputMiller,q22Miller,q23Miller,turboMiller,turboMid,postClipLp,toneLow,toneHighBase,outLoad;
    DC dc1,dc2,dc3,dcOut;
    rbcomponents::AntiParallelDiodePair preClip,mainClip;

    void update(){
        const float d=1.f-std::pow(1.f-clamp01(dist),2.15f); // VR1 250kD
        inputC12.setRC(fs,1010000.f,.047e-6f);
        c38.setRC(fs,104700.f,.15e-6f);
        c40.setRC(fs,105600.f,.047e-6f);
        c39.setRC(fs,104700.f,.047e-6f);
        c45.setRC(fs,104700.f,.022e-6f);
        c35.setRC(fs,104700.f,10e-6f);
        toneC37.setRC(fs,15000.f+.5f*100000.f,.0068e-6f);
        outC24.setRC(fs,1000000.f,.05e-6f);
        inputMiller.setHz(fs,12500.f);
        q22Miller.setHz(fs,9200.f-1800.f*d);
        q23Miller.setHz(fs,7800.f-1500.f*d);
        turboMiller.setHz(fs,6400.f);
        turboMid.setHz(fs,720.f);
        postClipLp.setRC(fs,4700.f,0.0047e-6f);
        // Q12/Q10/Q13 active tone board around C28/C29/C30/C37.
        toneLow.setRC(fs,47000.f+(1.f-tone)*100000.f,.0068e-6f);
        toneHighBase.setRC(fs,15000.f+tone*100000.f,.0068e-6f);
        outLoad.setHz(fs,12500.f);
        preClip.setSpec(rbcomponents::diode1S188FM());
        preClip.setSourceR(4700.f);
        mainClip.setSpec(rbcomponents::diode1SS133());
        mainClip.setSourceR(4700.f-2200.f*d);
    }

public:
    void setSampleRate(float sr){fs=sr>1000.f?sr:192000.f;reset();}
    void setDist(float v){dist=clamp01(v);update();}
    void setTone(float v){tone=clamp01(v);update();}
    void setLevel(float v){level=clamp01(v);}
    void setTurbo(float v){turbo=v>=.5f;update();}
    void reset(){
        inputC12.reset();c38.reset();c40.reset();c39.reset();c45.reset();c35.reset();toneC37.reset();outC24.reset();
        inputMiller.reset();q22Miller.reset();q23Miller.reset();turboMiller.reset();turboMid.reset();postClipLp.reset();toneLow.reset();toneHighBase.reset();outLoad.reset();
        dc1.reset();dc2.reset();dc3.reset();dcOut.reset();preClip.reset();mainClip.reset();update();
    }

    float process(float in){
        const float d=1.f-std::pow(1.f-clamp01(dist),2.15f);

        // Q6/Q21 2SK118 input buffer and electronic switch.
        float x=inputMiller.process(inputC12.process(.975f*in));
        x=.985f*std::tanh(.72f*x)/.72f;
        x=c38.process(x);

        // D14/D15 are the low-knee 1S188FM pair before Q22.
        x=preClip.process(1.15f*x);
        x=c40.process(x);

        // Q22 and Q23 are discrete common-emitter gain stages. Their emitter
        // bypass networks and VR1 produce the nonlinear gain sweep.
        float y=-bjt(x,3.0f+10.f*d+16.f*d*d,-.018f);
        y=q22Miller.process(dc1.process(y));
        y=c39.process(y);
        y=-bjt(y,2.2f+6.5f*d,-.012f,1.18f,1.04f,.98f,.84f);
        y=q23Miller.process(dc2.process(y));

        // Q14/Q15 and Q16-Q20 switch the additional Turbo-II compound stage.
        // Mode I bypasses it; Mode II adds gain and the 700-1k mid emphasis.
        if(turbo){
            const float low=turboMid.process(y);
            const float mid=y-.72f*low;
            y=c45.process(y+.95f*mid);
            y=-bjt(y,2.4f+8.5f*d,-.020f,1.28f,1.08f,1.0f,.82f);
            y=turboMiller.process(dc3.process(y));
        }

        // Q17/Q18 driver into D11/D12 1SS133 shunt clipping.
        y=c35.process(y);
        y=mainClip.process((1.35f+2.1f*d+(turbo?.55f:0.f))*y);
        y=postClipLp.process(y);

        // Discrete active TONE network. Centre retains the characteristic
        // DS-2 scoop; clockwise selects the C37 high branch.
        const float lo=toneLow.process(y);
        const float hb=toneHighBase.process(y);
        const float hi=toneC37.process(y-hb);
        const float t=clamp01(tone);
        y=lo*(1.12f-.82f*t)+hi*(.18f+1.42f*t)-hb*(.10f+.16f*(1.f-std::fabs(2.f*t-1.f)));

        // VR3 50KA LEVEL is a passive output pot and must mute at zero.
        // 2.75 (was 1.55, +5 dB): re-fit 2026-07-14 vs the Brit DI — the default
        // sat ~-21 dBFS RMS, ~5 dB under the pedal family (~-16).
        const float vol=2.75f*std::pow(level,2.2f);
        y=outLoad.process(dcOut.process(y*vol));
        y=outC24.process(y);
        return std::tanh(y);
    }
};
} // namespace turbodistortion
#endif
