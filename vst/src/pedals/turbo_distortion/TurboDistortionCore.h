#ifndef TURBO_DISTORTION_CORE_H
#define TURBO_DISTORTION_CORE_H
//
// TurboDistortionCore — Boss DS-2 Turbo Distortion, component-guided from the
// factory schematic (ET/VR boards 7510952000 / 7510954000).
//
//   IN -> FET buffer -> gain stage (DIST) -> silicon feedback clip (1SS-133)
//      -> [TURBO II: mid pre-emphasis + 2nd gain stage + clip] -> hard silicon
//         clip to ground (1SS-133) -> active TONE (LO/HI) -> LEVEL -> OUT.
//
// Two modes: I = classic DS-1-family distortion (open, medium gain); II = the
// Turbo network — more gain, tighter lows, a mid-forward bark and heavier
// compression/sustain. Runs oversampled by the caller (nonlinear clippers).
//
#include <cmath>
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"

namespace turbodistortion {

static constexpr float kPi = 3.14159265358979323846f;
static inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
static inline float smoothstep(float v){ v=clamp01(v); return v*v*(3.f-2.f*v); }
static inline float dn(float v){ return std::fabs(v)<1.0e-15f?0.f:v; }
static inline float clampFreq(float hz,float sr){ const float n=sr*0.45f; return hz<20.f?20.f:(hz>n?n:hz); }

class RcHighPass {
    float a=0.f,x1=0.f,y1=0.f;
public:
    void setHz(float sr,float hz){ const float s=sr>1000.f?sr:48000.f; const float rc=1.f/(2.f*kPi*clampFreq(hz,s)); const float dt=1.f/s; a=rc/(rc+dt); }
    void reset(){ x1=y1=0.f; }
    inline float process(float x){ const float y=a*(y1+x-x1); x1=x; y1=dn(y); return y1; }
};
class RcLowPass {
    float a=0.f,y=0.f;
public:
    void setHz(float sr,float hz){ const float s=sr>1000.f?sr:48000.f; a=1.f-std::exp(-2.f*kPi*clampFreq(hz,s)/s); }
    void reset(){ y=0.f; }
    inline float process(float x){ y+=a*(x-y); y=dn(y); return y; }
};
class DcBlock {
    float x1=0.f,y1=0.f;
public:
    void reset(){ x1=y1=0.f; }
    inline float process(float x){ const float y=x-x1+0.9985f*y1; x1=x; y1=dn(y); return y1; }
};
// One-pole tilt for the DS-2 TONE: a lowpassed + highpassed split mixed by the
// pot (CCW = dark/low, CW = bright/high), centred = flat-ish.
class Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void set(float nb0,float nb1,float nb2,float na0,float na1,float na2){ if(std::fabs(na0)<1e-12f)na0=1.f; const float i=1.f/na0; b0=nb0*i;b1=nb1*i;b2=nb2*i;a1=na1*i;a2=na2*i; }
public:
    void reset(){ z1=z2=0.f; }
    inline float process(float x){ const float y=b0*x+z1; z1=dn(b1*x-a1*y+z2); z2=dn(b2*x-a2*y); return y; }
    void setPeaking(float sr,float hz,float q,float dB){ const float s=sr>1000.f?sr:48000.f; hz=clampFreq(hz,s); const float a=std::pow(10.f,dB/40.f),w=2*kPi*hz/s,c=std::cos(w),al=std::sin(w)/(2*q);
        set(1+al*a,-2*c,1-al*a,1+al/a,-2*c,1-al/a); }
};

class TurboDistortionCore {
    float sampleRate=96000.f;
    float dist=0.55f, tone=0.5f, level=0.5f;
    bool  turbo=false;

    RcHighPass inHP, stage2HP, outHP;
    RcLowPass  inLP, clipLP, toneLP, toneHP_lp;
    RcHighPass toneHP;
    DcBlock    dc1, dc2, dcOut;
    Biquad     midBark;                 // Mode II mid pre-emphasis (the "turbo" bark)
    rbshared::OpAmpStage  gain1, gain2; // transistor gain stages (M5218-ish model)
    rbcomponents::AntiParallelDiodePair softClip1;  // feedback silicon clip (1SS-133)
    rbcomponents::AntiParallelDiodePair softClip2;  // Mode II second clip
    rbcomponents::AntiParallelDiodePair hardClip;   // D9/D10 hard clip to ground (1SS-133)

    void updateComponentValues(){
        const float d=smoothstep(dist);
        // Input: FET buffer feed. Mode II tightens the lows (less flub into the
        // extra gain); Mode I keeps more body.
        inHP.setHz(sampleRate, turbo ? 150.f : 95.f);
        inLP.setHz(sampleRate, 11000.f);
        // Gain stages: DIST scales the closed-loop gain. Mode II runs hotter.
        gain1.setSpec(rbshared::m5218Spec());
        gain2.setSpec(rbshared::m5218Spec());
        // silicon clippers — the real 1SS-133; source R falls as DIST rises (harder)
        softClip1.setSpec(rbcomponents::diode1SS133()); softClip1.setSourceR(2400.f - 900.f*d);
        softClip2.setSpec(rbcomponents::diode1SS133()); softClip2.setSourceR(2000.f - 800.f*d);
        hardClip.setSpec(rbcomponents::diode1SS133());  hardClip.setSourceR(1500.f - 600.f*d);
        clipLP.setHz(sampleRate, 6500.f);         // de-fizz the clip harmonics
        stage2HP.setHz(sampleRate, 180.f);
        midBark.setPeaking(sampleRate, 900.f, 0.9f, turbo ? 7.0f : 0.0f);
        // TONE: split lo/hi around ~700 Hz, mixed by the pot.
        toneLP.setHz(sampleRate, 720.f);
        toneHP.setHz(sampleRate, 720.f);
        toneHP_lp.setHz(sampleRate, 6200.f);
        outHP.setHz(sampleRate, 30.f);
    }

public:
    void setSampleRate(float sr){ sampleRate=sr>1000.f?sr:96000.f; gain1.setSampleRate(sampleRate); gain2.setSampleRate(sampleRate); reset(); }
    void setDist(float v){ dist=clamp01(v); updateComponentValues(); }
    void setTone(float v){ tone=clamp01(v); updateComponentValues(); }
    void setLevel(float v){ level=clamp01(v); }
    void setTurbo(float v){ turbo=(v>=0.5f); updateComponentValues(); }

    void reset(){
        inHP.reset(); stage2HP.reset(); outHP.reset(); inLP.reset(); clipLP.reset();
        toneLP.reset(); toneHP.reset(); toneHP_lp.reset(); dc1.reset(); dc2.reset(); dcOut.reset();
        midBark.reset(); gain1.reset(); gain2.reset(); softClip1.reset(); softClip2.reset(); hardClip.reset();
        updateComponentValues();
    }

    float process(float in){
        const float d=smoothstep(dist);
        // FET input buffer
        float x=inLP.process(inHP.process(in));
        // ── Stage 1 gain + silicon feedback clip ──
        const float g1 = 2.5f + 22.0f*dist + 30.0f*d + (turbo ? 26.0f : 0.0f);
        float y = gain1.process(x, 2.0f + 10.0f*d);
        y = softClip1.process(y * g1 * 0.5f);
        y = dc1.process(y);
        // ── Mode II: mid pre-emphasis + 2nd gain stage + clip (the Turbo bark) ──
        if (turbo) {
            y = midBark.process(y);
            y = stage2HP.process(y);
            const float g2 = 3.0f + 16.0f*dist + 20.0f*d;
            y = gain2.process(y, 2.0f + 12.0f*d);
            y = softClip2.process(y * g2 * 0.5f);
            y = dc2.process(y);
        }
        // ── Hard silicon clip to ground (D9/D10) ──
        y = hardClip.process(y * (1.6f + 0.8f*d));
        y = clipLP.process(y);
        // ── active TONE: mix lo + hi split by the pot ──
        const float t = clamp01(tone);
        const float lo = toneLP.process(y);
        const float hi = toneHP_lp.process(toneHP.process(y));
        y = lo*(1.10f - 0.85f*t) + hi*(0.25f + 1.35f*t);
        y = outHP.process(dcOut.process(y));
        // ── LEVEL + loudness trim (harder settings self-compress, so the trim
        //    keeps output ~unity across the sweep) ──
        const float lvl = (0.30f + 1.30f*level) / (1.0f + 0.7f*dist + 0.5f*d + (turbo?0.35f:0.f));
        return std::tanh(1.02f * y * lvl);
    }
};

} // namespace turbodistortion
#endif // TURBO_DISTORTION_CORE_H
