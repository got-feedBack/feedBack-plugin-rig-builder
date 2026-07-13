#ifndef VH4_CORE_H
#define VH4_CORE_H
//
// Vh4Core — Diezel VH4, circuit-real on the shared tube_stage.hpp framework.
// Currently voices CHANNEL 3 "MEGA" (the iconic tight VH4 high-gain), modelled
// as a real 12AX7 cascade + 4x EL34 power, using the Aion DZ4 recreation as the
// component-level reference (tone stack, Deep/Presence corners, gain structure).
//
//   IN -> tight coupling -> v1..v4 12AX7 cascade (Gain = the drive) -> Marshall
//   TMB tone stack (Yeh) -> DEEP (active ~115 Hz low shelf) -> recovery -> LTP PI
//   -> 4x EL34 (Master = drive) -> PRESENCE (power-amp NFB ~4 kHz) -> OT roll ->
//   internal 4x12 cab-sim. Runs 2x oversampled.
//
// ── EXPANSION ──
// `channel` + voiceChannel() carry the multi-channel structure. Only case 2
// (MEGA) is filled today; Ch1 (Clean), Ch2 (Crunch), Ch4 (Lead) are reserved
// cases that fall back to MEGA until modelled. Adding a channel = fill its case
// (stage count / gains / tone / tightness) — the process() chain is generic.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace vh4 {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),sn=std::sin(w),al=sn*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
    void lowShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),sn=std::sin(w),al=sn*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)+(A-1)*c+t; b0=A*((A+1)-(A-1)*c+t)/a0; b1=2*A*((A-1)-(A+1)*c)/a0; b2=A*((A+1)-(A-1)*c-t)/a0; a1=-2*((A-1)+(A+1)*c)/a0; a2=((A+1)+(A-1)*c-t)/a0; }
};

struct Vh4Core {
    float sr = 96000.0f;
    int   channel = 2;                    // 0=Clean 1=Crunch 2=MEGA(Ch3) 3=Lead(Ch4). Only 2 modelled.
    rbtube::HP1 inCoupling, hp12, hp23;   // tight interstage coupling = the VH4 focus
    rbtube::TubeStage v1, v2, v3, v4;
    Biquad deepShelf, presenceShelf, outTilt;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AT7 pi;
    rbtube::PowerAmpEL34 power;           // 4x EL34 (~100W)
    rbtube::LP1 otVoice;
    rbtube::HP1 spkHP;                     // internal 4x12 cab-sim
    rbtube::LP1 spkLP1, spkLP2;
    Biquad spkPresence;

    float pGain=.60f,pBass=.5f,pMid=.55f,pTreble=.55f,pDeep=.5f,pPres=.5f,pMaster=.55f,pCab=1.f;
    float g1=1,g2=1,g3=1,g4=1,piDrive=6.f,outLevel=1.f; int nStages=4;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setChannel(int ch){ channel=ch; recalc(); }
    void setGain(float v){ pGain=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setDeep(float v){ pDeep=clamp01(v); recalc(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setMaster(float v){ pMaster=clamp01(v); recalc(); }
    void setCabSim(float v){ pCab=clamp01(v); }

    void reset(){ inCoupling.reset(); hp12.reset(); hp23.reset(); v1.reset(); v2.reset(); v3.reset(); v4.reset();
        deepShelf.reset(); presenceShelf.reset(); outTilt.reset(); tone.reset(); pi.reset(); power.reset(); otVoice.reset();
        spkHP.reset(); spkLP1.reset(); spkLP2.reset(); spkPresence.reset(); }

    // Per-channel voicing. Only MEGA (Ch3) is filled today; others fall through.
    void voiceChannel(){
        const float gA = rbtube::PotTaper::audio(pGain, 1.55f);   // steep -> wide high-gain range
        switch(channel){
            default:            // reserved: Clean(0)/Crunch(1)/Lead(3) -> fall back to MEGA for now
            case 2:             // ── MEGA (Ch3): deep, tight 4-stage cascade ──
                inCoupling.set(sr, 100.0f);   // very tight lows (the VH4 focus); DEEP adds them back
                hp12.set(sr, 150.0f);         // interstage coupling caps keep the cascade tight
                hp23.set(sr, 160.0f);
                v1.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
                v2.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);
                v3.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
                v4.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);   // recovery
                g1 = 1.0f;
                g2 = 0.9f + 9.0f*gA;
                g3 = 0.6f + 7.0f*gA;
                g4 = 1.0f + 3.5f*gA;
                nStages = 4;
                break;
        }
    }

    void recalc(){
        voiceChannel();
        // Marshall TMB tone stack (Yeh) — DZ4 Ch3 values: Treble 250k/560pF,
        // Bass 1M/22n, Mid 25k/22n, slope 39k.
        tone.setComponents(250e3, 1e6, 25e3, 39e3, 560e-12, 22e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);
        // DEEP: active low boost/cut ~115 Hz (noon = flat). The VH4 low-end thump.
        deepShelf.lowShelf(sr, 115.0f, (pDeep-0.5f)*16.0f);
        // PRESENCE: real-amp power-amp NFB high-shelf ~4 kHz (NOT the DZ4 pedal's
        // 160 Hz full-range quirk). noon = flat.
        presenceShelf.highShelf(sr, 4000.0f, (pPres-0.5f)*11.0f);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);

        // 4x EL34: MASTER = power drive + output. Tight OT.
        const float vol = rbtube::PotTaper::audio(pMaster, 1.15f);
        power.set(sr, 0.5f + 2.2f*vol, -37.0f, 0.05f, 34.0f, 11000.0f);
        power.out = 0.011f;
        otVoice.set(sr, 12500.0f);
        outTilt.highShelf(sr, 2600.0f, 4.0f);

        // Internal 4x12 cab-sim (Cab Sim = 1)
        spkHP.set(sr, 95.0f);
        spkLP1.set(sr, 4400.0f);
        spkLP2.set(sr, 5200.0f);
        spkPresence.highShelf(sr, 2600.0f, 3.0f);

        // Loudness makeup: DECREASES with Gain so the knob is drive, not level;
        // operating point ~-16 dBFS RMS.
        outLevel = std::pow(10.0f, 0.05f * (4.5f - 5.0f*pGain - 0.5f*pMaster));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1.process(x * g1);
        y = hp12.process(y);
        y = v2.process(y * g2);
        y = hp23.process(y);
        if (nStages >= 3) y = v3.process(y * g3);
        y = tone.process(y);
        y = deepShelf.process(y);
        y = v4.process(y * g4);                // recovery / 4th gain stage
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = presenceShelf.process(y);          // power-amp NFB presence
        y = otVoice.process(y);
        y = outTilt.process(y);
        y *= outLevel;
        if (pCab > 0.0001f) {
            float c = spkHP.process(y);
            c = spkLP2.process(spkLP1.process(c));
            c = spkPresence.process(c) * 1.5f;
            y = y + pCab * (c - y);
        }
        return y;
    }
};

} // namespace vh4
#endif // VH4_CORE_H
