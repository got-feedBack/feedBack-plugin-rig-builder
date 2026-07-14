#ifndef MAJOR_CORE_H
#define MAJOR_CORE_H
//
// MajorCore — Marshall Major 200W ("The Pig"), circuit-real on the shared
// tube_stage.hpp framework. The Major is the Plexi's big brother but voiced for
// HEADROOM, not crunch:
//
//   IN -> coupling (tight) -> Bright + Normal 12AX7 channels (Volume I / II,
//   jumpered) -> fuller Marshall tone stack (Yeh) -> presence -> recovery 12AX7
//   -> 12AU7 LTP PI (clean, high-headroom) -> 4x KT88 at high B+ / cold bias
//   (200W, late/smooth breakup) -> OT roll -> internal 4x12 cab-sim.
//   Runs 2x oversampled.
//
// Differences from PlexiCore that MAKE it a Major: (1) PhaseInverterLTP12AU7
// instead of the 12AX7/12AT7 block — the real ECC82 splitter, far more headroom;
// (2) PowerAmpKT88 with a cold bias and a stiff supply instead of 4x EL34 — the
// KT88s stay clean much longer and break up tighter/smoother; (3) a lower preamp
// gain span so the amp is a clean-loud platform (Blackmore + booster), the
// distortion arriving late and mostly from the power section.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace majoramp {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
};

struct MajorCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage vBright, vNormal, v3;
    Biquad brightShelf, presenceShelf, outTilt;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AU7 pi;    // the Major's real ECC82 splitter (high headroom)
    rbtube::PowerAmpKT88 power;          // 4x KT88 (~200W, cold bias, huge headroom)
    rbtube::LP1 otVoice;
    rbtube::HP1 spkHP;                    // internal 4x12 cab-sim
    rbtube::LP1 spkLP1, spkLP2;
    Biquad spkPresence;

    float pPres=.5f,pBass=.5f,pMid=.55f,pTreble=.60f,pV1=.68f,pV2=.0f,pInput=.5f,pCab=1.f;
    float gB=1.f,gN=1.f,piDrive=5.f,outLevel=1.f,v3Drive=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setVolume1(float v){ pV1=clamp01(v); recalc(); }
    void setVolume2(float v){ pV2=clamp01(v); recalc(); }
    void setInput(float v){ pInput=clamp01(v); recalc(); }
    void setCabSim(float v){ pCab=clamp01(v); }

    void reset(){ inCoupling.reset(); vBright.reset(); vNormal.reset(); v3.reset();
        brightShelf.reset(); tone.reset(); presenceShelf.reset(); outTilt.reset();
        pi.reset(); power.reset(); otVoice.reset();
        spkHP.reset(); spkLP1.reset(); spkLP2.reset(); spkPresence.reset(); }

    void recalc(){
        inCoupling.set(sr, 100.0f);      // tight-ish lows; the real Major is fuller than the Plexi
        // Per the 1966 PA schematic + voltage chart: V1 cathodes are 820R fully
        // bypassed by 250uF (warm, full gain) and the V1 plates sit at ~170-175V
        // (not 250V) — lower plate voltage = earlier, softer compression when
        // pushed. The V2 driver runs 470R/25uF at ~250V.
        vBright.set(sr, 1, 175.0f, 40.0f, 8.0f, 820.0f);
        vNormal.set(sr, 1, 175.0f, 40.0f, 8.0f, 820.0f);
        v3.set(sr, 1, 250.0f, 40.0f, 22.0f, 470.0f);

        // Volume pots = the drive (non-master), BUT with a much lower gain span
        // than the Plexi (was 14) so the preamp stays clean deep into the sweep —
        // the Major's headroom. The (late) breakup rides on the power section.
        gB = 0.30f + 8.5f * rbtube::PotTaper::audio(pV1, 1.30f);
        gN = 0.30f + 8.5f * rbtube::PotTaper::audio(pV2, 1.30f);
        v3Drive = 1.0f + 3.5f * rbtube::PotTaper::audio(pV1 > pV2 ? pV1 : pV2, 1.30f);
        brightShelf.highShelf(sr, 2200.0f, 4.0f);

        // Major tone stack (Yeh), per the 1966 PA schematic: Treble 250k/250pF,
        // Bass 1M, Mid 25k, slope 56k (the JTM45/Bassman-lineage slope, deeper
        // mid scoop than the Plexi's 33k — was wrongly 39k).
        tone.setComponents(250e3, 1e6, 25e3, 56e3, 250e-12, 22e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);
        // PRESENCE: the real 5k/0.68u power-amp NFB presence is BOOST-ONLY
        // (0 = flat, max = full sparkle), not a cut/boost around noon.
        presenceShelf.highShelf(sr, 3000.0f, 9.0f*pPres);

        // 12AU7 LTP phase inverter — the real ECC82 splitter: low-mu, lots of
        // headroom, only hardens when really driven. This (not a 12AX7) is a big
        // part of why the Major stays clean and tight.
        piDrive = 5.0f;
        pi.setGibson(sr, 1.0f, 1.0f);

        // 4x KT88 (~200W): COLD bias (-50) + stiff supply -> the huge clean
        // headroom and the tight, smooth, LATE breakup. Fixed drive; the Volume
        // knobs push the preamp output that feeds this.
        power.set(sr, 1.35f, -50.0f, 0.045f, 40.0f, 11000.0f);
        power.out = 0.0090f;
        otVoice.set(sr, 12000.0f);              // KT88 top: tight and clear
        outTilt.highShelf(sr, 2600.0f, 3.0f);   // eased: presence is boost-only now and carries the sparkle

        // Internal 4x12 cab-sim voice (Cab Sim = 1)
        spkHP.set(sr, 95.0f);
        spkLP1.set(sr, 4400.0f);
        spkLP2.set(sr, 5200.0f);
        spkPresence.highShelf(sr, 2600.0f, 3.0f);

        // Loudness makeup: hold ~-16 dBFS RMS across the Volume sweep (it stays
        // cleaner than the Plexi, so the level rides up less with drive).
        const float drv = (pV1 > pV2 ? pV1 : pV2);
        outLevel = std::pow(10.0f, 0.05f * (17.0f - 16.0f * drv + 7.0f * drv * drv));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        const float jb = (pInput <= 0.5f) ? 1.0f : (1.0f - (pInput-0.5f)*2.0f);   // bright weight
        const float jn = (pInput >= 0.5f) ? 1.0f : (pInput*2.0f);                  // normal weight
        const float b = vBright.process(brightShelf.process(x) * gB);
        const float n = vNormal.process(x * gN);
        float y = 0.6f * (jb*b + jn*n);
        y = tone.process(y);
        y = presenceShelf.process(y);
        y = v3.process(y * v3Drive);
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        y = outTilt.process(y);
        y *= outLevel;
        // Internal 4x12 cab-sim: blended in by Cab Sim (0 = amp-only).
        if (pCab > 0.0001f) {
            float c = spkHP.process(y);
            c = spkLP2.process(spkLP1.process(c));
            c = spkPresence.process(c) * 1.5f;
            y = y + pCab * (c - y);
        }
        return y;
    }
};

} // namespace majoramp
#endif // MAJOR_CORE_H
