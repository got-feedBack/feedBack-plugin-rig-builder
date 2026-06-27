#ifndef MARK_II_CORE_H
#define MARK_II_CORE_H
//
// MarkIICore — Mesa/Boogie Mark IIB, circuit-real OWN core (the shared core only
// drove ONE preamp stage → ~10% THD ceiling, far too clean for a Mark lead). Here
// the LEAD channel is a deep cascade (v1→v2→v3 driven + a driven v4 recovery) so it
// saturates like a real Mark IIC+ lead; the RHYTHM channel stays a low-gain 2-stage
// path. Real Mesa tone stack (Treble 250k/250pF, Bass 250k/.1, Mid 10k/.047, slope
// 100k) + a baked graphic-EQ voicing + the panel PULL switches (Bright, Shift, Gain
// Boost, Bright Lead, Half Power). 12AX7 preamp, LTP PI, 4× 6L6GC. 2x oversampled.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace markii {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void peak(float sr,float f,float dB,float Q){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*Q);
        float a0=1+al/A; b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
};

struct MarkIICore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v2, v3, v4;
    rbtube::ToneStackYeh tone;
    Biquad voiceTilt, brightShelf, presenceShelf;
    rbtube::PhaseInverterLTP12AT7 pi;
    rbtube::PowerAmp6L6GC power;
    rbtube::LP1 otVoice;

    int ch=1;   // 0 Rhythm, 1 Lead
    float pGain=.7f, pTreble=.6f, pBass=.5f, pMid=.5f, pMaster=.6f;
    bool pBright=false, pShift=false, pGainBoost=false, pBrightLead=false, pHalfPower=false;
    float g1=1.f,g2=1.f,g3=1.f,g4=1.f,piDrive=6.f,outLevel=1.f; int nStages=2;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setChannel(int c){ ch=c; recalc(); }
    void setActive(float gain,float treble,float bass,float mid,float master){
        pGain=clamp01(gain); pTreble=clamp01(treble); pBass=clamp01(bass); pMid=clamp01(mid); pMaster=clamp01(master); recalc(); }
    void setSwitches(bool bright,bool shift,bool gboost,bool brightLead,bool halfPower){
        pBright=bright; pShift=shift; pGainBoost=gboost; pBrightLead=brightLead; pHalfPower=halfPower; recalc(); }

    void reset(){ inCoupling.reset(); v1.reset(); v2.reset(); v3.reset(); v4.reset();
        tone.reset(); voiceTilt.reset(); brightShelf.reset(); presenceShelf.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, ch==1 ? 75.0f : 45.0f);   // Lead tight; Rhythm fuller lows (per the refs)
        v1.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v2.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v3.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
        v4.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);

        const float gA = rbtube::PotTaper::audio(pGain, 1.30f);
        const float gboost = pGainBoost ? 1.35f : 1.0f;   // GAIN BOOST pull = extra lead preamp gain (.005µF inject)
        if (ch == 0) {           // RHYTHM — 2-stage, cleans up low but breaks up toward full Volume
            g1 = 0.40f + 2.6f*gA; g2 = 0.50f + 2.1f*gA; g3 = 1.0f; g4 = 1.0f; nStages = 2;
        } else {                 // LEAD — deep cascade (drives v2/v3 + a driven v4): liquid Mark saturation
            g1 = 0.50f + 3.0f*gA; g2 = (0.60f + 4.0f*gA)*gboost; g3 = 0.70f + 3.0f*gA;
            g4 = 1.30f + 3.0f*gA; nStages = 3;
        }

        // Mesa Mark IIB tone stack (Yeh, DOUBLE-precision — float NaNs @192k): Treble 250k/250pF,
        // Bass 250k/.1, Mid 10k/.047 (the Boogie scoop), slope 100k. SHIFT pull adds a 750pF ∥
        // treble cap (brighter/treble-shifted) — modelled as a brighter treble setting.
        tone.setComponents(250e3, 250e3, 10e3, 100e3, 250e-12, 100e-9, 47e-9);
        tone.update(sr, clamp01(pTreble + (pShift?0.12f:0.0f)), pMid, pBass);

        presenceShelf.highShelf(sr, 3000.0f, 0.0f);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);
        // HALF POWER pull = 100W → 60W (drop the outer pair): less drive + a touch more compression.
        const float pw = pHalfPower ? 0.72f : 1.0f;
        power.set(sr, (0.5f + 2.2f*rbtube::PotTaper::audio(pMaster,1.15f))*pw, -36.0f, 0.05f + (pHalfPower?0.03f:0.0f), 30.0f, 11000.0f);
        power.out = 0.012f;
        otVoice.set(sr, 14000.0f);

        // Baked voicing to the amp-only reference (markii_leaddrive/vol): the Mark is BRIGHT with a
        // gentle mid tilt (NOT a deep scoop). Flatten the core's 1.25k hump + a strong bright shelf
        // (Lead is brighter/more present than Rhythm). Bright / Bright-Lead pulls add top.
        voiceTilt.peak(sr, 1250.0f, ch==1 ? -2.5f : -4.5f, 1.0f);   // flatten the core's 1.25k hump (deeper on the cleaner Rhythm)
        float brightDb = (ch==1 ? 4.5f : 2.0f) + (pBright?2.0f:0.0f) + ((pBrightLead && ch==1)?2.5f:0.0f);
        brightShelf.highShelf(sr, 3500.0f, brightDb);

        // Makeup decreases with gain so the Gain/Lead-Drive knob is drive, not volume (~-16 dBFS).
        const float base = (ch==1) ? 10.0f : 15.0f;
        outLevel = std::pow(10.0f, 0.05f * (base - 7.0f*pGain));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1.process(x * g1);
        y = v2.process(y * g2);
        if (nStages >= 3) y = v3.process(y * g3);
        y = tone.process(y);
        y = presenceShelf.process(y);
        y = v4.process(y * g4);                    // recovery (Rhythm) / 4th gain stage (Lead)
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        y = voiceTilt.process(y);                  // graphic-EQ voicing (post-power, like the EQ loop)
        y = brightShelf.process(y);                // bright top
        return y * outLevel;
    }
};

} // namespace markii
#endif // MARK_II_CORE_H
