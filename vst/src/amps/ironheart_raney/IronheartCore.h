#ifndef IRONHEART_CORE_H
#define IRONHEART_CORE_H
//
// IronheartCore — Laney Ironheart IRT60H, circuit-real on tube_stage.hpp.
// Topology from the official service schematic (laney_irt60h_sm.pdf):
//
//   IN -> TL072 buffer -> [op-amp PRE-BOOST, VR2 100K + 220 pF HF limit]
//   -> V1A ECC83 (100K plate, 1K5 || 1uF cathode -> ~106 Hz partial bypass)
//   LEAD:   CH2 GAIN -> V1B (1K5||22u) -> V2A -> V2B -> V3B (plates ~140 V)
//   RHYTHM: divider -> RHYTHM DRIVE -> V3A(a) -> V3A(b)
//   CLEAN:  divider 220K/4K7+470 pF -> CLEAN VOLUME -> V3A(a)
//   -> TMB (T 250K/470p, slope 47K, B 250K/22n+100n, M 25K/22n)
//   -> VOLUME 1M -> LTP PI (82K/100K) -> TONE (dual 220K post-PI tilt)
//   -> WATTS (dual 220K drive attenuator) -> 2x 6L6 -> OT.
//   DYNAMICS = schematic ENHANCE: power NFB depth (220K + 6n8/39K).
//
#include "../../_shared/tube_stage.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

namespace irt {

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

struct IronheartCore {
    float sr = 96000.0f;
    int   channel = 1;                    // 0=Clean 1=Rhythm 2=Lead
    rbtube::HP1 inCoupling;
    rbtube::LP1 boostLP;                  // 220 pF sobre el op-amp de boost
    rbcomponents::AntiParallelDiodePair boostClip;   // KDS226 en el feedback del TL072
    rbtube::CouplingCapGridLeak cp1, cp2, cp3, cp4;
    rbtube::TubeStage v1, s2, s3, s4, s5; // V1A + hasta 4 etapas del canal
    rbtube::ToneStackYeh tone;
    Biquad toneTilt, dynShelf, outTilt;
    rbtube::PhaseInverterLTP12AX7 pi;     // V4 ECC83 LTP (82K/100K)
    rbtube::PowerAmpPPT<rbtube::Tube6L6GC> power; // 2x 6L6GC (bias-selectable EL34)
    rbtube::LP1 otVoice;
    rbtube::HP1 spkHP;                    // internal 4x12 cab-sim
    rbtube::LP1 spkLP1, spkLP2;
    Biquad spkPresence;

    float pGain=.55f,pBass=.5f,pMid=.5f,pTreble=.5f,pVol=.6f,pDyn=.5f,pTone=.5f,pWatts=1.f,pCab=1.f,pBoost=.5f;
    bool  boostOn=false;
    float g1=1,g2=1,g3=1,g4=1,g5=1,boostGain=1.f,outLevel=1.f; int nStages=2;
    float lvlBase=6.f,lvlGain=-4.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setChannel(int ch){ channel=ch; recalc(); }
    void setGain(float v){ pGain=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setVolume(float v){ pVol=clamp01(v); recalc(); }
    void setDynamics(float v){ pDyn=clamp01(v); recalc(); }
    void setTone(float v){ pTone=clamp01(v); recalc(); }
    void setWatts(float v){ pWatts=clamp01(v); recalc(); }
    void setCabSim(float v){ pCab=clamp01(v); }
    void setBoost(float v){ pBoost=clamp01(v); recalc(); }
    void setBoostOn(bool b){ boostOn=b; recalc(); }

    void reset(){ inCoupling.reset(); boostLP.reset(); boostClip.reset(); cp1.reset(); cp2.reset(); cp3.reset(); cp4.reset();
        v1.reset(); s2.reset(); s3.reset(); s4.reset(); s5.reset(); tone.reset(); toneTilt.reset();
        dynShelf.reset(); outTilt.reset(); pi.reset(); power.reset(); otVoice.reset();
        spkHP.reset(); spkLP1.reset(); spkLP2.reset(); spkPresence.reset(); }

    void voiceChannel(){
        const float gA = rbtube::PotTaper::audio(pGain, 1.45f);
        switch(channel){
            case 0:             // ── CLEAN: V1A -> divider + 470 pF bright -> V3A(a) ──
                inCoupling.set(sr, 25.0f);
                cp1.set(sr, 1.0e6f, 2.2e-9f, 68.0e3f, 0.60f, 0.35f, 0.8f);
                v1.set(sr, 1, 250.0f, 40.0f, 106.0f, 1500.0f);   // 1K5 || 1uF
                s2.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);    // V3A(a)
                g1 = 1.0f;
                g2 = 1.5f + 20.0f*gA;
                nStages = 2;
                lvlBase = 34.5f; lvlGain = -12.0f;
                break;
            default:
            case 1:             // ── RHYTHM: V1A -> RHYTHM DRIVE -> V3A(a) -> V3A(b) ──
                inCoupling.set(sr, 28.0f);
                cp1.set(sr, 1.0e6f, 1.8e-9f, 220.0e3f, 0.45f, 0.45f, 1.2f);
                cp2.set(sr, 1.0e6f, 1.5e-9f, 220.0e3f, 0.45f, 0.45f, 1.2f);
                v1.set(sr, 1, 250.0f, 40.0f, 106.0f, 1500.0f);
                s2.set(sr, 1, 250.0f, 40.0f, 140.0f, 1500.0f);
                s3.set(sr, 1, 235.0f, 40.0f, 160.0f, 1500.0f);   // V3A plates ~140 V: mas comprimida
                g1 = 1.0f;
                g2 = 9.0f + 12.0f*gA;
                g3 = 6.0f + 9.0f*gA;
                nStages = 3;
                lvlBase = 19.5f; lvlGain = -1.5f;
                break;
            case 2: {           // ── LEAD: V1A -> CH2 GAIN -> V1B -> V2A -> V2B -> V3B ──
                inCoupling.set(sr, 30.0f);
                cp1.set(sr, 1.0e6f, 1.4e-9f, 330.0e3f, 0.35f, 0.55f, 1.6f);
                cp2.set(sr, 1.0e6f, 1.2e-9f, 330.0e3f, 0.35f, 0.55f, 1.6f);
                cp3.set(sr, 1.0e6f, 1.2e-9f, 330.0e3f, 0.35f, 0.55f, 1.6f);
                cp4.set(sr, 1.0e6f, 1.6e-9f, 330.0e3f, 0.40f, 0.50f, 1.4f);
                // Residual documentado: con PRE-BOOST la referencia derrumba la
                // coherencia de graves (IMD masiva, 0.24 vs nuestro ~0.5); el
                // clip de diodos + niveles + mid/high si calzan.
                v1.set(sr, 1, 250.0f, 40.0f, 106.0f, 1500.0f);
                s2.set(sr, 1, 250.0f, 40.0f, 5.0f, 1500.0f);     // V1B: 1K5 || 22uF (full bypass)
                s3.set(sr, 1, 250.0f, 40.0f, 150.0f, 1500.0f);   // V2A
                s4.set(sr, 1, 235.0f, 40.0f, 160.0f, 1500.0f);   // V2B
                s5.set(sr, 1, 220.0f, 40.0f, 50.0f, 1500.0f);    // V3B a placas bajas (~140 V)
                g1 = 1.0f;
                g2 = 4.2f + 10.0f*gA;
                g3 = 2.6f + 6.5f*gA;
                g4 = 2.0f + 4.5f*gA;
                g5 = 1.5f + 2.2f*gA;
                nStages = 5;
                lvlBase = 18.5f; lvlGain = -1.0f;
                break; }
        }
    }

    void recalc(){
        voiceChannel();
        // op-amp PRE-BOOST: VR2 100K, ganancia minima +12 dB (la referencia con
        // boost al minimo ya satura igual) hasta +20 dB; 220 pF limita el top.
        boostGain = boostOn ? std::pow(10.0f, 0.05f * (12.0f + 8.0f * rbtube::PotTaper::audio(pBoost, 1.2f))) : 1.0f;
        boostClip.setSpec(rbcomponents::diode1N4148());   // KDS226 ~= silicio rapido
        boostClip.setSourceR(2200.0f);
        boostLP.set(sr, 7200.0f);          // 100K || 220 pF
        // TMB del esquema: Treble 250K/470p, slope 47K, Bass 250K/22n, Mid 25K/22n
        tone.setComponents(250e3, 250e3, 25e3, 47e3, 470e-12, 22e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);
        // TONE: tilt post-PI (dual 220K + 10n/100n) — oscuro<->brillante
        toneTilt.highShelf(sr, 1200.0f, (pTone-0.5f)*10.0f);
        // DYNAMICS (ENHANCE): profundidad del NFB — low shelf en potencia
        dynShelf.lowShelf(sr, 140.0f, (pDyn-0.5f)*9.0f);
        // WATTS: atenuador de drive al par de 6L6 (<1W .. 60W)
        const float w = 0.12f + 0.88f * rbtube::PotTaper::audio(pWatts, 1.0f);
        power.set(sr, (0.5f + 2.4f*w) , -52.0f, 0.10f, 40.0f, 14000.0f);
        power.out = 0.010f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);
        otVoice.set(sr, 15500.0f);
        outTilt.highShelf(sr, 2800.0f, 4.0f);
        spkHP.set(sr, 95.0f);
        spkLP1.set(sr, 4400.0f);
        spkLP2.set(sr, 5200.0f);
        spkPresence.highShelf(sr, 2600.0f, 3.0f);
        outLevel = std::pow(10.0f, 0.05f * (lvlBase + lvlGain*pGain - 0.4f*pVol));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        if (boostOn) {
            // TS-style: el KDS226 en el feedback del TL072 clipea a ~±0.6 V;
            // el op-amp recupera nivel — overdrive real antes de las valvulas.
            float b = boostClip.process(x * boostGain);
            x = boostLP.process(b * 1.6f);
        }
        float y = v1.process(x * g1);
        y = s2.process(cp1.process(y, g2));
        if (nStages >= 3) y = s3.process(cp2.process(y, g3));
        if (nStages >= 4) y = s4.process(cp3.process(y, g4));
        if (nStages >= 5) y = s5.process(cp4.process(y, g5));
        y = tone.process(y);                       // stack DESPUES de la distorsion
        const float vol = 0.20f + 0.80f * rbtube::PotTaper::audio(pVol, 1.1f);
        y = pi.process(y * vol);
        y = toneTilt.process(y);                   // TONE dual post-PI
        y = power.process(y);
        y = dynShelf.process(y);                   // DYNAMICS via NFB
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

} // namespace irt
#endif // IRONHEART_CORE_H
