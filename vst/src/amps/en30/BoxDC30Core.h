#ifndef BOX_DC30_CORE_H
#define BOX_DC30_CORE_H
//
// BoxDC30Core - Vox AC30 Top Boost (parody "BOX DC30"), REBUILT Guitarix-style:
// the signal flow is a clean feed-forward cascade of Guitarix-style tube stages
// (anti-alias -> Koren tube table -> per-stage DC-block), so it is stable at any
// (oversampled) rate -- unlike the old white-box EN30Core which blew up at 192k.
// Component-level voicing (AC30 Top Boost stack, Cut, bright, EL84 push-pull power
// + sag, 2x12 voicing) sits BETWEEN the tube stages as ordinary stable biquads.
// Tubes use OUR Koren tables (public model). Drop-in param interface == EN30Core.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace boxdc30 {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// RBJ biquad (peaking / shelves / low-pass)
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void norm(float a0){ b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0; }
    void peaking(float sr,float f,float Q,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=1+al*A;b1=-2*c;b2=1-al*A; float a0=1+al/A;a1=-2*c;a2=1-al/A; norm(a0); }
    void lowShelf(float sr,float f,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)-(A-1)*c+2*rA*al); b1=2*A*((A-1)-(A+1)*c); b2=A*((A+1)-(A-1)*c-2*rA*al);
        float a0=(A+1)+(A-1)*c+2*rA*al; a1=-2*((A-1)+(A+1)*c); a2=(A+1)+(A-1)*c-2*rA*al; norm(a0); }
    void highShelf(float sr,float f,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)+(A-1)*c+2*rA*al); b1=-2*A*((A-1)+(A+1)*c); b2=A*((A+1)+(A-1)*c-2*rA*al);
        float a0=(A+1)-(A-1)*c+2*rA*al; a1=2*((A-1)-(A+1)*c); a2=(A+1)-(A-1)*c-2*rA*al; norm(a0); }
    void lowpass(float sr,float f,float Q){ f=fminf(f,sr*0.49f); float w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=(1-c)/2;b1=1-c;b2=(1-c)/2; float a0=1+al;a1=-2*c;a2=1-al; norm(a0); }
};

struct BoxDC30Core {
    float sr = 48000.0f;
    rbtube::HP1 inputCoupling;
    rbtube::TubeStage v1, v2, v3;       // three real cathode-biased 12AX7 stages
    rbtube::LP1 couple12, couple23;     // inter-stage coupling rolloff (Guitarix lowpass(6531))
    rbtube::PowerAmpPP power;            // real class-A push-pull EL84 (no NFB, AC30)
    rbtube::ToneStackYeh tonestack;     // real Vox Top Boost R/C tone network (Yeh model)
    Biquad bright, cutLP, spkBody, spkPres, spkRoll;
    // params (0..1), interface identical to EN30Core
    float pInput=0.5f, pNVol=0.7f, pTBVol=0.7f, pTreble=0.5f, pBass=0.5f, pBright=0.5f,
          pCut=0.5f, pMaster=0.7f, pRevTone=0.5f, pRevLevel=0.0f, pSpeed=0.5f, pDepth=0.0f;
    float inGain=1, masterDrive=1, outLevel=1, lfoPhase=0, lfoInc=0;
    float inScale=4, preGain=1, gainOut=1;     // audio->grid-volts + inter-stage pre-gains

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setInput(float v){ pInput=clamp01(v); recalc(); }
    void setNormalVol(float v){ pNVol=clamp01(v); recalc(); }
    void setTBVol(float v){ pTBVol=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setBright(float v){ pBright=clamp01(v); recalc(); }
    void setCut(float v){ pCut=clamp01(v); recalc(); }
    void setMaster(float v){ pMaster=clamp01(v); recalc(); }
    void setRevTone(float v){ pRevTone=clamp01(v); }
    void setRevLevel(float v){ pRevLevel=clamp01(v); }
    void setSpeed(float v){ pSpeed=clamp01(v); recalc(); }
    void setDepth(float v){ pDepth=clamp01(v); }
    void reset(){ inputCoupling.reset(); v1.reset(); v2.reset(); v3.reset(); couple12.reset(); couple23.reset(); power.reset();
        tonestack.reset(); bright.reset(); cutLP.reset();
        spkBody.reset(); spkPres.reset(); spkRoll.reset(); lfoPhase=0; }

    void recalc(){
        inputCoupling.set(sr, 12.0f);                              // input grid-leak coupling
        // THREE real cathode-biased 12AX7 stages, AC30 Top Boost values (same Ri/Rk/
        // fck Guitarix uses for the 12ax7): V1 grid-leak 68k Rk2.7k, V2/V3 250k with
        // Rk 1.5k/820. Each self-biases (Vk0 solved) and saturates on its own load
        // line; the cathode loop makes it breathe. The Top Boost VOLUME is the linear
        // inter-stage pre-gain (Guitarix `*(preamp)`), so raising it drives the
        // cascade clean->crunch->plateau with NO ad-hoc curve.
        inGain      = 0.40f + 1.6f * pInput;                       // input drive
        v1.set(sr, 0, 250.0f, 40.0f, 86.0f,  2700.0f);             // V1 (68k grid-leak)
        v2.set(sr, 1, 250.0f, 40.0f, 132.0f, 1500.0f);             // V2 (250k)
        v3.set(sr, 1, 250.0f, 40.0f, 194.0f, 820.0f);              // V3 (250k, hottest)
        float vol   = std::pow(pTBVol, 1.1f);                      // volume taper: clean at low, cooks only as you turn up
        inScale     = 2.0f * (0.7f + 0.6f * pNVol);                // audio -> grid volts into V1 (keep V1 cleaner)
        preGain     = 0.35f + 3.5f * vol;                          // inter-stage pre-gain: low floor (clean low vol) + high slope (cooks high)
        gainOut     = 0.60f + 0.55f * vol;                         // post-V3 level into the power amp
        couple12.set(sr, 9000.0f);                                 // inter-stage Miller rolloff (more top/chime -> Treble has room to work)
        couple23.set(sr, 9000.0f);
        // Vox Top Boost tone stack = the REAL R/C network (Yeh model), not biquads.
        // AC30 has Treble + Bass knobs (no Mid): Treble->t; the circuit's "mid" param is
        // the body/scoop control (the literal bass-pot node is inactive at Vox values),
        // so Bass knob -> m. l fixed. Bright cap + Cut stay separate (they are, in the amp).
        bright.highShelf(sr, 2400.0f, 9.0f * pBright);             // Brilliance/Bright cap (pre tone stack)
        tonestack.setComponents(220e3, 220e3, 220e3, 100e3, 470e-12, 100e-9, 47e-9); // Vox AC15/AC30
        tonestack.update(sr, pTreble, 0.15f + 0.55f * pBass, 0.5f);
        cutLP.lowpass(sr, 900.0f + 4200.0f * (1.0f - pCut), 0.7f); // Cut: bites the presence region 5.1k(no cut)->0.9k(full)
        // EL84 class-A push-pull power amp (real pentode, no NFB). Master pushes it into
        // power-amp breakup; bias fixed at the class-A point, sag/cathode handle compression.
        power.set(sr, 3.5f + 16.0f * vol + 2.0f * pMaster, -7.5f, 0.40f); // volume cooks the power amp (clean floor, hot top)
        power.out   = 0.0075f;                                     // scale plate-volt differential to signal
        masterDrive = 1.0f;
        outLevel    = 0.5f * (1.0f - 0.45f * pTBVol);              // level comp: keep loudness ~constant across gain
        // 2x12 AC30 voicing (Celestion-ish) — amp's own speaker color (cab IR adds the cab)
        // Speaker voicing kept MILD / PRE-CAB on purpose: the amp is voiced before the
        // cabinet, and the user's cab IR provides the speaker's low bump, low-mid scoop
        // and HF rolloff. Baking the full speaker curve here would double up with the cab.
        // (So the bare-amp spectrum is intentionally NOT the Ruby-with-speaker curve.)
        spkBody.peaking(sr, 110.0f, 0.8f, 2.5f);                   // gentle low body
        spkPres.highShelf(sr, 2500.0f, -4.0f + 9.0f * pTreble);    // TREBLE knob: ~9dB shelf (the real tonestack treble is only ~2dB in-chain)
        spkRoll.highShelf(sr, 4000.0f, 5.0f);                      // AC30 top chime (pre-cab)
        lfoInc = (3.0f + 8.0f * pSpeed) / sr;                      // tremolo 3..11 Hz
    }

    inline float process(float x){
        x = inputCoupling.process(x * inGain);
        x = bright.process(x);
        x = v1.process(x * inScale);                               // audio -> grid volts -> V1
        x = tonestack.process(x);                                  // real Vox tone stack between V1 and V2
        x = v2.process(couple12.process(x) * preGain);             // coupling + pre-gain -> V2
        x = v3.process(couple23.process(x) * preGain);             // coupling + pre-gain -> V3
        x *= gainOut;
        x = power.process(x);
        x = cutLP.process(x);                                      // Cut AFTER power amp (real AC30: tames the output treble, post-distortion)
        x = spkRoll.process(spkPres.process(spkBody.process(x)));  // 2x12 voicing
        if (pDepth > 0.0f){                                        // tremolo (amplitude)
            lfoPhase += lfoInc; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
            float lfo = 0.5f * (1.0f + std::sin(2.0f * kPi * lfoPhase));
            x *= 1.0f - 0.9f * pDepth * lfo;
        }
        // loudness flattening vs the Top Boost Volume/gain (clean post-output makeup;
        // anchored ~0 dB at Vol 0.5 — the AC30 vol IS the gain, ~16 dB swing without this).
        float gcDb = 25.288f - 78.409f * pTBVol + 54.825f * pTBVol * pTBVol;
        if (gcDb > 20.0f) gcDb = 20.0f; else if (gcDb < -12.0f) gcDb = -12.0f;
        return x * outLevel * std::pow(10.0f, 0.05f * gcDb);
    }
};

} // namespace boxdc30
#endif // BOX_DC30_CORE_H
