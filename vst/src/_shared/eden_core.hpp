#ifndef RB_EDEN_CORE_HPP
#define RB_EDEN_CORE_HPP
//
// EdenCore — shared circuit-real model of the Eden WT-series "Valve-Tech" hybrid
// bass preamp, used by all three heads (WT-300 / WT-550 / WT-800C). Built on the
// shared tube_stage.hpp framework. Schematics: David Eden WT-300 preamp (2-19-93),
// WT-550 preamp ("WT404", 2004), WT-800C preamp (D. Grout).
//
//   IN -> input coupling -> 12AX7 input valve ("Valve-Tech" warmth) -> Eden opto
//   compressor (gentle, internal) -> Enhance contour (the Eden signature mid-scoop
//   + low/high lift) -> Bass shelf -> 3-band SEMI-PARAMETRIC EQ (sweepable Low
//   30-300 / Mid 200-2k / High 1.2-12k, each Freq + Level) -> Treble shelf ->
//   Master -> output limiter -> SS power amp (clean, big headroom) -> out.
//
//   The THREE heads share this exact preamp; they differ only in POWER/headroom
//   (WT-300 ~300 W, WT-550 ~550 W, WT-800C 2x400 W) and the WT-800C adds a bi-amp
//   CROSSOVER + BALANCE (here folded back to one mono output as a low/high tilt at
//   the crossover frequency; Bridge-mono is irrelevant to a single output). The
//   model enum sets a subtle low-end extension to match the bigger power sections.
//
//   Hybrid amp: only the ONE 12AX7 input uses the framework table (so it gets the
//   shared softTableLimit — no hard table-edge "8-bit"); everything else is op-amp
//   / solid-state, modelled with clean biquads. The chain runs 2x oversampled.
//
#include "tube_stage.hpp"
#include <cmath>

namespace eden {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

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
    void highpass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q);
        float a0=1+al; b0=(1+c)*0.5f/a0; b1=-(1+c)/a0; b2=(1+c)*0.5f/a0; a1=-2*c/a0; a2=(1-al)/a0; }
};

// Eden internal opto compressor — gentle, always-on smoothing (no front-panel knob).
struct OptoComp {
    float env=0, atk=0, rel=0;
    void setSR(float fs){ atk=std::exp(-1.f/(0.006f*fs)); rel=std::exp(-1.f/(0.180f*fs)); }
    void reset(){ env=0; }
    inline float process(float x){
        const float a=std::fabs(x); const float c=(a>env)?atk:rel; env=c*env+(1.f-c)*a;
        const float thr=0.20f; float gr=1.f;
        if (env>thr){ const float over=env-thr; gr=1.f/(1.f+3.8f*over); }
        return x*gr;
    }
};

struct EdenCore {
    enum Model { WT300=0, WT550=1, WT800=2 };
    int model = WT300;
    float sr = 96000.0f;

    rbtube::HP1 inHP;
    rbtube::TubeStage v1;             // 12AX7 input valve
    OptoComp comp;
    Biquad enhScoop, enhLo, enhHi;    // Enhance contour
    Biquad bqBass, bqTreble;          // Bass / Treble shelves
    Biquad band[3];                   // 3-band semi-parametric EQ
    Biquad xoLow, xoHigh;             // WT-800 bi-amp crossover (folded to mono tilt)
    rbtube::LP1 outLp;                // SS power-amp top roll

    // band sweep ranges (Hz)
    const float bandLo[3] = { 30.f, 200.f, 1200.f };
    const float bandHi[3] = { 300.f, 2000.f, 12000.f };

    // params (0..1)
    float pGain=.5f,pEnh=.3f,pBass=.5f,pTreble=.5f,pMaster=.7f;
    float pF[3]={.5f,.5f,.5f}, pL[3]={.5f,.5f,.5f};
    float pXoFreq=.5f, pBalance=.5f; bool xoverOn=false;
    float inGain=1.f, outLevel=1.f;

    void setModel(int m){ model=m; }
    void setSampleRate(float s){ sr=s; recalc(); reset(); }

    void reset(){ inHP.reset(); v1.reset(); comp.reset(); enhScoop.reset(); enhLo.reset(); enhHi.reset();
        bqBass.reset(); bqTreble.reset(); for(int i=0;i<3;++i) band[i].reset(); xoLow.reset(); xoHigh.reset(); outLp.reset(); }

    // n = param count (11 for WT-300/550, 14 for WT-800).
    void setParams(const float* p, int n){
        pGain=clamp01(p[0]); pEnh=clamp01(p[1]); pBass=clamp01(p[2]);
        pF[0]=clamp01(p[3]); pL[0]=clamp01(p[4]);
        pF[1]=clamp01(p[5]); pL[1]=clamp01(p[6]);
        pF[2]=clamp01(p[7]); pL[2]=clamp01(p[8]);
        pTreble=clamp01(p[9]); pMaster=clamp01(p[10]);
        if (n>=14){ pXoFreq=clamp01(p[11]); pBalance=clamp01(p[12]); xoverOn=p[13]>0.5f; }
        else { xoverOn=false; }
        recalc();
    }

    void recalc(){
        // Bigger power section -> deeper low-end extension (subtle, the real audible
        // difference between the otherwise-identical preamps).
        const float inHz = (model==WT800)?20.0f : (model==WT550)?24.0f : 28.0f;
        inHP.set(sr, inHz);
        v1.set(sr, 1, 250.0f, 24.0f, 22.0f, 1500.0f);          // 12AX7 input valve
        comp.setSR(sr);
        // Gain drives the input valve. Eden is hi-fi/clean until pushed.
        inGain = 1.5f + 5.0f * rbtube::PotTaper::audio(pGain, 1.30f);

        // Enhance: the signature contour — scoop ~600 Hz, lift lows + highs, depth ~ knob.
        const float e = pEnh;
        enhScoop.peak(sr, 600.0f, -12.0f*e, 0.8f);
        enhLo.lowShelf(sr, 90.0f, 5.0f*e);
        enhHi.highShelf(sr, 4000.0f, 5.0f*e);

        // Bass / Treble shelves (sheet: Bass C19 .033, Treble C20 470p/C21 150p).
        bqBass.lowShelf(sr, 50.0f, (pBass-0.5f)*28.0f);
        bqTreble.highShelf(sr, 4000.0f, (pTreble-0.5f)*28.0f);

        // 3-band semi-parametric EQ: fc = lo*(hi/lo)^freq, +/-15 dB by level.
        for (int i=0;i<3;++i){
            const float fc = bandLo[i] * std::pow(bandHi[i]/bandLo[i], pF[i]);
            band[i].peak(sr, fc, (pL[i]-0.5f)*30.0f, 0.9f);
        }

        // WT-800 bi-amp crossover folded to a mono low/high tilt at the X-over freq.
        if (xoverOn){
            const float xf = 100.0f * std::pow(50.0f, pXoFreq);   // 100 Hz .. 5 kHz
            xoLow.lowpass(sr, xf, 0.707f);
            xoHigh.highpass(sr, xf, 0.707f);
        }

        outLp.set(sr, 9000.0f);
        outLevel = (pMaster / 0.7f) * std::pow(10.0f, 0.05f * (8.0f + 3.0f * pGain));
    }

    inline float process(float x){
        x = inHP.process(x);
        float s = v1.process(x * inGain);          // 12AX7 input valve
        s = comp.process(s);                        // Eden opto comp
        s = enhScoop.process(s); s = enhLo.process(s); s = enhHi.process(s);   // Enhance
        s = bqBass.process(s);
        for (int i=0;i<3;++i) s = band[i].process(s);                          // semi-param EQ
        s = bqTreble.process(s);
        if (xoverOn){                               // mono low/high balance tilt
            const float lo = xoLow.process(s), hi = xoHigh.process(s);
            const float bl = pBalance;              // 0=all lows .. 1=all highs
            s = lo * (2.0f*(1.0f-bl)) + hi * (2.0f*bl);
        }
        s = outLp.process(s);
        return s * outLevel;
    }
};

} // namespace eden
#endif // RB_EDEN_CORE_HPP
