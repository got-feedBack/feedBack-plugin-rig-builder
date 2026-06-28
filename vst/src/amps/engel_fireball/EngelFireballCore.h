#ifndef ENGEL_FIREBALL_CORE_H
#define ENGEL_FIREBALL_CORE_H
//
// EngelFireballCore — ENGL Fireball (E625, parody "Engel"), circuit-real OWN core.
// A 2-channel high-gain head: CLEAN (headroomy) + LEAD/ULTRA (a deep 4-stage ECC83
// cascade -> brutal, tight, modern-metal saturation). Built on the DualRectCore /
// Jcm800Core pattern (gain stages BEFORE the passive tone stack, driven recovery
// AFTER) so the lead saturates HARD (crest ~4-5, like the E650 lead reference)
// instead of the shared core's ~10% THD ceiling. Power = 2x 6L6GC (per schematic).
//
//   IN -> inCoupling -> V1 -> V2  [LEAD: -> V3 -> V4]  -> ENGL TMB tone stack
//      -> scoop -> Bright/Bottom/MidBoost voicing -> Presence -> V_recovery (driven)
//      -> 12AX7 LTP PI -> 2x 6L6GC -> OT roll -> top tilt -> 4x12 -> ·makeup.
//
// Schematic: amps/ENGL Fireball (EN-50)/engl-fireball-amplifier-schematic_new.pdf
// (ENGL 625): 5x ECC83 preamp, Ultra switch, tone stack Treble 250k/220pF, Bass 1M,
// Middle 20k, caps 22n/15n; 2x 6L6GC + Presence NFB.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace engel {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void highShelf(float sr,float f,float dB){ if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
    void lowShelf(float sr,float f,float dB){ if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} if(f<10)f=10; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)+(A-1)*c+t; b0=A*((A+1)-(A-1)*c+t)/a0; b1=2*A*((A-1)-(A+1)*c)/a0; b2=A*((A+1)-(A-1)*c-t)/a0; a1=-2*((A-1)+(A+1)*c)/a0; a2=((A+1)+(A-1)*c-t)/a0; }
    void peak(float sr,float f,float dB,float Q){ if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q);
        float a0=1+al/A; b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
    void lowpass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al;
        b0=(1-c)*0.5f/a0; b1=(1-c)/a0; b2=(1-c)*0.5f/a0; a1=-2*c/a0; a2=(1-al)/a0; }
    void highpass(float sr,float f,float Q){ if(f>sr*0.49f)f=sr*0.49f; float w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q),a0=1+al;
        b0=(1+c)*0.5f/a0; b1=-(1+c)/a0; b2=(1+c)*0.5f/a0; a1=-2*c/a0; a2=(1-al)/a0; }
};

struct EngelFireballCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v2, v3, v4, vR;
    Biquad scoop, brightSw, bottomSw, midBoostSw, presenceShelf, outTilt;
    Biquad cabHP, cabLowShelf, cabPresence, cabTopRoll;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AT7 pi;
    rbtube::PowerAmp6L6GC power;                        // 2x 6L6GC (per schematic)
    rbtube::LP1 otVoice;

    bool lead=true, bright=false, bottom=false, midBoost=false, cabOn=true;
    float pCleanGain=.4f, pLeadGain=.6f, pBass=.5f, pMid=.5f, pTreble=.6f, pPres=.5f, pLeadVol=.5f, pMaster=.6f;

    float g1=1,g2=1,g3=1,g4=1,gR=1,piDrive=6,outLevel=1;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void reset(){ inCoupling.reset(); v1.reset();v2.reset();v3.reset();v4.reset();vR.reset();
        scoop.reset();brightSw.reset();bottomSw.reset();midBoostSw.reset();presenceShelf.reset();outTilt.reset();
        cabHP.reset();cabLowShelf.reset();cabPresence.reset();cabTopRoll.reset();
        tone.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        // LEAD lows are present (+4 in the ref) but tight/percussive -> a moderate HP;
        // CLEAN fuller.
        inCoupling.set(sr, lead ? 95.0f : 120.0f);   // LEAD tight/percussive; CLEAN tight Fender-ish (ref lo cut)
        v1.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v2.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
        v3.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
        v4.set(sr, 1, 250.0f, 40.0f, 33.0f, 1500.0f);   // colder late stage -> the lead bite
        vR.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);   // recovery

        const float g = rbtube::PotTaper::audio(lead ? pLeadGain : pCleanGain, 1.30f);
        if (lead) {
            // Deep 4-stage cascade (the Ultra path) + a hard-driven recovery -> brutal
            // saturation (crest ~4-5). Drives sit BEFORE the tone stack (loses ~15 dB);
            // the recovery AFTER. Pushed harder than the Recto Red for the ENGL grind.
            g1 = 0.6f + 6.0f*g;
            g2 = 0.7f + 5.0f*g;
            g3 = 0.8f + 4.0f*g;
            g4 = 0.9f + 3.5f*g;
            gR = 1.3f + 5.0f*g;
        } else {
            // CLEAN: 2 gentle stages, stays headroomy.
            g1 = 0.30f + 1.7f*g;
            g2 = 0.40f + 1.3f*g;
            g3 = 1.0f; g4 = 1.0f;
            gR = 1.0f + 1.0f*g;
        }

        // ENGL tone stack (Yeh): Treble 250k/220pF, Bass 1M, Middle 20k, slope 33k,
        // Cb 22nF, Cm 15nF (per schematic — the code's old 250k/500pF/22n was wrong).
        tone.setComponents(250e3, 1e6, 20e3, 68e3, 220e-12, 22e-9, 15e-9);  // slope 68k -> less mid scoop (ref hm ~-3)
        tone.update(sr, pTreble, pMid, pBass);
        // A subtle upper-mid dip on LEAD (the ENGL edge); MidBoost fills it.
        scoop.peak(sr, 2000.0f, lead ? -1.0f : 0.0f, 1.0f);
        // Voicing switches (were DEAD): Bright = treble lift, Bottom = low boost,
        // MidBoost = mid push (counters the scoop / fattens leads).
        brightSw.highShelf(sr, 2600.0f, bright ? 4.0f : 0.0f);
        bottomSw.lowShelf(sr, 120.0f, bottom ? 4.5f : 0.0f);
        midBoostSw.peak(sr, 700.0f, midBoost ? 4.0f : 0.0f, 0.8f);
        presenceShelf.highShelf(sr, 3000.0f, (pPres-0.5f)*10.0f);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);
        const float vol = rbtube::PotTaper::audio(pMaster, 1.15f) * (0.7f + 0.6f*pLeadVol);
        // LEAD pushes the 6L6 power section into clipping too (the cranked-ENGL squash);
        // CLEAN keeps it headroomy.
        power.set(sr, (lead ? 1.3f : 0.5f) + (lead ? 3.6f : 2.0f)*vol, -36.0f, 0.06f, 30.0f, 11000.0f);
        power.out = 0.012f;
        otVoice.set(sr, lead ? 11000.0f : 14000.0f);
        outTilt.highShelf(sr, 2600.0f, lead ? 4.0f : 7.0f);

        // Fallback 4x12 (CabSim; ENGL V30-voiced 4x12 — the LEAD ref top is rolled ~-12 dB).
        cabHP.highpass(sr, 85.0f, 0.70f);
        cabLowShelf.lowShelf(sr, 240.0f, lead ? -2.5f : -7.0f);   // CLEAN tames the 1M-bass boom
        cabPresence.peak(sr, 2300.0f, 3.0f, 0.7f);                // fills the upper-mid (hm) toward the ref
        cabTopRoll.lowpass(sr, lead ? 4500.0f : 8500.0f, 0.70f);  // CLEAN brighter top

        // Loudness makeup -> ~-16 dBFS at the operating point; decreases with gain.
        const float mkBase = lead ? -1.5f : 18.0f;
        outLevel = std::pow(10.0f, 0.05f * (mkBase - 5.0f * (lead?pLeadGain:pCleanGain)));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1.process(x * g1);
        y = v2.process(y * g2);
        if (lead) { y = v3.process(y * g3); y = v4.process(y * g4); }
        y = tone.process(y);
        y = scoop.process(y);
        y = midBoostSw.process(y);
        y = bottomSw.process(y);
        y = brightSw.process(y);
        y = presenceShelf.process(y);
        y = vR.process(y * gR);
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        y = outTilt.process(y);
        if (cabOn) { y = cabHP.process(y); y = cabLowShelf.process(y); y = cabPresence.process(y); y = cabTopRoll.process(y); }
        return y * outLevel;
    }
};

} // namespace engel
#endif // ENGEL_FIREBALL_CORE_H
