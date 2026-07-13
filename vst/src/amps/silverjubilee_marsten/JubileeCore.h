#ifndef JUBILEE_CORE_H
#define JUBILEE_CORE_H
//
// JubileeCore — Marshall 2555 Silver Jubilee, circuit-real on the shared
// tube_stage.hpp framework + the AsymDiodeStringClipper from semiconductors.hpp.
//
// The Jubilee is JCM800-adjacent in its tubes and tone stack, but its voice
// comes from a DIODE CLIPPER in the preamp (LED3/LED4 + 3x 1N4007, asymmetric),
// NOT from pure tube clipping. That soft, asymmetric diode limiting is the
// singing, compressed, even-harmonic Jubilee sustain. The GAIN pull switch
// ("Rhythm Clip") raises the clip thresholds and symmetrises them for tighter
// chord work.
//
//   IN -> coupling -> V1A 12AX7 -> GAIN -> V1B 12AX7 -> DIODE CLIPPER ->
//   LEAD MASTER -> V2A 12AX7 (recovery) -> Marshall tone stack (Yeh) ->
//   presence -> LTP PI -> 4x EL34 push-pull -> OT roll -> ·makeup.
//   Runs 4x oversampled (the clipper + tubes are the nonlinearities).
//
#include "../../_shared/tube_stage.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

namespace jubilee {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
};

struct JubileeCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1a, v1b, v2;        // 12AX7: input, gain-driven, recovery
    Biquad brightShelf;                    // bright cap across the gain pot
    rbcomponents::AsymDiodeStringClipper clip;   // LED3/LED4 + 3x 1N4007 (asymmetric)
    rbtube::HP1 clipDcBlock;               // C21 (1u) block after the clipper node
    rbtube::ToneStackYeh tone;             // Marshall TMB (Yeh)
    Biquad presenceShelf, outTilt;         // power-amp NFB presence + amp-only top tilt
    rbtube::PhaseInverterLTP12AT7 pi;      // LTP phase inverter
    rbtube::PowerAmpEL34 power;            // 4x EL34 (~100W)
    rbtube::LP1 otVoice;
    rbtube::HP1 spkHP;                      // internal 4x12 cab-sim: thump corner
    rbtube::LP1 spkLP1, spkLP2;            // internal 4x12 cab-sim: cone roll-off
    Biquad spkPresence;                    // internal 4x12 cab-sim: 2.6k bite

    float pGain=.65f,pLead=.70f,pBass=.5f,pMid=.55f,pTreble=.55f,pPres=.55f,pMaster=.6f;
    float pRhythm=0.f, pCab=1.f;
    float gDrive=1.f, clipInDrive=1.f, leadLevel=1.f, piDrive=6.f, outLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setGain(float v){ pGain=clamp01(v); recalc(); }
    void setLeadMaster(float v){ pLead=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMiddle(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setMaster(float v){ pMaster=clamp01(v); recalc(); }
    void setRhythmClip(float v){ pRhythm=(v>=0.5f)?1.f:0.f; recalc(); }
    void setCabSim(float v){ pCab=clamp01(v); }

    void reset(){ inCoupling.reset(); v1a.reset(); v1b.reset(); v2.reset(); brightShelf.reset();
        clip.reset(); clipDcBlock.reset(); tone.reset(); presenceShelf.reset(); outTilt.reset();
        pi.reset(); power.reset(); otVoice.reset();
        spkHP.reset(); spkLP1.reset(); spkLP2.reset(); spkPresence.reset(); }

    void recalc(){
        constexpr float kGSpan = 9.0f;     // GAIN-pot span into V1B (feeds the clipper)
        constexpr float kHp    = 110.0f;   // input coupling HP (firm lows, a touch looser than the JCM800)
        inCoupling.set(sr, kHp);
        v1a.set(sr, 1, 250.0f, 40.0f, 25.0f, 1500.0f);   // V1A input 12AX7
        v1b.set(sr, 1, 250.0f, 40.0f, 22.0f, 2700.0f);   // V1B gain-driven 12AX7 (colder bias -> crunch edge)
        v2.set(sr,  1, 250.0f, 40.0f, 55.0f, 1500.0f);   // V2A recovery 12AX7

        // GAIN pot: controlled drive into V1B, then a fixed boost INTO the diode
        // clipper so the diodes actually limit (the Jubilee clips at the diodes,
        // not the tubes). Bright cap eases off as gain climbs.
        gDrive = 0.40f + kGSpan * rbtube::PotTaper::audio(pGain, 1.30f);
        // Drive INTO the diode clipper scales with Gain too: low Gain only kisses
        // the diode knees (edge-of-breakup, still some grit — the clipper is
        // always in circuit on a Jubilee), high Gain slams them (full singing
        // sustain). A fixed drive here over-clipped even at low Gain.
        clipInDrive = 1.3f + 3.0f * rbtube::PotTaper::audio(pGain, 1.30f);
        brightShelf.highShelf(sr, 2000.0f, 4.0f * (1.0f - pGain));

        // ── Diode clipper (LED3/LED4 + 3x 1N4007 D1-D3): ASYMMETRIC. LEDs (~1.8V)
        //    one polarity, a 3x silicon string (~2.1V) the other -> even harmonics,
        //    the singing Jubilee compression. Rhythm Clip (pull) raises + symmetrises
        //    the thresholds (D4/D5 added) for tighter, cleaner-headroom chord work. ──
        clip.setSpec(rbcomponents::diode1N4148());
        if (pRhythm >= 0.5f) { clip.setSeries(3, 3); clip.setSourceR(15000.0f); }  // rhythm: tighter, symmetric, more headroom
        else                 { clip.setSeries(2, 3); clip.setSourceR(9000.0f);  }  // lead: soft, asymmetric, compressed
        clipDcBlock.set(sr, 40.0f);   // C21 1u coupling out of the clip node

        // LEAD MASTER (VR2): post-clipper level into V2A. Higher = more V2 drive.
        leadLevel = 0.25f + 1.35f * rbtube::PotTaper::audio(pLead, 1.15f);

        // Marshall tone stack (Yeh): Treble 220k/470pF, Bass 1M/22nF, Mid 22k/22nF, slope 33k.
        tone.setComponents(220e3, 1e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9);
        tone.update(sr, pTreble, pMid, pBass);
        presenceShelf.highShelf(sr, 3000.0f, (pPres-0.5f)*10.0f);

        piDrive = 6.0f;
        pi.setFenderAB763(sr, 1.0f, 1.0f);

        // 4x EL34 (~100W): a touch more headroom + output than the JCM800's pair.
        const float vol = rbtube::PotTaper::audio(pMaster, 1.15f);
        power.set(sr, 0.5f + 2.4f*vol, -38.0f, 0.06f, 30.0f, 11000.0f);
        power.out = 0.011f;
        otVoice.set(sr, 16000.0f);
        outTilt.highShelf(sr, 2600.0f, 8.0f);

        // Internal 4x12 cab-sim voice (Cab Sim = 1): G12 close-mic-ish — HP thump
        // ~95 Hz, two-pole cone roll-off ~4.4 kHz, a presence bump at 2.6 kHz.
        spkHP.set(sr, 95.0f);
        spkLP1.set(sr, 4400.0f);
        spkLP2.set(sr, 5200.0f);
        spkPresence.highShelf(sr, 2600.0f, 3.0f);

        // Loudness makeup: DECREASES with Gain (and a touch with Lead Master) so the
        // knobs add distortion, not level; operating point sits ~-16 dBFS RMS.
        outLevel = std::pow(10.0f, 0.05f * (5.2f - 4.2f * pGain - 1.0f * pLead));
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1a.process(x);                          // V1A input (clean)
        y = v1b.process(brightShelf.process(y) * gDrive);  // V1B gain-driven
        y = clip.process(y * clipInDrive);                 // diode clipper (the Jubilee grind)
        y = clipDcBlock.process(y) * leadLevel;            // out of the clip node -> LEAD MASTER
        y = v2.process(y);                                 // V2A recovery
        y = tone.process(y);                               // Marshall TMB
        y = presenceShelf.process(y);
        y = pi.process(y * piDrive);                       // LTP PI
        y = power.process(y);                              // 4x EL34
        y = otVoice.process(y);
        y = outTilt.process(y);
        y *= outLevel;
        // Internal 4x12 cab-sim: blended in by Cab Sim (0 = amp-only).
        if (pCab > 0.0001f) {
            float c = spkHP.process(y);
            c = spkLP2.process(spkLP1.process(c));
            c = spkPresence.process(c) * 1.5f;   // makeup for the cone roll-off
            y = y + pCab * (c - y);
        }
        return y;
    }
};

} // namespace jubilee
#endif // JUBILEE_CORE_H
