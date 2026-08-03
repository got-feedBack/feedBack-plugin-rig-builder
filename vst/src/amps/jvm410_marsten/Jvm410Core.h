#ifndef JVM410_CORE_H
#define JVM410_CORE_H
//
// Jvm410Core — Marshall JVM410H (parody "Marsten"), circuit-real OWN core. A
// 4-channel (CLEAN/CRUNCH/OD1/OD2) cascading EL34 head; each channel has a 3-mode
// (green/orange/red) voicing that stacks extra preamp gain + saturation. Built on
// the proven DslCore/Jcm800Core pattern (gain stages BEFORE the passive tone stack,
// a DRIVEN recovery AFTER) so the OD channels saturate properly instead of hitting
// the shared core's ~10% THD ceiling.
//
//   IN -> inCoupling -> V1 12AX7
//     CLEAN : -> V2 (gentle) ......................... (stays clean)
//     CRUNCH: -> V2 (driven) ......................... (JCM800-ish crunch)
//     OD1   : -> V2 -> V3 (cascade) .................. (singing OD)
//     OD2   : -> V2 -> V3 -> V4 (cold cascade) ....... (liquid high-gain)
//   -> Marshall TMB tone stack (Yeh 220k/1M/22k/33k, 470p/22n/22n) -> Presence
//   -> V_recovery (driven) -> 12AX7 LTP PI -> 4x EL34 -> OT roll -> top tilt
//   -> Resonance (LF NFB) -> ·makeup. Runs oversampled.
//
// Schematic: amps/Marshall JVM410/Marshall_jvm410_sch.pdf (FRONT PANEL 1 tone
// stacks B220k/A1M/B22k + 470p/22n/22n; SHT2 4x EL34; Presence VR326 / Resonance VR305).
//
// RECALIBRADO 2026-07-26 contra el pack A2 del JVM410H real (83 caps APP-JVM,
// 4 canales x modos x 8 pasos; 39 refs en test logic/jvm410_a2/; paso N ~ Gain
// N/8, EQ noon asumido, Master 0.6). Hallazgos: el drive del CLEAN estaba ~4x
// timido (el Orange-II real ya trae el pelo de nuestro gain max viejo) y el
// clean real CRUNCHEA al fondo; los canales de drive eran gordos abajo y
// huecos en 800-2k; la voz DEBE vivir POST-power porque vR/PI re-saturan y
// lavan todo lo pre-stack (por eso el stack/trims pre-vR no movian el
// espectro); red = MAS gain conservando lows (no "mas apretado"). Bloques
// nuevos: cp12/23/34/R (blocking real), loadLowTrim/loadMidFill globales,
// chLowTrim post-power por canal (gain/mode-dependiente), od2MidCut, brights
// por canal, curvas de drive re-escaladas, makeup por canal re-medido a -16.
// Resultado: bandas <=+-2.5 tipico (peor +-3.5 en OD2-Red 250-800), coh
// mid/top +-0.10. RESIDUALES: coh 80-800 +0.15..+0.23 en ODs/Reds = piso de
// RUIDO de las refs (hum/hiss del amp real que el A2 reproduce; silencio de
// la ref -35 dBFS vs -42 nuestro — no perseguir), crest +1.2..+2.8 en
// clean/crunch medios (master/compresion de captura desconocidos), Reds
// todavia ~+0.15 mas limpios que la ref.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace jvm410 {

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

struct Jvm410Core {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v2, v3, v4, vR;
    rbtube::CouplingCapGridLeak cp12, cp23, cp34, cpR;   // blocking real entre etapas (la mugre LF que faltaba vs el pack A2)
    Biquad brightShelf, presenceShelf, resoShelf, outTilt, loadLowTrim, loadMidFill, chLowTrim, od2MidCut, od2MidCut2, od2LowFat;
    Biquad cabHP, cabLowShelf, cabPresence, cabTopRoll;     // fallback 4x12
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AT7 pi;
    rbtube::PowerAmpEL34 power;                             // 4x EL34 (~100W)
    rbtube::LP1 otVoice;

    // panel params
    int ch=2; float mode=0.5f;                              // 0 Clean/1 Crunch/2 OD1/3 OD2 ; green/orange/red
    float pGain=.6f, pVol=.5f, pBass=.5f, pMid=.5f, pTreble=.6f, pPres=.5f, pReso=.5f, pMaster=.6f;
    bool cabOn=true;

    float gDrive=1, v3Drive=1, v4Drive=1, vRDrive=1, piDrive=6, outLevel=1;
    bool useV2=true, useV3=true, useV4=false;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void reset(){ inCoupling.reset(); v1.reset();v2.reset();v3.reset();v4.reset();vR.reset();
        cp12.reset();cp23.reset();cp34.reset();cpR.reset();
        brightShelf.reset();presenceShelf.reset();resoShelf.reset();outTilt.reset();loadLowTrim.reset();loadMidFill.reset();chLowTrim.reset();od2MidCut.reset();od2MidCut2.reset();od2LowFat.reset();
        cabHP.reset();cabLowShelf.reset();cabPresence.reset();cabTopRoll.reset();
        tone.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        // Per-channel input HP (OD2 tightest, Clean fullest) + the green/orange/red
        // mode tightens the lows as it climbs (the real JVM red modes are tighter/
        // more focused) — makes the mode audible even when the channel is saturated.
        const float hpBase = ch==3 ? 62.0f : ch==0 ? 45.0f : 55.0f;
        inCoupling.set(sr, hpBase + 6.0f * mode);    // red = MAS GAIN, no menos graves (las refs Red conservan los lows)
        v1.set(sr, 1, 250.0f, 40.0f, 25.0f, 1800.0f);   // V1A input
        v2.set(sr, 1, 250.0f, 40.0f, 22.0f, 2700.0f);   // gain (colder = crunch)
        v3.set(sr, 1, 250.0f, 40.0f, 30.0f, 1500.0f);   // OD cascade 2
        v4.set(sr, 1, 250.0f, 40.0f, 33.0f, 820.0f);    // OD2 cascade 3 (cold -> the lead bite)
        vR.set(sr, 1, 250.0f, 40.0f, 55.0f, 1500.0f);   // recovery
        cp12.set(sr, 1.0e6f, 22.0e-9f, 470.0e3f, 0.55f, 0.40f, 1.0f);
        cp23.set(sr, 1.0e6f, 22.0e-9f, 470.0e3f, 0.55f, 0.40f, 1.0f);
        cp34.set(sr, 1.0e6f, 22.0e-9f, 470.0e3f, 0.55f, 0.40f, 1.0f);
        cpR.set(sr, 1.0e6f, 22.0e-9f, 470.0e3f, 0.55f, 0.40f, 1.0f);

        const float g  = rbtube::PotTaper::audio(pGain, 1.30f);
        const float m  = mode;                            // 0 green / 0.5 orange / 1 red
        // Per-channel cascade depth + drive. Gain stages sit BEFORE the passive tone
        // stack (loses ~15 dB) and the recovery AFTER -> the loss is what keeps the
        // cascade from collapsing to a square wave (the DslCore lesson).
        switch (ch) {
            case 0: // CLEAN — gentle, stays clean even cranked; mode adds a little edge
                useV2=true; useV3=false; useV4=false;
                gDrive  = 2.30f + (3.5f + 6.5f*m) * g;   // el clean real Orange-II ya trae el pelo de nuestro gain MAX viejo (fit A2)
                v3Drive = 1.0f; v4Drive = 1.0f;
                vRDrive = 1.0f + 2.2f * g;
                break;
            case 1: // CRUNCH — JCM800-style 2-stage
                useV2=true; useV3=false; useV4=false;
                gDrive  = 1.00f + (6.5f + 6.5f*m) * g;
                v3Drive = 1.0f; v4Drive = 1.0f;
                vRDrive = 1.0f + (4.5f + 1.5f*m) * g;
                break;
            case 2: // OD1 — +1 cascade (singing OD)
                useV2=true; useV3=true; useV4=false;
                gDrive  = 0.45f + (8.0f + 3.0f*m) * g;
                v3Drive = 1.0f + (4.0f + 2.0f*m) * g;
                v4Drive = 1.0f;
                vRDrive = 1.0f + (5.0f + 2.0f*m) * g;
                break;
            default: // OD2 — +2 cascade (liquid high-gain)
                useV2=true; useV3=true; useV4=true;
                gDrive  = 0.45f + (9.0f + 3.0f*m) * g;
                v3Drive = 1.0f + (5.0f + 2.0f*m) * g;
                v4Drive = 1.0f + (3.0f + 2.0f*m) * g;
                vRDrive = 1.0f + (6.5f + 2.0f*m) * g;
                break;
        }
        // bright cap across the gain pot (eases as gain rises); Clean is the brightest
        brightShelf.highShelf(sr, 2000.0f, (ch==0 ? 8.0f : 6.0f) * (1.0f - pGain));
        // Los canales de drive reales son mas MAGROS abajo que nuestra cascada
        // (acoples interstage): trim gain-dependiente (fit vs pack A2).
        const float trimDepth = (ch==1 ? (8.0f - 5.0f*g) : ch==2 ? (6.5f - 3.0f*g) : (5.5f - 3.0f*g)) - 2.2f*m;   // los modos red conservan lows
        chLowTrim.lowShelf(sr, 550.0f, ch==0 ? 0.0f : -trimDepth);

        // Marshall TMB tone stack (Yeh) — shared topology across the four channels.
        tone.setComponents(220e3, 1e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);
        presenceShelf.highShelf(sr, 3000.0f, (pPres-0.5f)*10.0f);   // HF NFB
        resoShelf.lowShelf(sr, 100.0f, (pReso-0.5f)*8.0f);          // LF NFB (Resonance) — was DEAD

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);
        const float vol = rbtube::PotTaper::audio(pMaster, 1.15f) * (0.7f + 0.6f*pVol);
        power.set(sr, 0.5f + 2.4f*vol, -36.0f, 0.12f, 30.0f, 11000.0f);
        power.out = 0.011f;
        otVoice.set(sr, 16000.0f);
        outTilt.highShelf(sr, 2600.0f, 9.0f);
        loadLowTrim.lowShelf(sr, 150.0f, -3.6f);        // el real es mas magro abajo (fit A2, todas las refs)
        loadMidFill.peak(sr, 1300.0f, 2.5f, 0.8f);      // 800-2k que faltaba
        // OD2 real es mas magro en 100-600 de lo que el shelf de 550 alcanza; el
        // deficit crece con el modo (peor caso medido: Red +3.4 dB en 250-800,
        // meseta +2 en 80-600 tras el primer corte).
        od2MidCut.peak(sr, 340.0f, ch==3 ? -(2.2f + 3.0f*m) : 0.0f, 0.60f);
        od2MidCut2.peak(sr, 520.0f, ch==3 ? -1.9f*m : 0.0f, 1.00f);   // el residuo 320-600 que el de 340 no alcanza
        // A gain BAJO el OD2 real es mucho mas magro en 100-180 (fit II/IV/VI:
        // +6.5/+2.2/0 en green; red ~1.2 dB menos gordo) — corte que decrece
        // con gain y con el modo, centrado bajo para no tocar 320-600.
        {
            const float lowFat = std::max(0.0f, std::min(6.5f, 22.0f*(0.45f - g)) - 2.0f*m);
            od2LowFat.peak(sr, 150.0f, ch==3 ? -lowFat : 0.0f, 0.90f);
        }

        // Fallback 4x12 (CabSim; host bypasses with an external IR).
        cabHP.highpass(sr, 80.0f, 0.70f);
        cabLowShelf.lowShelf(sr, 220.0f, -3.0f);
        cabPresence.peak(sr, 3500.0f, 3.0f, 0.8f);
        cabTopRoll.lowpass(sr, 5600.0f, 0.70f);

        // Loudness makeup: per-channel base, decreasing with Gain so the knob adds
        // dirt not level; ~-16 dBFS at the operating point. Clean is intrinsically
        // quiet (little gain) -> a bigger base; OD2 the smallest.
        const float mkBase = ch==0 ? 5.8f : ch==1 ? 6.0f : ch==2 ? 3.7f
                           : 3.3f + 1.6f*m;   // OD2: compensa el midCut por modo
        outLevel = std::pow(10.0f, 0.05f * (mkBase - 5.0f * pGain));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1.process(x);
        y = brightShelf.process(y);
        if (useV2) y = v2.process(cp12.process(y, gDrive));
        if (useV3) y = v3.process(cp23.process(y, v3Drive));
        if (useV4) y = v4.process(cp34.process(y, v4Drive));
        y = tone.process(y);
        y = presenceShelf.process(y);
        y = vR.process(cpR.process(y, vRDrive));
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        y = outTilt.process(y);
        y = loadLowTrim.process(y);
        y = loadMidFill.process(y);
        y = chLowTrim.process(y);   // POST-power: despues del ultimo clip (vR/PI re-saturan y lavan todo lo pre-stack)
        y = od2MidCut.process(y);
        y = od2MidCut2.process(y);
        y = od2LowFat.process(y);
        y = resoShelf.process(y);
        if (cabOn) { y = cabHP.process(y); y = cabLowShelf.process(y); y = cabPresence.process(y); y = cabTopRoll.process(y); }
        return y * outLevel;
    }
};

} // namespace jvm410
#endif // JVM410_CORE_H
