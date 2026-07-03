#ifndef AOR50_CORE_H
#define AOR50_CORE_H
//
// Aor50Core — Raney AOR50 = Laney A50 Series II (dwg 1284), circuit-real.
// HYBRID tube + silicon: the lead grind is a 1N4148 anti-parallel DIODE CLIPPER
// (JFET-switched per channel) recovered by a TL072 — NOT an all-tube cascade
// (the ECC83 stages run fairly clean; V2A/V2B are cold/unbypassed). Two channels
// via optocoupler: CHANNEL ONE (clean) / AOR (lead = clipper engaged).
//
//   IN -> V1A(hot) -> V1B -> V2A(cold) -> V2B(very cold) -> AOR tone stack (Yeh:
//   220k/1M/22k, 1M slope, 470p/10n/22n) -> V3 recovery -> [AOR: drive -> 1N4148
//   clipper -> TL072 makeup] -> Pull EQ (Deep/Mid-Boost/Bright) -> Presence ->
//   12AX7 LTP PI -> 2x EL34 -> OT roll -> top tilt -> ·makeup. Runs oversampled.
//
#include "../../_shared/tube_stage.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

namespace aor50 {

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
};

struct Aor50Core {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v1b, v2, v2b, v3;
    rbtube::ToneStackYeh tone;
    rbcomponents::AntiParallelDiodePair clip;     // 1N4148 lead clipper
    Biquad deepShelf, midBoostPk, brightShelf, presenceShelf, outTilt;
    rbtube::PhaseInverterLTP12AT7 pi;
    rbtube::PowerAmpEL34 power;                    // 2x EL34
    rbtube::LP1 otVoice;

    bool aor=true, brightOn=false, deepOn=false, midBoostOn=false;
    float pGain=.6f, pMaster=.5f, pBass=.55f, pMid=.5f, pTreble=.6f, pPres=.5f;

    float gDrive=1.f, clipDrive=1.f, clipMakeup=1.f, piDrive=6.f, outLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void reset(){ inCoupling.reset(); v1.reset();v1b.reset();v2.reset();v2b.reset();v3.reset();
        tone.reset(); clip.reset(); deepShelf.reset();midBoostPk.reset();brightShelf.reset();
        presenceShelf.reset();outTilt.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, 28.0f);                       // looser lows than a Marshall
        v1.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);    // V1A hot (bypassed)
        v1b.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);   // V1B
        v2.set(sr, 1, 215.0f, 40.0f, 55.0f, 1500.0f);    // V2A cold (R16 10k unbypassed)
        v2b.set(sr, 1, 140.0f, 40.0f, 80.0f, 1500.0f);   // V2B very cold (0.2V)
        v3.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);    // V3 recovery

        // AOR tone stack (Yeh) — circuit-real: Treble 220k/470pF, Bass 1M/10nF,
        // Mid 22k/22nF, SLOPE 1M (not the Marshall 33k -> scoopier mids / fatter,
        // looser lows). This was the model's biggest EQ error (was 250k/470k/25k/33k/250p).
        tone.setComponents(220e3, 1e6, 22e3, 1e6, 470e-12, 10e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);

        // CHANNEL: AOR engages the 1N4148 diode clipper (the lead grind) + a big
        // pre-clip drive; CHANNEL ONE bypasses the clipper and stays tube-clean.
        const float g = rbtube::PotTaper::audio(pGain, 1.30f);
        if (aor) {
            gDrive    = 1.0f + 3.0f * g;
            clipDrive = 1.0f + 26.0f * g;      // drive INTO the diodes = the lead gain
        } else {
            gDrive    = 1.2f + 3.0f * g;       // CHANNEL ONE clean gain
            clipDrive = 1.0f;                  // clipper bypassed -> clean
        }
        clip.setSpec(rbcomponents::diode1N4148());
        clip.setSourceR(3300.0f);              // soft silicon clip; tune for THD
        clipMakeup = 1.30f;                    // TL072 recovers the ±0.6V clip to ~unity+

        // Pull switches (were DEAD): Deep = LF bass lift; Mid-Boost = mid peak;
        // Bright = master treble-bleed (C22/C23 1n) high shelf, stronger at low master.
        deepShelf.lowShelf(sr, 90.0f, deepOn ? 6.0f : 0.0f);
        midBoostPk.peak(sr, 650.0f, midBoostOn ? 4.5f : 0.0f, 0.7f);
        brightShelf.highShelf(sr, 1600.0f, brightOn ? (3.0f + 4.0f*(1.0f-pMaster)) : 0.0f);
        presenceShelf.highShelf(sr, 3400.0f, (pPres-0.5f)*10.0f);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);
        const float vol = rbtube::PotTaper::audio(pMaster, 1.15f);
        power.set(sr, 0.5f + 2.2f*vol, -36.0f, 0.06f, 30.0f, 11000.0f);
        power.out = 0.011f;
        otVoice.set(sr, 14000.0f);             // open the top (diode-clip fizz); was a dark 9k
        outTilt.highShelf(sr, 2600.0f, 9.0f);

        // Loudness makeup (dB ≈ mk − 5·Gain): per-channel, decreasing with Gain so the
        // knob adds dirt not level. Big bases — the AOR clean tube path + tone stack lose
        // a lot, and the diode clip is recovered by the TL072. Target ~-16 dBFS operating.
        const float mk = aor ? 6.5f : 15.0f;
        outLevel = std::pow(10.0f, 0.05f * (mk - 5.0f * pGain));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1.process(x * gDrive);
        y = v1b.process(y);
        y = v2.process(y);
        y = v2b.process(y);
        y = tone.process(y);
        y = v3.process(y);
        if (aor) {                              // 1N4148 lead clipper + TL072 makeup
            y = clip.process(y * clipDrive) * clipMakeup;
        }
        y = deepShelf.process(y);
        y = midBoostPk.process(y);
        y = brightShelf.process(y);
        y = presenceShelf.process(y);
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        y = outTilt.process(y);
        return y * outLevel;
    }
};

} // namespace aor50
#endif // AOR50_CORE_H
