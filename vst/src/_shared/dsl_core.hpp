#ifndef DSL_CORE_HPP
#define DSL_CORE_HPP
//
// DslCore<PowerTube> — Marshall JCM2000 "Dual Super Lead" family OWN core
// (DSL100 = 4x EL34, DSL15 = 2x 6V6), modelled on the proven Jcm800Core but
// DUAL-CHANNEL with a real high-gain ULTRA cascade so it blows past the shared
// core's ~10% THD ceiling (the Ultra/Lead channel must be liquid/saturated).
//
//   IN -> inCoupling -> V1 12AX7
//     CLASSIC (Clean/Crunch): -> GAIN V2 ...................... (2 driven stages)
//     ULTRA   (OD1/OD2):      -> GAIN V2 -> V3 -> V4 (+midBoost) (4 driven stages)
//   -> JCM800 tone stack (Yeh) -> ToneShift scoop -> Presence -> V_recovery
//   -> 12AX7 LTP PI -> power tube -> OT roll -> top tilt -> Resonance(deep) -> ·makeup
//
// EQ/voicing calibrated against the finished Marsten family (JCM800/JTM45/Plexi):
// JCM800 TMB stack (220k/1M/22k/33k, 470p/22n/22n), 16k-ish OT + a 2.6k top tilt.
//
#include "tube_stage.hpp"
#include <cmath>

namespace rbdsl {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
    void lowShelf(float sr,float f,float dB){ if(f<10)f=10; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)+(A-1)*c+t; b0=A*((A+1)-(A-1)*c+t)/a0; b1=2*A*((A-1)-(A+1)*c)/a0; b2=A*((A+1)-(A-1)*c-t)/a0; a1=-2*((A-1)+(A+1)*c)/a0; a2=((A+1)+(A-1)*c-t)/a0; }
    void peak(float sr,float f,float dB,float Q){ if(f>sr*0.49f)f=sr*0.49f; if(dB==0.0f){b0=1;b1=b2=a1=a2=0;return;} float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q);
        float a0=1+al/A; b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
};

template <typename PowerTube>
struct DslCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v2, v3, v4, vR;     // input, gain, cascade2, cascade3(cold), recovery
    Biquad brightShelf, midBoost, scoop, presenceShelf, outTilt, resoShelf;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AT7 pi;
    PowerTube power;
    rbtube::LP1 otVoice;

    // panel params
    bool ultra=true, classicCrunch=true, ultraOD2=false, toneShift=false;
    float pClassicGain=.5f, pUltraGain=.6f, pBass=.55f, pMid=.5f, pTreble=.6f, pPres=.45f, pReso=.5f, pVol=.5f;

    // per-amp config (set once by the plugin)
    float cfgOtFreq=16000.f, cfgTilt=9.f, cfgBias=-37.f, cfgClassicSpan=9.f, cfgUltraSpan=14.f, cfgHp=115.f;
    float cfgPowerBase=0.5f, cfgPowerDrive=2.2f, cfgMakeupClassic=5.5f, cfgMakeupUltra=2.0f;

    float gDrive=1, v3Drive=1, v4Drive=1, vRDrive=1, piDrive=6, outLevel=1;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setConfig(float ot,float tilt,float bias,float cSpan,float uSpan,float hp,
                   float pBase,float pDrive,float mkC,float mkU){
        cfgOtFreq=ot; cfgTilt=tilt; cfgBias=bias; cfgClassicSpan=cSpan; cfgUltraSpan=uSpan; cfgHp=hp;
        cfgPowerBase=pBase; cfgPowerDrive=pDrive; cfgMakeupClassic=mkC; cfgMakeupUltra=mkU; recalc();
    }
    void setChannel(float v){ ultra = v>=0.5f; recalc(); }
    void setClassicMode(float v){ classicCrunch = v>=0.5f; recalc(); }
    void setUltraMode(float v){ ultraOD2 = v>=0.5f; recalc(); }
    void setToneShift(float v){ toneShift = v>=0.5f; recalc(); }
    void setClassicGain(float v){ pClassicGain=clamp01(v); recalc(); }
    void setUltraGain(float v){ pUltraGain=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setResonance(float v){ pReso=clamp01(v); recalc(); }
    void setVolume(float v){ pVol=clamp01(v); recalc(); }

    void reset(){ inCoupling.reset(); v1.reset();v2.reset();v3.reset();v4.reset();vR.reset();
        brightShelf.reset();midBoost.reset();scoop.reset();presenceShelf.reset();outTilt.reset();resoShelf.reset();
        tone.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, cfgHp);
        v1.set(sr, 1, 250.0f, 40.0f, 25.0f, 1800.0f);   // V1A input 12AX7
        v2.set(sr, 1, 250.0f, 40.0f, 22.0f, 2700.0f);   // gain stage (colder = crunch)
        v3.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);   // Ultra cascade 2
        v4.set(sr, 1, 250.0f, 40.0f, 33.0f, 820.0f);    // Ultra cascade 3 (cold -> asym, the lead bite)
        vR.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);   // recovery

        // Proven JCM800/MarkIII structure: the gain stages sit BEFORE the passive tone
        // stack (which loses ~15 dB), and a DRIVEN recovery stage sits AFTER it. That
        // tone-stack loss is what keeps the cascade from running straight to a square
        // wave. ULTRA adds ONE extra pre-tone cascade stage (v3) for the liquid lead.
        const float g = ultra ? pUltraGain : pClassicGain;
        const float uT = rbtube::PotTaper::audio(pUltraGain, 1.30f);
        const float cT = rbtube::PotTaper::audio(pClassicGain, 1.30f);
        if (ultra) {
            gDrive  = 0.45f + cfgUltraSpan * uT;             // gain stage
            v3Drive = 1.0f + 5.0f * uT;                      // extra ULTRA cascade (pre-tone)
            vRDrive = 1.0f + 9.0f * uT + (ultraOD2 ? 1.5f : 0.0f);  // driven recovery (post-tone)
        } else {
            gDrive  = (0.45f + cfgClassicSpan * cT) * (classicCrunch ? 1.25f : 1.0f);
            v3Drive = 1.0f;
            vRDrive = 1.0f + 4.0f * cT;                      // mild recovery
        }
        v4Drive = 1.0f;   // v4 unused in the JCM800-style chain
        brightShelf.highShelf(sr, 2000.0f, 4.0f * (1.0f - g));
        // ULTRA OD2 ("Lead 2") = a mid-boosted, higher-gain voice
        midBoost.peak(sr, 600.0f, (ultra && ultraOD2) ? 5.0f : 0.0f, 0.7f);

        // JCM800-family TMB tone stack (Yeh), double-precision internally.
        tone.setComponents(220e3, 1e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);
        // TONE SHIFT: switched mid-scoop (the modern "scooped" voice)
        scoop.peak(sr, 600.0f, toneShift ? -8.0f : 0.0f, 0.7f);
        presenceShelf.highShelf(sr, 3000.0f, (pPres-0.5f)*10.0f);
        // RESONANCE / DEEP: resonant low-end boost in the power section
        resoShelf.lowShelf(sr, 100.0f, (pReso-0.5f)*8.0f);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);
        const float vol = rbtube::PotTaper::audio(pVol, 1.15f);
        power.set(sr, cfgPowerBase + cfgPowerDrive*vol, cfgBias, 0.06f, 30.0f, 11000.0f);
        power.out = 0.011f;
        otVoice.set(sr, cfgOtFreq);
        outTilt.highShelf(sr, 2600.0f, cfgTilt);

        // Loudness makeup: per-channel base, decreasing with Gain so the knob adds
        // dirt not level; target ~-16 dBFS at the operating point (family method).
        const float mk = ultra ? cfgMakeupUltra : cfgMakeupClassic;
        outLevel = std::pow(10.0f, 0.05f * (mk - 5.0f * g));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1.process(x);
        y = brightShelf.process(y);
        y = v2.process(y * gDrive);                  // gain stage
        if (ultra) {
            y = v3.process(y * v3Drive);             // extra ULTRA cascade stage
            y = midBoost.process(y);                 // OD2 mid boost
        }
        y = tone.process(y);                         // tone stack (attenuates ~-15 dB)
        y = scoop.process(y);                        // Tone Shift
        y = presenceShelf.process(y);
        y = vR.process(y * vRDrive);                 // driven recovery
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        y = outTilt.process(y);
        y = resoShelf.process(y);                    // Resonance / Deep
        return y * outLevel;
    }
};

} // namespace rbdsl
#endif // DSL_CORE_HPP
