#ifndef JCM800_CORE_H
#define JCM800_CORE_H
//
// Jcm800Core — Marshall JCM800 2203/2204 "LEAD SERIES" Master Volume,
// circuit-real from the factory preamp drawing (jcm800pr.gif, 24-4-81):
//
//   IN(High, 68k) -> V1a 12AX7 (100k plate, 10k||0.68u cathode)
//     -> PREAMP 1M pot -> 470k series || 470p bright (fixed treble lift)
//     -> V1b 12AX7 (100k plate, 820R||0.68u cathode — hot, tight lows)
//     -> V2b cathode follower (direct-coupled; grid-conduction squash)
//     -> Marshall TMB stack (Treble 250k + 470p, Bass 1M, Mid 25k, slope 33k,
//        22n/22n) — the stack sits AFTER all preamp clipping, so lows ride the
//        clipped wave (spiky crest, the VH4 lesson)
//     -> MASTER 1M -> LTP PI 12AX7 (82k/100k, Marshall values)
//     -> EL34 push-pull -> OT rolloffs -> amp-only top tilt -> makeup.
//
// Presence (22k pot, 0.1u in the power NFB) approximated as a 0..+boost high
// shelf ahead of the power stage. Calibrated against the preamp_min/half/max
// reference renders (brit_di, EQ/master at noon assumed) with
// compare_amp_reference + compare_nonlinear_reference:
//   bands within +-2 dB, crest p50 within +-0.9 dB, RMS within +-1.2 dB,
//   coherence 800-2500 / 2500-8000 within +-0.03 at all three points.
// RESIDUAL (documented, do not chase blindly): 80-800 coherence is +0.12..+0.21
// cleaner than the reference at half/max. CF bite, power drive and grid
// blocking were each pushed to close it and overshot the mid bands first; part
// of the gap is reference noise/hum (its own top-band coherence at MIN is only
// 0.17). Same class of residual as the Ironheart pre-boost lows.
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
    void peak(float sr,float f,float dB,float Q){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2*Q);
        float a0=1+al/A; b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
};

struct Jcm800Core {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1a, v1b;            // 12AX7: input (10k||0.68u), hot (820R||0.68u)
    rbtube::CouplingCapGridLeak cp1, cp2;  // .022u couplings with grid blocking
    Biquad brightShelf;                    // 470k || 470p interstage bright (fixed)
    rbtube::ToneStackYeh tone;             // Marshall TMB (real 2203 values)
    Biquad presenceShelf, outTilt, lowMidDip, loadBassRes, loadMid, loadAir;  // NFB presence + top tilt + 160Hz + carga reactiva (master alto)
    rbtube::PhaseInverterLTP12AX7 pi;      // ECC83 LTP (82k/100k)
    rbtube::PowerAmpPPT<rbtube::TubeEL34> power;
    rbtube::LP1 otVoice;

    float pGain=.6f,pBass=.5f,pMid=.5f,pTreble=.5f,pPres=.5f,pVol=.6f;
    float gDrive=1.f, cfDrive=1.6f, piDrive=4.f, outLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setGain(float v){ pGain=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setVolume(float v){ pVol=clamp01(v); recalc(); }

    void reset(){ inCoupling.reset(); v1a.reset(); v1b.reset(); cp1.reset(); cp2.reset();
        brightShelf.reset(); tone.reset(); presenceShelf.reset(); outTilt.reset(); lowMidDip.reset(); loadBassRes.reset(); loadMid.reset(); loadAir.reset();
        pi.reset(); power.reset(); otVoice.reset(); }

    // V2b direct-coupled cathode follower driving the stack: the positive swing
    // runs into grid conduction (soft squash), the negative runs out of tail
    // current later (harder, asymmetric). Units are plate-volts/40.
    static inline float cfSquash(float x){
        const float kneeP = 1.15f, sP = 0.60f;
        const float kneeN = 1.60f, sN = 0.40f;
        if (x >  kneeP) x =  kneeP + sP * std::tanh((x - kneeP) / sP);
        if (x < -kneeN) x = -(kneeN + sN * std::tanh((-x - kneeN) / sN));
        return x;
    }

    void recalc(){
        inCoupling.set(sr, 33.0f);
        v1a.set(sr, 1, 260.0f, 40.0f, 23.0f, 10000.0f);   // 10k || 0.68u (fc 23 Hz)
        v1b.set(sr, 1, 260.0f, 40.0f, 286.0f, 820.0f);    // 820R || 0.68u (fc 286 Hz)

        // PREAMP pot (1M audio) + 470k series into V1b. The reference plugin's
        // "min" is still driven, so the sweep has a hot floor.
        gDrive = 3.0f + 37.0f * rbtube::PotTaper::audio(pGain, 0.50f);
        cp1.set(sr, 1.0e6f, 22.0e-9f, 470.0e3f, 0.70f, 0.40f, 1.2f);
        brightShelf.highShelf(sr, 650.0f, 1.0f + 10.0f*(1.0f - rbtube::PotTaper::audio(pGain, 0.7f)));  // 470p bright: fuerte a gain bajo (wiper alto), casi nulo a max
        cp2.set(sr, 1.0e6f, 22.0e-9f, 10.0e3f, 0.80f, 0.35f, 0.8f);
        cfDrive = 1.2f + 1.4f * rbtube::PotTaper::audio(pGain, 0.8f);

        // Real 2203 stack: Treble 250k + 470p, Bass 1M, Mid 25k, slope 33k, 22n/22n.
        tone.setComponents(250e3, 1e6, 25e3, 33e3, 470e-12, 22e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);
        presenceShelf.highShelf(sr, 2400.0f, 9.0f * pPres);   // NFB presence: boost only
        lowMidDip.peak(sr, 155.0f, -4.0f, 0.8f);

        const float vol = rbtube::PotTaper::audio(pVol, 1.15f);
        piDrive = 6.0f * (0.10f + 0.90f * vol) / 0.505f;      // MASTER into the PI (noon = cal point)
        pi.set(sr, 1.0f, 1.0f);
        power.set(sr, 0.6f + 2.4f*vol, -38.0f, 0.03f, 100.0f, 14000.0f);
        power.out = 0.011f;
        otVoice.set(sr, 16000.0f);
        outTilt.highShelf(sr, 2600.0f, 10.0f + 2.5f*pGain);
        // Carga reactiva con el POWER exigido (receta del Jtm45Core, fit V8/V10
        // de su grilla A2): la impedancia deja crecer LF (~100 Hz) y HF por
        // sobre el clip plano de medios — es el crest que bias/sag/drive no dan.
        // hot=0 en el punto de calibracion (master noon): las refs preamp_min/
        // half/max quedan bit-identicas; entra solo con master arriba.
        const float hot = std::fmax(0.0f, vol - 0.55f);
        loadBassRes.peak(sr, 100.0f, 6.0f * hot, 0.9f);
        loadMid.peak(sr, 1300.0f, 1.6f * hot, 0.9f);
        loadAir.highShelf(sr, 4800.0f, 10.5f * hot * (0.80f + 0.55f * pPres));

        // Level trajectory vs the references (min -22.6 / half -15.2 / max -14.7 RMS).
        // Trayectoria RMS calibrada a las refs (min->max ~ +8 dB); el absoluto va
        // -4.5 dB bajo la ref para caer en familia (~-8 LUFS post-cab como
        // plexi/jtm45/dualrect) — el leveler final usa el modelo de loudness.
        outLevel = std::pow(10.0f, 0.05f * (-2.4f + 9.6f*pGain - 6.4f*pGain*pGain - 4.2f * hot));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1a.process(x);                        // V1a input 12AX7
        y = brightShelf.process(cp1.process(y, gDrive)); // PREAMP pot + 470k||470p
        y = v1b.process(y);                              // V1b hot 12AX7
        y = cfSquash(cp2.process(y, cfDrive));           // V2b cathode follower
        y = tone.process(y);                             // TMB stack (post-clip)
        y = lowMidDip.process(y);
        y = presenceShelf.process(y);
        y = pi.process(y * piDrive);                     // MASTER -> LTP PI
        y = power.process(y);                            // EL34 push-pull
        y = otVoice.process(y);
        y = outTilt.process(y);
        y = loadMid.process(y);
        y = loadAir.process(y);
        y = loadBassRes.process(y);
        return y * outLevel;
    }
};

} // namespace jcm800
#endif // JCM800_CORE_H
