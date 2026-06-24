#ifndef LOVOLT_CORE_H
#define LOVOLT_CORE_H
//
// LovoltCore — Custom Hiwatt 100 (DR103) all-tube head, BASS voicing, circuit-real
// on the shared tube_stage.hpp framework. Schematic: HiWatt DR103 (DR103_Complete).
// Same circuit as the guitar dr103_lovolt, bass-adapted (deep lows, no guitar cab).
//
//   IN -> input coupling (deep, bass) -> NORMAL + BRILLIANT 12AX7 channels (blended)
//      -> Hiwatt passive tone stack (Yeh: Treble 250k, Bass 500k, Middle 100k, slope
//         56k; 250pF/22nF/22nF) -> Presence shelf -> V3 12AX7 recovery
//      -> 12AT7 LTP phase inverter -> 4x EL34 push-pull (~100W, big clean Hiwatt
//         headroom) -> OT voicing -> ·Master makeup -> out
//
// Rewritten 2026-06-24 to the SAME clean framework pattern as CitrusCore/RumbleCore
// (which sound great) — the previous build ported the guitar Dr103's heavy nodal
// MultiNodeBPlus supply + Miller + load-feedback loop, which sounded harsh ("8-bit")
// in-app. This drops that complexity for the robust TubeStage + PowerAmpEL34 path.
// Runs oversampled (LovoltPlugin wraps it 2x).
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace lovolt {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void bypass(){ b0=1;b1=b2=a1=a2=0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/2*std::sqrt((A+1/A)+2),rA=std::sqrt(A);
        float a0=(A+1)-(A-1)*c+2*rA*al; b0=A*((A+1)+(A-1)*c+2*rA*al)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-2*rA*al)/a0;
        a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-2*rA*al)/a0; }
};

struct LovoltCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;              // deep lows (bass)
    rbtube::TubeStage vN, vB, v3;       // NORMAL + BRILLIANT 12AX7 channels + recovery
    Biquad brightShelf;                 // Brilliant channel bright cap
    rbtube::ToneStackYeh tone;          // Hiwatt TMB (Yeh)
    Biquad presenceShelf;               // Presence (power-amp NFB top air)
    rbtube::PhaseInverterLTP12AT7 pi;   // ECC81 12AT7 LTP phase inverter
    rbtube::PowerAmpEL34 power;         // 4x EL34 (~100W)
    rbtube::LP1 otVoice;               // OT top roll (pre-cab)

    // params (0..1)
    float pNormalVol=0.5f, pBrightVol=0.3f, pBass=0.5f, pTreble=0.5f, pMid=0.5f, pPres=0.4f, pMaster=0.7f;
    float gN=1.f, gB=1.f, piDrive=6.f, outLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setNormalVol(float v){ pNormalVol=clamp01(v); recalc(); }
    void setBrightVol(float v){ pBrightVol=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setMaster(float v){ pMaster=clamp01(v); recalc(); }

    void reset(){ inCoupling.reset(); vN.reset(); vB.reset(); v3.reset();
        brightShelf.reset(); tone.reset(); presenceShelf.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, 26.0f);                       // deep but tight (bass-friendly)
        vN.set(sr, 1, 250.0f, 40.0f, 22.0f, 1500.0f);    // NORMAL channel 12AX7
        vB.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);    // BRILLIANT channel 12AX7
        v3.set(sr, 1, 250.0f, 40.0f, 40.0f, 1500.0f);    // recovery 12AX7

        // Channel volumes = drive (Hiwatt non-master volumes, audio taper). Both
        // channels jumpered (bass slot). Brilliant gets a bright shelf.
        gN = 0.4f + 2.8f * rbtube::PotTaper::audio(pNormalVol, 1.30f);
        gB = 0.5f + 3.2f * rbtube::PotTaper::audio(pBrightVol, 1.30f);
        brightShelf.highShelf(sr, 2200.0f, 5.0f);

        // Hiwatt tone stack (Yeh): Treble 250k/250pF, Bass 500k/22nF, Mid 100k/22nF, slope 56k.
        tone.setComponents(250e3, 500e3, 100e3, 56e3, 250e-12, 22e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);
        // Presence (power-amp NFB top air): off..+ as the knob rises.
        presenceShelf.highShelf(sr, 3000.0f, (pPres-0.4f)*12.0f);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);

        // 4x EL34 (~100W). Hiwatt = STIFF supply (clean & loud, breaks up only when
        // Master is cranked). Low OT HP keeps the bass.
        const float master = rbtube::PotTaper::audio(pMaster, 1.15f);
        power.set(sr, 0.5f + 2.4f*master, -42.0f, 0.06f, 30.0f, 12000.0f);
        power.out = 0.011f;
        otVoice.set(sr, 9000.0f);

        // Family loudness makeup (gain-dependent on the louder channel volume).
        const float drv = (pNormalVol > pBrightVol ? pNormalVol : pBrightVol);
        outLevel = std::pow(10.0f, 0.05f * (12.0f + 10.0f * drv));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        // NORMAL + BRILLIANT channels (both jumpered)
        const float n = vN.process(x * gN);
        const float b = vB.process(brightShelf.process(x) * gB);
        float y = 0.6f * n + 0.6f * b;
        y = tone.process(y);                             // Hiwatt TMB
        y = presenceShelf.process(y);
        y = v3.process(y);                               // recovery
        y = pi.process(y * piDrive);                     // 12AT7 LTP PI
        y = power.process(y);                            // 4x EL34
        y = otVoice.process(y);
        return y * outLevel;
    }
};

} // namespace lovolt
#endif // LOVOLT_CORE_H
