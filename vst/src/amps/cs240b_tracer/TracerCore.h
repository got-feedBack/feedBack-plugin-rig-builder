#ifndef TRACER_CORE_H
#define TRACER_CORE_H
//
// TracerCore — Trace Elliot V-Type V8 (400W all-valve bass head), circuit-real on
// the shared tube_stage.hpp framework. Schematic: Trace Elliot SM00080 (preamp
// cd0120x1 + 400W valve power cd0120x1), all-ECC83 preamp + 8x KT88 push-pull.
//
//   IN (Passive/Active) -> input coupling (47n/1M -> deep lows, bass amp)
//      -> V1 ECC83 -> ·Gain I (+Bright) -> V2 ECC83 -> ·Gain II (+Pull)
//      -> FET feedback COMPRESSOR (J112, +Comp Level, on/off)
//      -> Trace passive tone stack: pre-shape scoop + Bass(+Deep) / Middle(+Shift
//         cap-swap) / Treble
//      -> V3 ECC83 recovery -> ·Level -> V6 ECC83 long-tail-pair phase inverter
//      -> V7 ECC83 cathode-follower drivers (~unity, folded into the PI->power drive)
//      -> ·Master -> 8x KT88 push-pull (B+ ~660V, fixed -60V bias, ~400W, huge
//         clean headroom) -> output-transformer voicing -> out
//
// KT88 power tube: real Koren table koren_kt88_ftube.h (mu8.16/ex1.35/kg1 890/
// kp32.4/kvb16.6) generated from the KT88 datasheet, NOT the 6550 proxy. Trace tone
// stack is its own passive 3-band (NOT a Fender TMB) with the signature mid-scoop
// pre-shape baked in. Values from the V8 service manual; runs oversampled 2x.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace tracer {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// RBJ biquad (peaking / shelves), denormal-flushed via rbtube::dn.
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void bypass(){ b0=1;b1=b2=a1=a2=0; }
    void peaking(float sr,float f,float Q,float dB){ if(f>sr*0.49f)f=sr*0.49f;
        float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*Q),a0=1+al/A;
        b0=(1+al*A)/a0; b1=(-2*c)/a0; b2=(1-al*A)/a0; a1=(-2*c)/a0; a2=(1-al/A)/a0; }
    void lowShelf(float sr,float f,float dB){ float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/2*std::sqrt((A+1/A)+2),rA=std::sqrt(A);
        float a0=(A+1)+(A-1)*c+2*rA*al; b0=A*((A+1)-(A-1)*c+2*rA*al)/a0; b1=2*A*((A-1)-(A+1)*c)/a0; b2=A*((A+1)-(A-1)*c-2*rA*al)/a0;
        a1=-2*((A-1)+(A+1)*c)/a0; a2=((A+1)+(A-1)*c-2*rA*al)/a0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/2*std::sqrt((A+1/A)+2),rA=std::sqrt(A);
        float a0=(A+1)-(A-1)*c+2*rA*al; b0=A*((A+1)+(A-1)*c+2*rA*al)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-2*rA*al)/a0;
        a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-2*rA*al)/a0; }
};

// FET feedback compressor (J112) — program-dependent, single Comp Level control.
struct FetComp {
    float env=0.f, atkC=0.f, relC=0.f, thr=0.10f, amount=0.f;
    void set(float sr, float amt){ amount=clamp01(amt);
        atkC=std::exp(-1.0f/(0.004f*sr)); relC=std::exp(-1.0f/(0.13f*sr)); }
    void reset(){ env=0.f; }
    inline float process(float x){
        const float a=std::fabs(x);
        env = (a>env) ? atkC*env+(1.f-atkC)*a : relC*env+(1.f-relC)*a;
        float gr=1.f;
        if(env>thr){ const float over=env/thr; gr=std::pow(over, -(0.62f*amount)); }  // up to ~3:1
        return x * gr * (1.0f + 0.7f*amount);                                          // makeup
    }
};

struct TracerCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;              // 47n/1M input coupling (deep lows)
    rbtube::TubeStage v1, v2, v3;        // ECC83 input / gain II / recovery stages
    Biquad brightBq;                    // Bright pull (treble lift around Gain I)
    FetComp comp;                       // built-in FET compressor
    Biquad preShape, bassBq, midBq, trebleBq;  // Trace passive tone stack
    rbtube::PhaseInverterLTP12AX7 pi;   // V6 ECC83 long-tail-pair phase inverter
    rbtube::PowerAmpKT88 power;         // 8x KT88 push-pull (~400W, stiff supply)
    rbtube::LP1 otVoice;                // gentle output-transformer top roll (pre-cab)

    // params (0..1)
    float pGain1=0.5f, pGain2=0.4f, pLevel=0.6f, pBass=0.5f, pMid=0.5f, pTreble=0.5f, pComp=0.4f, pMaster=0.7f;
    bool  pActive=false, pBright=false, pGain2Pull=false, pDeep=false, pMidShift=false, pCompOn=false;
    float inScale=1.f, g1=1.f, g2=1.f, level=1.f, piDrive=6.f, outLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setGain1(float v){ pGain1=clamp01(v); recalc(); }
    void setGain2(float v){ pGain2=clamp01(v); recalc(); }
    void setLevel(float v){ pLevel=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setComp(float v){ pComp=clamp01(v); recalc(); }
    void setMaster(float v){ pMaster=clamp01(v); recalc(); }
    void setActive(bool b){ pActive=b; recalc(); }
    void setBright(bool b){ pBright=b; recalc(); }
    void setGain2Pull(bool b){ pGain2Pull=b; recalc(); }
    void setDeep(bool b){ pDeep=b; recalc(); }
    void setMidShift(bool b){ pMidShift=b; recalc(); }
    void setCompOn(bool b){ pCompOn=b; recalc(); }

    void reset(){ inCoupling.reset(); v1.reset(); v2.reset(); v3.reset();
        brightBq.reset(); comp.reset(); preShape.reset(); bassBq.reset(); midBq.reset();
        trebleBq.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, 4.0f);                         // 47n/1M -> deep lows (bass amp)
        // ECC83 preamp stages off the ~270V HT rail.
        v1.set(sr, 1, 270.0f, 40.0f, 16.0f, 1500.0f);     // V1 input
        v2.set(sr, 1, 270.0f, 40.0f, 26.0f, 1500.0f);     // V2 (Gain II stage)
        v3.set(sr, 1, 270.0f, 40.0f, 45.0f, 1500.0f);     // V3 recovery

        // Gains: Gain I (RV1 1M log) + Gain II (RV2 1M log, +Pull = more gain).
        const float gA1 = rbtube::PotTaper::audio(pGain1, 1.40f);
        const float gA2 = rbtube::PotTaper::audio(pGain2, 1.40f);
        inScale = (pActive ? 0.5f : 1.0f) * 0.9f;         // Active jack pads ~-6 dB
        g1 = 0.4f + 3.2f * gA1;                           // V1 -> Gain I
        g2 = (0.5f + 3.0f * gA2) * (pGain2Pull ? 1.8f : 1.0f);  // Gain II (+Pull boost)
        // Bright pull: treble lift around Gain I.
        if (pBright) brightBq.highShelf(sr, 2200.0f, 6.0f); else brightBq.bypass();

        // FET compressor.
        comp.set(sr, pComp);

        // ── Trace passive tone stack (NOT a Fender TMB) ──
        // Fixed pre-shape: the signature Trace mid-scoop "smile" (gentle, always on).
        preShape.peaking(sr, 500.0f, 0.8f, -3.5f);
        // Bass: low shelf ~80 Hz; DEEP pull drops the corner + adds depth.
        bassBq.lowShelf(sr, pDeep ? 55.0f : 80.0f, (pBass-0.5f)*28.0f + (pDeep ? 3.0f : 0.0f));
        // Middle: peaking; MID SHIFT swaps the cap (25k x {47n,4n7}) -> low vs high centre.
        midBq.peaking(sr, pMidShift ? 1400.0f : 380.0f, 0.8f, (pMid-0.5f)*24.0f);
        // Treble: high shelf ~3 kHz.
        trebleBq.highShelf(sr, 3000.0f, (pTreble-0.5f)*28.0f);

        // Level (RV3 100k) -> drive into the recovery/PI.
        level = 0.3f + 1.6f * rbtube::PotTaper::audio(pLevel, 1.20f);
        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);

        // 8x KT88 push-pull. V8 = very stiff supply (LOW sag), fixed bias, low OT
        // high-pass so the fundamental survives, gentle OT LP. Master (RV7 50k lin)
        // sets the power-amp drive (the V8's huge clean headroom).
        const float master = rbtube::PotTaper::audio(pMaster, 1.10f);
        power.set(sr, 0.6f + 2.6f*master, -43.5f, 0.06f, 26.0f, 9000.0f);
        power.out = 0.0050f;                              // KT88 plate-volt differential -> signal
        power.biasShift = 1.8f;
        otVoice.set(sr, 7500.0f);

        // Loudness makeup — gain-dependent (designed from the harness sweep). Tuned in
        // calibrate_amp_core.py to the amp-family reference.
        const float drv = (pGain1 > pGain2 ? pGain1 : pGain2);
        outLevel = std::pow(10.0f, 0.05f * (14.0f + 11.0f * drv));
    }

    inline float process(float x){
        x = inCoupling.process(x * inScale);
        x = v1.process(x * g1);                           // V1 + Gain I
        if (pBright) x = brightBq.process(x);             // Bright pull
        x = v2.process(x * g2);                           // V2 + Gain II (+Pull)
        if (pCompOn) x = comp.process(x);                 // FET compressor
        x = preShape.process(x);                          // Trace pre-shape scoop
        x = bassBq.process(x); x = midBq.process(x); x = trebleBq.process(x);
        x = v3.process(x * level);                        // recovery + Level
        x = pi.process(x * piDrive);                      // V6 LTP PI
        x = power.process(x);                             // 8x KT88 PP
        x = otVoice.process(x);
        return x * outLevel;
    }
};

} // namespace tracer
#endif // TRACER_CORE_H
