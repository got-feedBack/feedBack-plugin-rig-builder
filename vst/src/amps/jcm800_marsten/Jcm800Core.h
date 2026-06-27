#ifndef JCM800_CORE_H
#define JCM800_CORE_H
//
// Jcm800Core — Marshall JCM800 2204, circuit-real on the shared tube_stage.hpp
// framework with CONTROLLED gain staging (clean at low Gain -> crunch at high).
//
// Rewritten from the original cascade (3x TubeStage with x16 coupling) which was
// so over-gained it saturated every signal to ~100% THD at ALL settings (no clean
// zone, behaved bimodally: saturate or silent). Same proven pattern as LovoltCore
// and the bass amps: drive each 12AX7 with a SMALL, controlled gain + a final
// loudness makeup, so the Gain knob actually sweeps clean -> crunch.
//
//   IN -> input coupling -> V1 12AX7 (input) -> GAIN -> V2 12AX7 (drive) ->
//   Marshall tone stack (Yeh) -> presence -> V3 12AX7 (recovery) -> 12AX7 LTP PI
//   -> 2x EL34 push-pull -> OT roll -> ·makeup. Runs 2x oversampled.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace jcm800 {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
};

struct Jcm800Core {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v2, v3;          // 12AX7: input, GAIN-driven, recovery
    Biquad brightShelf;                    // bright cap across the gain pot
    rbtube::ToneStackYeh tone;             // Marshall TMB (Yeh)
    Biquad presenceShelf, outTilt;         // power-amp NFB presence + amp-only top tilt
    rbtube::PhaseInverterLTP12AT7 pi;      // LTP phase inverter
    rbtube::PowerAmpEL34 power;            // 2x EL34
    rbtube::LP1 otVoice;

    float pGain=.6f,pBass=.5f,pMid=.5f,pTreble=.5f,pPres=.5f,pVol=.6f;
    float gDrive=1.f, piDrive=6.f, outLevel=1.f, v3Drive=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setGain(float v){ pGain=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setVolume(float v){ pVol=clamp01(v); recalc(); }

    void reset(){ inCoupling.reset(); v1.reset(); v2.reset(); v3.reset(); brightShelf.reset();
        tone.reset(); presenceShelf.reset(); outTilt.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        // Re-voice dialed against the amp-only reference render (top end + crunch onset):
        // bigger gain span + a recovery-stage drive that scales with Gain, tighter input
        // HP (120Hz) to firm the low end, brighter OT roll (16k) + a top tilt (+9 @ 2.6k).
        constexpr float kGSpan = 11.0f;    // GAIN-pot span into V2 (clean -> crunch)
        constexpr float kV3Ex  = 10.0f;    // recovery-stage extra drive, scales with Gain
        constexpr float kHp    = 120.0f;   // input coupling HP (firm lows)
        inCoupling.set(sr, kHp);
        v1.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);    // V1A input 12AX7
        v2.set(sr, 1, 250.0f, 40.0f, 22.0f, 2700.0f);    // V2A (2204): slightly colder bias than V1 (2k7 cathode) -> a touch of asymmetric crunch, not a fuzz
        v3.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);    // recovery 12AX7

        // The 2204 GAIN pot: controlled drive into V2 (clean -> crunch).
        gDrive = 0.45f + kGSpan * rbtube::PotTaper::audio(pGain, 1.30f);
        v3Drive = 1.0f + kV3Ex * rbtube::PotTaper::audio(pGain, 1.30f);
        brightShelf.highShelf(sr, 2000.0f, 4.0f * (1.0f - pGain));   // bright at low gain

        // Marshall tone stack (Yeh): Treble 220k/470pF, Bass 1M/22nF, Mid 22k/22nF, slope 33k.
        tone.setComponents(220e3, 1e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);
        presenceShelf.highShelf(sr, 3000.0f, (pPres-0.5f)*10.0f);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);

        const float vol = rbtube::PotTaper::audio(pVol, 1.15f);
        power.set(sr, 0.5f + 2.2f*vol, -38.0f, 0.06f, 30.0f, 11000.0f);
        power.out = 0.011f;
        constexpr float kOt   = 16000.0f;  // brighter OT roll (open top, matches reference)
        constexpr float kTilt = 9.0f;      // amp-only top tilt @ 2.6k (presence/edge)
        otVoice.set(sr, kOt);
        outTilt.highShelf(sr, 2600.0f, kTilt);

        // Loudness makeup: DECREASES with Gain to cancel the drive's level rise, so
        // the Gain knob adds distortion (not volume) and output stays ~flat ~-15 LUFS.
        // Loudness makeup: DECREASES with Gain to cancel the drive's level rise, so the
        // Gain knob adds distortion (not volume). Refit for the GS=11 re-voice so the
        // operating range sits ~-16 dBFS and peaks stay below the output soft-knee (no clip).
        outLevel = std::pow(10.0f, 0.05f * (5.5f - 5.0f * pGain));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1.process(x);                         // input 12AX7 (clean)
        y = v2.process(brightShelf.process(y) * gDrive); // GAIN-driven 12AX7
        y = tone.process(y);                             // Marshall TMB
        y = presenceShelf.process(y);
        y = v3.process(y * v3Drive);                     // recovery
        y = pi.process(y * piDrive);                     // LTP PI
        y = power.process(y);                            // 2x EL34
        y = otVoice.process(y);
        y = outTilt.process(y);
        return y * outLevel;
    }
};

} // namespace jcm800
#endif // JCM800_CORE_H
