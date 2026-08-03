#ifndef JTM45_CORE_H
#define JTM45_CORE_H
//
// Jtm45Core — Marshall JTM45, circuit-real (familia Bassman 5F6-A), reescrito
// con la receta probada del Jcm800Core 2203:
//
//   IN -> V1 (dos mitades 12AX7: Bright con bright-cap, Normal) ->
//   Loudness I/II (pots con grid blocking, mezcla jumpered) ->
//   V2a 12AX7 (etapa de ganancia post-mezcla) ->
//   V2b cathode follower (acoplado directo; squash de conduccion de grilla) ->
//   stack FMV JTM45 (Treble 250k/270pF, Bass 1M, Mid 25k, slope 56k) DESPUES
//   de todo el clipping del pre (los graves cabalgan el clip = crest real) ->
//   presence (NFB del power, boost-only) -> PI LTP ECC83 -> 2x KT66 (~5881)
//   con sag GZ34 (rectificadora a valvula, no silicio) -> OT.
//
// CALIBRADO contra la grilla A2 del JTM45 real (test logic/jtm45_a2/, capturas
// contra carga reactiva del mismo rig que el AC30/JCM800): BALANCED V3/V5/V8
// x P2/P5/P8 (EQ noon asumido para BALANCED), CRANKED V10 y esquinas de EQ
// (BRIGHT/FAT/LOCUT/MIDS/SCOOP con mapeos asumidos de sus etiquetas).
// Resultado: coherencias +-0.10, crest +-0.5 en V3-V5 y presence sweep,
// bandas +-2.5, niveles +-0.7. RESIDUALES documentados: crest -2.1/-2.8 a
// V8/V10 (nuestro power aprieta transitorios mas que el real, misma clase
// que el JCM800/JC120; bias y sag no lo mueven), top 5-10k -2..-3.8 (piso
// de fizz/hiss de las capturas, no se persigue), sub +3 a V3, LOCUT +3.6
// de graves (su LOCUT probablemente es Bass=0 exacto).
//
// JUMPER calibrado aparte contra V10BOTH_CRANKED_P4 (la ref jumpereada es MAS
// sucia que la de un canal: coh -0.08/-0.10, +1.4 dB en 50-100): normalBody
// (canal Normal gordo real, el porque del truco), jDrive 1.45x en el grid de
// V2a, normalTop y jumpTopTrim. Con eso both-dimed queda en bandas +-0.6 y la
// coherencia en la misma clase que el residual single (+0.10/+0.08). El
// mapping del juego ahora lleva RS Gain a los DOS Loudness (rig jumpereado).
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace jtm45 {

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
    void lowShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)+(A-1)*c+t; b0=A*((A+1)-(A-1)*c+t)/a0; b1=2*A*((A-1)-(A+1)*c)/a0; b2=A*((A+1)-(A-1)*c-t)/a0; a1=-2*((A-1)+(A+1)*c)/a0; a2=((A+1)+(A-1)*c-t)/a0; }
};

struct Jtm45Core {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage vBright, vNormal, v2a;     // V1 (dos mitades) + V2a post-mezcla
    rbtube::CouplingCapGridLeak cpBright, cpNormal, cp2;
    Biquad brightShelf, normalBody, normalTop, presenceShelf, outTilt, loadBassRes, loadMid, loadAir, jumpTopTrim;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AX7 pi;            // ECC83 real (set() generico; setMarshall gatea)
    rbtube::PowerAmp5881 power;                  // ~KT66
    rbtube::LP1 otVoice;

    float pPres=.5f,pBass=.5f,pMid=.55f,pTreble=.62f,pL1=.62f,pL2=.0f,pInput=.5f;
    float gB=1.f,gN=1.f,cfDrive=1.6f,piDrive=4.f,outLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setLoudness1(float v){ pL1=clamp01(v); recalc(); }
    void setLoudness2(float v){ pL2=clamp01(v); recalc(); }
    void setInput(float v){ pInput=clamp01(v); recalc(); }

    void reset(){ inCoupling.reset(); vBright.reset(); vNormal.reset(); v2a.reset();
        cpBright.reset(); cpNormal.reset(); cp2.reset();
        brightShelf.reset(); normalBody.reset(); normalTop.reset(); tone.reset(); presenceShelf.reset(); outTilt.reset();
        loadBassRes.reset(); loadMid.reset(); loadAir.reset(); jumpTopTrim.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    // V2b cathode follower acoplado directo (mismo modelo que el Jcm800Core).
    static inline float cfSquash(float x){
        const float kneeP = 1.05f, sP = 0.55f;
        const float kneeN = 1.70f, sN = 0.40f;
        if (x >  kneeP) x =  kneeP + sP * std::tanh((x - kneeP) / sP);
        if (x < -kneeN) x = -(kneeN + sN * std::tanh((-x - kneeN) / sN));
        return x;
    }

    void recalc(){
        inCoupling.set(sr, 44.0f);                       // acople real (el 90 Hz anterior era fit a la ref sin carga)
        vBright.set(sr, 1, 260.0f, 40.0f, 25.0f, 820.0f);   // V1a bright: 820R catodo compartido 5F6A
        vNormal.set(sr, 1, 260.0f, 40.0f, 25.0f, 820.0f);   // V1b normal
        v2a.set(sr, 1, 260.0f, 40.0f, 24.0f, 1500.0f);      // V2a post-mezcla (1k5 bypass total)

        // Loudness = drive del canal (no-master). Floor moderado: el V3 real de la
        // grilla ya viene con algo de pelo; el barrido llega al roar en V8-V10.
        gB = 0.55f + 27.5f * rbtube::PotTaper::audio(pL1, 0.85f);
        gN = 0.55f + 27.5f * rbtube::PotTaper::audio(pL2, 0.85f);
        cpBright.set(sr, 1.0e6f, 22.0e-9f, 270.0e3f, 0.55f, 0.40f, 1.0f);
        cpNormal.set(sr, 1.0e6f, 22.0e-9f, 270.0e3f, 0.55f, 0.40f, 1.0f);
        cp2.set(sr, 1.0e6f, 22.0e-9f, 10.0e3f, 0.80f, 0.35f, 0.8f);
        // Bright cap del Loudness I (100pF): fuerte a volumen bajo, desaparece arriba.
        brightShelf.highShelf(sr, 1500.0f, 1.5f + 5.5f * (1.0f - rbtube::PotTaper::audio(pL1, 0.9f)));
        // Canal NORMAL: voz gorda real (acoples grandes, sin bright cap). Es el
        // motivo del truco del jumper: la ref V10BOTH suma +1.4 dB en 50-100
        // vs V10 de un canal y esos graves empujan al power (fit vs grilla A2).
        normalBody.lowShelf(sr, 150.0f, 3.0f);
        normalTop.highShelf(sr, 2800.0f, -1.6f);        // sin bright cap = mas oscuro arriba

        const float drv0 = (pL1 > pL2 ? pL1 : pL2);
        cfDrive = 0.85f + 1.15f * rbtube::PotTaper::audio(drv0, 0.9f);

        // Stack FMV JTM45 (Bassman-derived): slope 56k = el mid-voicing del JTM45.
        tone.setComponents(250e3, 1e6, 25e3, 56e3, 270e-12, 22e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);
        presenceShelf.highShelf(sr, 2400.0f, 13.0f * pPres);   // NFB presence: boost-only (pot 25k)

        piDrive = 4.4f;
        pi.set(sr, 1.0f, 1.0f);
        // GZ34 real en el JTM45 -> sag presente (no el 0.06 seco de antes).
        const float drvA = rbtube::PotTaper::audio(drv0, 0.85f);
        power.set(sr, 0.9f + 2.9f * drvA, -38.0f, 0.045f, 46.0f, 15000.0f);
        power.out = 0.0135f;
        otVoice.set(sr, 16500.0f);
        outTilt.highShelf(sr, 2800.0f, 6.2f);                 // aire contra carga (fit vs grilla A2)
        // La carga reactiva deja crecer LF/HF con el drive del power: los picos
        // reales a V8/V10 cabalgan esas bandas POR SOBRE el clip plano de medios
        // (el crest -2..-3.4 que bias/sag/drive NO movian; fit V8/V10 grilla A2).
        const float hot = std::fmax(0.0f, drvA - 0.45f);
        loadAir.highShelf(sr, 4800.0f, 8.5f * hot * (0.80f + 0.55f * pPres));   // con presence alto el NFB HF cede mas
        loadMid.peak(sr, 1300.0f, 1.6f + 1.3f * hot, 0.9f);   // 800-2k (el aire alto se lo comia a V10)
        loadBassRes.peak(sr, 100.0f, 0.3f + 2.0f * drvA + 1.4f * hot, 0.9f); // resonancia OT+carga (crece con drive; a V3 el sub sobraba)
        // Jumpereado el balance real pierde un pelo de 2-5k (fit vs V10BOTH).
        const float jbR = (pInput <= 0.5f) ? 1.0f : (1.0f - (pInput-0.5f)*2.0f);
        const float jnR = (pInput >= 0.5f) ? 1.0f : (pInput*2.0f);
        jumpTopTrim.peak(sr, 3000.0f, -1.5f * jbR * jnR, 0.85f);

        outLevel = std::pow(10.0f, 0.05f * (-0.33f - 6.93f * drv0 - 1.46f * drv0 * drv0 - 1.6f * hot));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        const float jb = (pInput <= 0.5f) ? 1.0f : (1.0f - (pInput-0.5f)*2.0f);   // peso bright
        const float jn = (pInput >= 0.5f) ? 1.0f : (pInput*2.0f);                  // peso normal
        const float b = cpBright.process(vBright.process(brightShelf.process(x)), gB);
        const float n = cpNormal.process(vNormal.process(normalTop.process(normalBody.process(x))), gN);
        // Jumpereado los dos canales suman EN el grid de V2a (mixers 270k):
        // la ref V10BOTH es MAS sucia que V10 de un canal, no mas limpia.
        const float jDrive = 1.0f + 0.45f * jb * jn;
        float y = v2a.process(0.55f * jDrive * (jb*b + jn*n));   // V2a post-mezcla
        y = cfSquash(cp2.process(y, cfDrive));            // V2b cathode follower
        y = tone.process(y);                              // stack DESPUES del clipping del pre
        y = presenceShelf.process(y);
        y = pi.process(y * piDrive);                      // LTP ECC83
        y = power.process(y);                             // KT66 + sag GZ34
        y = otVoice.process(y);
        y = outTilt.process(y);
        y = loadMid.process(y);
        y = loadAir.process(y);
        y = loadBassRes.process(y);
        y = jumpTopTrim.process(y);
        return y * outLevel;
    }
};

} // namespace jtm45
#endif // JTM45_CORE_H
