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
    void setHz(float sr,float hz){ const float dt=1.f/sr,rc=1.f/(2.f*kPi*hz); a=rc/(rc+dt); }
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
static inline float bjtCurve(float v,float pk,float nk,float pr,float nr){
    return v>=0.f?pr*(1.f-std::exp(-pk*v)):-nr*(1.f-std::exp(nk*v));
}
static inline float bjt(float x,float drive,float bias,float pk=1.22f,float nk=1.06f,float pr=1.02f,float nr=0.88f){
    return bjtCurve(bias+drive*x,pk,nk,pr,nr)-bjtCurve(bias,pk,nk,pr,nr);
}

// 2-pole resonant band-pass (TPT SVF) — the Turbo-II gyrator resonance.
class ResBP {
    float g=0.f,k=1.f,s1=0.f,s2=0.f;
public:
    void set(float sr,float f,float q){ g=std::tan(kPi*f/sr); k=1.f/q; }
    void reset(){ s1=s2=0.f; }
    float process(float x){
        const float a1=1.f/(1.f+g*(g+k)), a2=g*a1;
        const float v3=x-s2;
        const float bp=a1*s1+a2*v3;
        const float lp=s2+a2*s1+g*a2*v3;
        s1=dn(2.f*bp-s1); s2=dn(2.f*lp-s2);
        return bp;
    }
};

class TurboDistortionCore {
    float fs=192000.f,dist=.55f,tone=.5f,level=.5f;
    bool turbo=false;
    HP inputC12,c38,c40,c39,c45,c35,toneC37,outC24,modeLowCut;
    LP inputMiller,q22Miller,q23Miller,turboMiller,toneLow,toneHighBase,outLoad,turboTop;
    ResBP turboRes,modeVoice;
    rbcomponents::AntiParallelDiodePair preClip,mainClip;

    void update(){
        const float d=1.f-std::pow(1.f-clamp01(dist),2.15f); // VR1 250kD
        inputC12.setRC(fs,1010000.f,.047e-6f);
        c38.setRC(fs,104700.f,.15e-6f);
        c40.setRC(fs,105600.f,.047e-6f);
        c39.setRC(fs,104700.f,.047e-6f);
        c45.setRC(fs,104700.f,.022e-6f);
        c35.setRC(fs,104700.f,10e-6f);
        toneC37.setRC(fs,15000.f,.0068e-6f);
        outC24.setRC(fs,1000000.f,.05e-6f);
        inputMiller.setHz(fs,12500.f);
        q22Miller.setHz(fs,9200.f-1800.f*d);
        q23Miller.setHz(fs,7800.f-1500.f*d);
        turboMiller.setHz(fs,6400.f);
        // Q16-Q20 gyrator: the Turbo-II voice is a RESONANT mid peak feeding
        // the extra gain stage — the DS-2 'honk' — not a broad tilt.
        turboRes.set(fs,900.f,2.2f);
        // The switched transistor network changes more than gain: Mode I has
        // a broad mid recovery and less sub-bass loading, while Mode II keeps
        // the narrower resonant voice and a lower top-end pole.
        modeLowCut.setHz(fs,turbo?80.f:110.f);
        modeVoice.set(fs,turbo?1120.f:1250.f,turbo?.75f:.68f);
        turboTop.setHz(fs,turbo?6800.f:15000.f);
        // Q12/Q10/Q13 create fixed low and high branches. VR2 blends those
        // branch voltages; it does not sweep both cutoff frequencies.
        toneLow.setRC(fs,33000.f,.0068e-6f);
        toneHighBase.setRC(fs,22000.f,.0047e-6f);
        outLoad.setHz(fs,12500.f);
        preClip.setSpec(rbcomponents::diode1S188FM());
        preClip.setSourceR(4700.f);
        mainClip.setSpec(rbcomponents::diode1SS133());
        mainClip.setSourceR(4700.f); // R57 is fixed; DIST does not alter it
    }

public:
    void setSampleRate(float sr){fs=sr>1000.f?sr:192000.f;reset();}
    void setDist(float v){dist=clamp01(v);update();}
    void setTone(float v){tone=clamp01(v);update();}
    void setLevel(float v){level=clamp01(v);}
    void setTurbo(float v){turbo=v>=.5f;update();}
    void reset(){
        inputC12.reset();c38.reset();c40.reset();c39.reset();c45.reset();c35.reset();toneC37.reset();outC24.reset();modeLowCut.reset();
        inputMiller.reset();q22Miller.reset();q23Miller.reset();turboMiller.reset();turboRes.reset();modeVoice.reset();toneLow.reset();toneHighBase.reset();outLoad.reset();turboTop.reset();
        preClip.reset();mainClip.reset();update();
    }

    float process(float in){
        const float d=1.f-std::pow(1.f-clamp01(dist),2.15f);

        // Q6/Q21 2SK118 input buffer and electronic switch.
        float x=inputMiller.process(inputC12.process(.975f*in));
        x=.985f*x;
        x=c38.process(x);

        // D14/D15 are the low-knee 1S188FM pair before Q22.
        x=preClip.process(3.0f*x)/3.0f;
        x=c40.process(x);

        // Q22 and Q23 are discrete common-emitter gain stages. Their emitter
        // bypass networks and VR1 produce the nonlinear gain sweep.
        float y=-bjt(x,3.0f+10.f*d+16.f*d*d,-.018f);
        y=q22Miller.process(y);
        y=c39.process(y);
        y=-bjt(y,2.2f+6.5f*d,-.012f,1.18f,1.04f,.98f,.84f);
        y=q23Miller.process(y);

        // Q14/Q15 and Q16-Q20 switch the additional Turbo-II compound stage.
        // Mode I bypasses it; Mode II adds the gyrator's RESONANT ~900 Hz mid
        // peak (the DS-2 honk/vocal voice) into an extra gain stage. The old
        // broad tilt (y - .72*LP720) just read as "more gain" — user report:
        // "mode II sounds like normal distortion, not turbo".
        if(turbo){
            y=c45.process(y+3.2f*turboRes.process(y));
            y=-bjt(y,2.6f+9.0f*d,-.020f,1.28f,1.08f,1.0f,.82f);
            y=turboMiller.process(y);
        }

        // Q17/Q18 driver into D11/D12 1SS133 shunt clipping.
        y=c35.process(y);
        y=mainClip.process(3.0f*(1.15f+1.55f*d+(turbo?.40f:0.f))*y)/3.0f;

        // Q12/Q10/Q13 and the mode-dependent loading recover the vocal centre
        // that remains visible in the reference renders. The previous Mode I
        // path fed the LO branch almost directly, leaving a bass-heavy hole
        // from roughly 700 Hz to 3 kHz. Mode II uses less broad recovery because
        // Q16-Q20 already provide its resonant peak.
        y=modeLowCut.process(y);
        y+= (turbo?.30f:.78f)*modeVoice.process(y);
        y=turboTop.process(y);
        if(turbo) y*=1.25f;

        // Discrete active TONE network. The unequal gains are the transistor
        // recovery/loading of each branch, not tone-dependent makeup.
        const float lo=toneLow.process(y);
        const float hb=toneHighBase.process(y);
        const float hi=toneC37.process(y-hb);
        const float t=clamp01(tone);
        const float centreLoad=4.f*t*(1.f-t);
        y=1.05f*(1.f-t)*lo+2.40f*t*hi+0.65f*centreLoad*lo;

        // VR3 50KA LEVEL is a passive output pot and must mute at zero.
        const float vol=13.5f*((std::pow(10.0f,2.0f*level)-1.0f)/99.0f);
        y=outLoad.process(y*vol);
        y=outC24.process(y);
        // Q5/Q3/Q11 recover the passive level loss but remain bounded by the
        // 9 V supply. This is output-stage headroom, not an extra clipper.
        const float outputRail=1.15f;
        return outputRail*std::tanh(y/outputRail);
    }
};
} // namespace turbodistortion
#endif
