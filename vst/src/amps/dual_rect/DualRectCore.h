#ifndef DUAL_RECT_CORE_H
#define DUAL_RECT_CORE_H
//
// DualRectCore — Mesa/Boogie 3-Channel Dual Rectifier, circuit-real on the shared
// tube_stage.hpp framework with CONTROLLED gain staging per channel. Rewritten
// from the over-gained 6-stage cascade that saturated every signal to ~100% THD.
//
//   Green (clean) -> low-gain 12AX7 path (cleans up like a Fender clean).
//   Orange (crunch) / Red (high gain) -> cascaded 12AX7 stages, more drive, but
//   tube-shaped + makeup so it's a musical Recto roar (not a static square wave).
//   Shared Mesa tone stack + 12AX7 LTP PI + 4x 6L6GC. Runs 2x oversampled.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace dualrect {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void peak(float sr,float f,float dB,float Q){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q);
        float a0=1+al/A; b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
};

struct DualRectCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v2, v3, v4;          // cascade (later stages only used on Orange/Red)
    Biquad midScoop, presenceShelf;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AT7 pi;
    rbtube::PowerAmp6L6GC power;
    rbtube::LP1 otVoice;

    // active-channel params (selected from the 3 channels by kChannel)
    int ch=2;  // 0 Green, 1 Orange, 2 Red
    float pGain=.7f,pTreble=.6f,pMid=.4f,pBass=.55f,pPres=.5f,pMaster=.55f,pOutput=.6f,pRect=1.f,pMode=1.f;
    float g1=1.f,g2=1.f,g3=1.f,g4=1.f,piDrive=6.f,outLevel=1.f; int nStages=2;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }

    // The plugin passes the full param array + resolves the active channel.
    void setChannel(int c){ ch=c; recalc(); }
    void setMode(float m){ pMode=clamp01(m); recalc(); }   // Green Clean(0)/Pushed(1); Orange/Red Raw(0)/Vintage(.5)/Modern(1)
    void setActive(float gain,float treble,float mid,float bass,float pres,float master,float output,float rect){
        pGain=clamp01(gain); pTreble=clamp01(treble); pMid=clamp01(mid); pBass=clamp01(bass);
        pPres=clamp01(pres); pMaster=clamp01(master); pOutput=clamp01(output); pRect=clamp01(rect); recalc(); }

    void reset(){ inCoupling.reset(); v1.reset(); v2.reset(); v3.reset(); v4.reset();
        midScoop.reset(); presenceShelf.reset(); tone.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, 70.0f);   // tighten lows pre-distortion to match the tight amp-only Recto reference (was 30 Hz = too full)
        v1.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v2.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v3.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
        v4.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);

        const float gA = rbtube::PotTaper::audio(pGain, 1.30f);
        // Per-channel gain staging + stage count (Green clean, Red high-gain roar).
        // modeGain: Green Clean->Pushed adds breakup; Orange/Red Raw(loose/clean) -> Modern(hottest).
        float modeGain;
        if (ch == 0) {            // GREEN clean (g4 = unity recovery)
            g1 = 0.3f + 1.8f*gA; g2 = 0.4f + 1.2f*gA; g3 = 1.0f; g4 = 1.0f; nStages = 2;
            modeGain = 1.0f + 0.8f*pMode;
        } else if (ch == 1) {     // ORANGE crunch (g4 = unity recovery)
            g1 = 0.4f + 2.8f*gA; g2 = 0.5f + 2.0f*gA; g3 = 0.6f + 1.4f*gA; g4 = 1.0f; nStages = 3;
            modeGain = 0.7f + 0.3f*pMode;
        } else {                  // RED — THE heavy-metal channel: deep 4-stage cascade (drives v4 too)
            g1 = 0.6f + 6.5f*gA; g2 = 0.7f + 5.0f*gA; g3 = 0.8f + 3.5f*gA;
            g4 = 1.3f + 4.5f*gA; nStages = 3;   // 4th driven stage = the Recto sustain/aggression (real = 5 stages)
            modeGain = 0.7f + 0.3f*pMode;        // Raw looser, Modern hottest
        }
        g1 *= modeGain; g2 *= modeGain; g3 *= modeGain; g4 *= modeGain;

        // Per-channel tone stack (circuit-real, Yeh DOUBLE-precision — float NaNs at 192k).
        // Green (CH1): 250k/250k/25k, slope 150k, 250pF/.1/.047. Orange+Red (CH2/CH3) share the
        // tighter stack: Bass pot 25k (NOT 250k) + slope 47k + 500pF/.02/.02 = the real Recto low end.
        if (ch == 0)  tone.setComponents(250e3, 250e3, 25e3, 150e3, 250e-12, 100e-9, 47e-9);
        else          tone.setComponents(250e3,  25e3, 25e3,  47e3, 500e-12,  20e-9, 20e-9);
        tone.update(sr, pTreble, pMid, pBass);
        // Recto "Modern" mid-scoop — scales with MODE on the high-gain channels (Raw=flat, Modern=full −5dB).
        midScoop.peak(sr, 650.0f, (ch>=1)? -5.0f*pMode : 0.0f, 0.8f);
        presenceShelf.highShelf(sr, 3000.0f, (pPres-0.5f)*10.0f);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);
        // Rectifier: Spongy(0)=more sag/lower, Bold(1)=tight/higher.
        const float vol = rbtube::PotTaper::audio(pMaster, 1.15f);
        power.set(sr, 0.5f + 2.2f*vol, -36.0f, 0.05f + 0.04f*(1.0f-pRect), 30.0f, 11000.0f);
        power.out = 0.012f;
        otVoice.set(sr, 14000.0f);   // open the top to match the bright amp-only reference (was 9k = too dark)

        // Makeup decreases with gain so the Gain knob is drive, not volume. The
        // master Output sets level on top.
        const float base = (ch==0)?13.5f : (ch==1)?13.0f : 12.5f;
        outLevel = (0.4f + pOutput) * std::pow(10.0f, 0.05f * (base - 7.0f * pGain));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1.process(x * g1);
        y = v2.process(y * g2);
        if (nStages >= 3) y = v3.process(y * g3);
        y = tone.process(y);
        y = midScoop.process(y);
        y = presenceShelf.process(y);
        y = v4.process(y * g4);            // recovery (Green/Orange) / 4th gain stage (Red metal)
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        return y * outLevel;
    }
};

} // namespace dualrect
#endif // DUAL_RECT_CORE_H
