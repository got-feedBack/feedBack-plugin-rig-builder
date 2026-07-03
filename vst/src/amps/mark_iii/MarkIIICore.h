#ifndef MARK_III_CORE_H
#define MARK_III_CORE_H
//
// MarkIIICore — Mesa/Boogie Mark III, circuit-real OWN core (the shared core drove
// ONE preamp stage → ~10% THD ceiling, far too clean for the MoP-era Mark III lead).
// Deep LEAD cascade (v1→v2→v3 driven + driven v4) so it saturates; low-gain 2-stage
// RHYTHM. Mark III Mesa tone stack (Treble 250k/250pF, Bass 250k/.047, Mid 10k, slope
// 120k) + a WIRED 5-band graphic EQ (80/240/750/2200/6600, the dead kEq params) + the
// pull-Bright switch. Mid-forward voice (NOT scooped). 12AX7 preamp, LTP PI, 6L6 power.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace markiii {

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
    void highPass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*Q);
        float a0=1+al; b0=((1+c)*0.5f)/a0; b1=(-(1+c))/a0; b2=((1+c)*0.5f)/a0; a1=(-2*c)/a0; a2=(1-al)/a0; }
};

struct MarkIIICore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v2, v3, v4;
    rbtube::ToneStackYeh tone;
    Biquad midVoice, brightShelf, postHp;
    Biquad eq[5];                          // 5-band graphic EQ @ 80/240/750/2200/6600
    rbtube::PhaseInverterLTP12AT7 pi;
    rbtube::PowerAmp6L6GC power;
    rbtube::LP1 otVoice;

    int ch=1;   // 0 Rhythm, 1 Lead
    float pGain=.55f, pTreble=.6f, pBass=.45f, pMid=.4f, pMaster=.5f;
    float pEq[5]={.6f,.5f,.38f,.5f,.58f}; bool pEqIn=true, pBright=false;
    float g1=1.f,g2=1.f,g3=1.f,g4=1.f,piDrive=6.f,outLevel=1.f; int nStages=2;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setChannel(int c){ ch=c; recalc(); }
    void setActive(float gain,float treble,float bass,float mid,float master){
        pGain=clamp01(gain); pTreble=clamp01(treble); pBass=clamp01(bass); pMid=clamp01(mid); pMaster=clamp01(master); recalc(); }
    void setEQ(float e80,float e240,float e750,float e2200,float e6600,bool eqIn){
        pEq[0]=clamp01(e80); pEq[1]=clamp01(e240); pEq[2]=clamp01(e750); pEq[3]=clamp01(e2200); pEq[4]=clamp01(e6600); pEqIn=eqIn; recalc(); }
    void setBright(bool b){ pBright=b; recalc(); }

    void reset(){ inCoupling.reset(); v1.reset(); v2.reset(); v3.reset(); v4.reset();
        tone.reset(); midVoice.reset(); brightShelf.reset(); postHp.reset(); for(int i=0;i<5;++i)eq[i].reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, ch==1 ? 180.0f : 130.0f);   // VERY tight lows — the Mark III's signature tight low end
        v1.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v2.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v3.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
        v4.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);

        const float gA = rbtube::PotTaper::audio(pGain, 1.30f);
        if (ch == 0) {           // RHYTHM — 2-stage, stays fairly clean (ref vol_10 crest ~15)
            g1 = 0.35f + 1.8f*gA; g2 = 0.45f + 1.5f*gA; g3 = 1.0f; g4 = 1.0f; nStages = 2;
        } else {                 // LEAD — deep cascade (the singing MoP lead)
            g1 = 0.55f + 3.2f*gA; g2 = 0.65f + 4.2f*gA; g3 = 0.75f + 3.2f*gA;
            g4 = 1.40f + 3.2f*gA; nStages = 3;
        }

        // Mark III Mesa tone stack (Yeh, DOUBLE-precision): Treble 250k/250pF, Bass 250k/.047,
        // Mid 10k (the Boogie scoop), slope 120k. PULL-BRIGHT adds the V1A bright cap (top lift).
        tone.setComponents(250e3, 250e3, 10e3, 120e3, 250e-12, 47e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);
        power.set(sr, 0.5f + 2.2f*rbtube::PotTaper::audio(pMaster,1.15f), -36.0f, 0.05f, 30.0f, 11000.0f);
        power.out = 0.012f;
        otVoice.set(sr, 13000.0f);

        // MID-FORWARD voice (the Mark III "cocked" lead peaks ~1.25-2.5k — NOT scooped). A gentle
        // presence peak + a moderate bright shelf (Lead more present than Rhythm). Bright pull adds top.
        postHp.highPass(sr, ch==1 ? 115.0f : 90.0f, 0.7f);   // 2-pole post-distortion HP: kill the regenerated lows (Mark III tight low end)
        midVoice.peak(sr, 1500.0f, ch==1 ? 3.0f : 2.0f, 0.9f);
        float brightDb = (ch==1 ? 3.5f : 1.5f) + (pBright?2.5f:0.0f);
        brightShelf.highShelf(sr, 3500.0f, brightDb);

        // 5-band graphic EQ (footswitchable): real centers, ±12 dB sliders (0.5 = flat).
        static const float fc[5] = {80.0f, 240.0f, 750.0f, 2200.0f, 6600.0f};
        for (int i=0;i<5;++i) eq[i].peak(sr, fc[i], pEqIn ? (pEq[i]-0.5f)*24.0f : 0.0f, 1.3f);

        const float base = (ch==1) ? 11.5f : 19.5f;
        outLevel = std::pow(10.0f, 0.05f * (base - 7.0f*pGain));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1.process(x * g1);
        y = v2.process(y * g2);
        if (nStages >= 3) y = v3.process(y * g3);
        y = tone.process(y);
        y = v4.process(y * g4);                    // recovery (Rhythm) / 4th gain stage (Lead)
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        y = postHp.process(y);                     // tight-lows HP (post-distortion)
        // graphic EQ loop (post-preamp) + voicing
        for (int i=0;i<5;++i) y = eq[i].process(y);
        y = midVoice.process(y);
        y = brightShelf.process(y);
        return y * outLevel;
    }
};

} // namespace markiii
#endif // MARK_III_CORE_H
