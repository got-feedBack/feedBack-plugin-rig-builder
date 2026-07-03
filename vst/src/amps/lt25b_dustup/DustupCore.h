#ifndef DUSTUP_CORE_H
#define DUSTUP_CORE_H
//
// DustupCore — Ashdown ABM500 EVO (Bass Magnifier) bass head, circuit-real on the
// shared tube_stage.hpp framework. Schematic: Ashdown ABM500 EVO preamp (APC010,
// Bass Magnifier), sheets 1 (preamp) + 2 (tone control).
//
//   IN -> Passive/Active pad -> input coupling -> ECC83 (12AX7) "Valve Drive"
//      (V1-A/V1-B; clean->grind as the Valve knob is raised, the Ashdown warmth)
//      -> Shape contour (switchable mid-scoop "smile") -> active Bass/Middle/
//      Treble (45 Hz shelf / 660 Hz bell / 5 kHz shelf, per sheet 2) -> 6-band
//      graphic EQ (100/180/340/1.3k/3.6k/5k, switchable) -> Sub-harmonic
//      generator (octave-down, blended by Sub level) -> opto Comp -> Output
//      -> SS power amp (clean, big headroom) -> ·makeup -> out.
//
// HYBRID amp: only ONE ECC83 valve path uses the framework tube table (so it gets
// the shared softTableLimit — no hard table-edge "8-bit" jumps); everything else
// is solid-state / op-amp, modelled with clean biquads. The whole nonlinear chain
// runs 2x oversampled (DustupPlugin wraps it) to keep the tube + sub-octave clean.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace dustup {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// RBJ biquad (shelves / peaks / band) with denormal guard from the shared header.
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void bypass(){ b0=1;b1=b2=a1=a2=0; }
    void lowShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)+(A-1)*c+t; b0=A*((A+1)-(A-1)*c+t)/a0; b1=2*A*((A-1)-(A+1)*c)/a0; b2=A*((A+1)-(A-1)*c-t)/a0; a1=-2*((A-1)+(A+1)*c)/a0; a2=((A+1)+(A-1)*c-t)/a0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
    void peak(float sr,float f,float dB,float Q){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q);
        float a0=1+al/A; b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
    void lowpass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q);
        float a0=1+al; b0=(1-c)*0.5f/a0; b1=(1-c)/a0; b2=(1-c)*0.5f/a0; a1=-2*c/a0; a2=(1-al)/a0; }
};

// Sub-octave generator — analog-style: track the fundamental, toggle on each
// rising zero-cross (divide-by-two = octave down), shape with the signal envelope.
struct SubOct {
    float lp=0, prev=0, flip=1, env=0, out=0, alp=0.02f, aenv=0.01f, aout=0.02f;
    void setSR(float fs){ alp=1.f-std::exp(-2*kPi*180.f/fs); aenv=1.f-std::exp(-2*kPi*16.f/fs); aout=1.f-std::exp(-2*kPi*110.f/fs); }
    void reset(){ lp=prev=env=out=0; flip=1; }
    inline float process(float x, float level){
        lp += alp * (x - lp);
        if (lp > 0.f && prev <= 0.f) flip = -flip;
        prev = lp;
        env += aenv * (std::fabs(lp) - env);
        out += aout * (flip * env - out);
        return level * out;
    }
};

// Gentle opto compressor (Ashdown's built-in comp) — on/off + amount.
struct OptoComp {
    float env=0, atk=0, rel=0;
    void setSR(float fs){ atk=std::exp(-1.f/(0.005f*fs)); rel=std::exp(-1.f/(0.130f*fs)); }
    void reset(){ env=0; }
    inline float process(float x, float amount){
        const float a=std::fabs(x); const float c=(a>env)?atk:rel; env=c*env+(1.f-c)*a;
        const float thr=0.22f; float gr=1.f;
        if (env>thr){ const float over=env-thr; gr=1.f/(1.f+amount*5.0f*over); }
        return x*gr*(1.f+amount*0.30f);
    }
};

static const float kEqFreq[6] = { 100.f, 180.f, 340.f, 1300.f, 3600.f, 5000.f };

struct DustupCore {
    float sr = 96000.0f;
    rbtube::HP1 inHP;                 // input coupling (deep, bass-friendly)
    rbtube::TubeStage v1;             // ECC83 (12AX7) Valve Drive
    Biquad shapeLo, shapeMid, shapeHi;
    Biquad bqBass, bqMid, bqTreble;   // active Bass/Middle/Treble (sheet 2)
    Biquad eqBand[6];                 // 6-band graphic EQ
    Biquad subLp;                     // sub-harmonic low-pass shaping
    SubOct sub; OptoComp comp;
    rbtube::LP1 outLp;                // SS power-amp gentle top roll

    // params (0..1) + derived
    float pInput=.5f,pBass=.5f,pMid=.5f,pTreble=.5f,pValve=.2f,pSub=.4f,pComp=.4f,pOutput=.7f;
    float pEq[6]={.5f,.5f,.5f,.5f,.5f,.5f};
    bool active=false, shape=false, eqIn=true, subOn=false, compOn=false;
    float inGain=1.f, drive=1.f, subLvl=0.f, compAmt=0.f, outLevel=1.f;
    float eqLin[6]={1,1,1,1,1,1};

    void setSampleRate(float s){ sr=s; recalc(); reset(); }

    void reset(){ inHP.reset(); v1.reset(); shapeLo.reset(); shapeMid.reset(); shapeHi.reset();
        bqBass.reset(); bqMid.reset(); bqTreble.reset(); subLp.reset(); sub.reset(); comp.reset(); outLp.reset();
        for (int i=0;i<6;++i) eqBand[i].reset(); }

    void setParams(const float* p){
        pInput=clamp01(p[0]); pBass=clamp01(p[1]); pMid=clamp01(p[2]); pTreble=clamp01(p[3]);
        pValve=clamp01(p[4]); pSub=clamp01(p[5]); pComp=clamp01(p[6]); pOutput=clamp01(p[7]);
        for (int i=0;i<6;++i) pEq[i]=clamp01(p[8+i]);
        active=p[14]>0.5f; shape=p[15]>0.5f; eqIn=p[16]>0.5f; subOn=p[17]>0.5f; compOn=p[18]>0.5f;
        recalc();
    }

    void recalc(){
        inHP.set(sr, 24.0f);                                   // deep input coupling (bass)
        // ECC83 V1 valve drive. Stiff-ish supply, gentle Hiwatt-like headroom; the
        // Valve knob and Input both push it from clean into grind.
        v1.set(sr, 1, 300.0f, 40.0f, 22.0f, 1500.0f);
        const float pad = active ? 0.45f : 1.0f;               // Active input pad (hotter basses)
        // Tamed ranges: the old wide ranges drove the ECC83 into collapse/clip at
        // max (level FELL when cranked) and left the real default (Valve 0.2) ~5 dB
        // under the family. Keep the tube in a sane window so level rises sanely.
        inGain = (1.4f + 1.2f * rbtube::PotTaper::audio(pInput, 1.30f)) * pad;
        drive  = 0.9f + 1.1f * rbtube::PotTaper::audio(pValve, 1.35f);    // clean -> grind

        // Shape: a fixed mid-scoop "smile" contour (push switch), bypassed when off.
        shapeMid.peak(sr, 500.0f, shape ? -9.0f : 0.f, 0.7f);
        if (shape){ shapeLo.lowShelf(sr, 80.0f, 4.0f); shapeHi.highShelf(sr, 3500.0f, 4.0f); }
        else { shapeLo.bypass(); shapeHi.bypass(); }

        // Active tone — sheet 2 corner freqs: Bass 45 Hz, Middle 660 Hz, Treble 5 kHz.
        bqBass.lowShelf(sr, 45.0f, (pBass-0.5f)*30.0f);
        bqMid.peak(sr, 660.0f, (pMid-0.5f)*24.0f, 0.7f);
        bqTreble.highShelf(sr, 5000.0f, (pTreble-0.5f)*30.0f);

        // 6-band graphic EQ, +/-12 dB each, switchable.
        for (int i=0;i<6;++i){
            const float dB = eqIn ? (pEq[i]-0.5f)*24.0f : 0.f;
            eqBand[i].peak(sr, kEqFreq[i], dB, 1.4f);
        }

        subLp.lowpass(sr, 120.0f, 0.7f);
        sub.setSR(sr); subLvl = pSub * 1.4f;
        comp.setSR(sr); compAmt = pComp;

        outLp.set(sr, 9000.0f);                                // SS power-amp top roll
        // Output level (master into the SS power amp) + family loudness makeup.
        outLevel = (pOutput / 0.7f) * std::pow(10.0f, 0.05f * (4.0f + 2.0f * pValve));
    }

    inline float process(float x){
        x = inHP.process(x);
        float s = v1.process(x * inGain * drive);              // ECC83 valve drive (clean->grind)
        s = shapeMid.process(s); s = shapeLo.process(s); s = shapeHi.process(s);  // Shape
        s = bqBass.process(s); s = bqMid.process(s); s = bqTreble.process(s);     // tone
        if (eqIn) for (int i=0;i<6;++i) s = eqBand[i].process(s);                  // graphic EQ
        if (subOn) s += subLp.process(sub.process(s, subLvl));                     // Sub-harmonics
        if (compOn) s = comp.process(s, compAmt);                                  // Comp
        s = outLp.process(s);                                                      // SS power top roll
        return s * outLevel;
    }
};

} // namespace dustup
#endif // DUSTUP_CORE_H
