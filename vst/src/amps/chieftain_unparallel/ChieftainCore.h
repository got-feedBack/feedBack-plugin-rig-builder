#ifndef CHIEFTAIN_CORE_H
#define CHIEFTAIN_CORE_H
//
// ChieftainCore - Matchless Chieftain Reverb (parody "Unparallel Chieftain"),
// REBUILT circuit-real / Guitarix-style from the hand-traced 7-page schematic
// (amps/Matchless Chieftan (BTQ-15)/Matchless-Chieftan-Schematic.pdf).
//
// EVERY audible stage is a REAL part (no tanh stand-ins except the final OT
// soft-clip ceiling): the two 12AX7 preamp triodes are rbtube::TubeStage
// (PURE Koren plate table + physical cathode auto-bias loop, Guitarix
// `tubestageF` topology, our tables), the Bass/Mid/Treble network is the real
// 3rd-order R/C transfer function (rbtube::ToneStackYeh, double precision), and
// the 2x EL34 push-pull power amp is rbtube::PowerAmpEL34 (pure EL34 pentode
// table, differential push-pull summed by the OT, cathode-bias sag). The amp
// runs at 2x oversampling (rbshared::Oversampler4x, OS=2) around the nonlinear
// chain, exactly like the BOX AC30 template (en30/BoxDC30Core.h).
//
// Schematic stage-by-stage (PAGE 1 / 2 / 4 of the hand-traced sheets):
//   IN -> 68K grid stopper + 1M grid leak                       (PAGE 1)
//   V1  12AX7  : 100K plate to 220V B+, 1.5K cathode (opt 25uF) (PAGE 1)
//   TMB tone stack : BASS 1M / MID 250K / TREBLE 1M, slope 100K,
//                    treble cap 5100pF (LARGE -> warm), 22nF bass/mid (PAGE 1)
//   VOLUME 250K  : the inter-stage drive control (= RS Gain)    (PAGE 1)
//   V2  12AX7  : 100K plate to 219V B+, 1.5K cathode + 25uF     (PAGE 1)
//   [Effects loop send/return - unity, unmodelled]              (PAGE 1)
//   V5  12AX7  long-tail phase inverter, MASTER 500K, BRILLIANCE
//               500K + .0047 cap (presence shelf), NO global NFB (PAGE 3)
//   V6/V7 2x EL34 push-pull, CATHODE-BIASED (270R 10W + 250uF,
//               ~24V cathode, ~415V plate -> hot class AB), 1K screen,
//               OT WTI 9356, GZ34 rect (gentle sag). 4/8/16 ohm.   (PAGE 4)
//   Spring REVERB : V3 12AX7 driver + tank + V4 12AX7 recovery,
//               REVERB LEVEL 100K, parallel return pre-PI.        (PAGE 2)
//
// Voice: Fender-meets-Marshall boutique CLEAN/crunch, big headroom -> stays
// clean far up the Volume dial; only two preamp stages + a hot class-AB EL34
// power section, so it is deliberately NOT over-saturated (unlike a Boogie).
//
// Tubes use OUR Koren tables (public physics model, not Guitarix GPL source).
// Tone-stack values cross-checked vs Guitarix src/faust/tonestack.dsp ts.ac30
// (Vox/Matchless family: 1M/1M treble/bass pots, 100K slope, 22nF caps); the
// Chieftain differs only in its 250K MID pot and the LARGE 5.1nF treble cap.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace chieftain {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// RBJ biquad (peaking / shelves / low/high-pass) -- the speaker/voicing color,
// identical machinery to the BoxDC30 template.
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void norm(float a0){ b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0; }
    void peaking(float sr,float f,float Q,float dB){ f=fminf(f,sr*0.49f); float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=1+al*A;b1=-2*c;b2=1-al*A; float a0=1+al/A;a1=-2*c;a2=1-al/A; norm(a0); }
    void lowShelf(float sr,float f,float dB){ f=fminf(f,sr*0.49f); float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)-(A-1)*c+2*rA*al); b1=2*A*((A-1)-(A+1)*c); b2=A*((A+1)-(A-1)*c-2*rA*al);
        float a0=(A+1)+(A-1)*c+2*rA*al; a1=-2*((A-1)+(A+1)*c); a2=(A+1)+(A-1)*c-2*rA*al; norm(a0); }
    void highShelf(float sr,float f,float dB){ f=fminf(f,sr*0.49f); float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)+(A-1)*c+2*rA*al); b1=-2*A*((A-1)+(A+1)*c); b2=A*((A+1)+(A-1)*c-2*rA*al);
        float a0=(A+1)-(A-1)*c+2*rA*al; a1=2*((A-1)-(A+1)*c); a2=(A+1)-(A-1)*c-2*rA*al; norm(a0); }
    void lowpass(float sr,float f,float Q){ f=fminf(f,sr*0.49f); float w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=(1-c)/2;b1=1-c;b2=(1-c)/2; float a0=1+al;a1=-2*c;a2=1-al; norm(a0); }
    void highpass(float sr,float f,float Q){ f=fminf(f,sr*0.49f); float w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=(1+c)/2;b1=-(1+c);b2=(1+c)/2; float a0=1+al;a1=-2*c;a2=1-al; norm(a0); }
};

// Compact mono spring reverb (2 damped combs + 2 allpass), band-limited like a
// real spring tank. The Chieftain reverb is OFF for songs (RS pins it to 0);
// this gives the REVERB knob something musical when dialled by hand. Mirrors the
// V3 driver / tank / V4 recovery loop as a parallel send returned before the PI.
class SpringReverb {
    static const int N1=1481, N2=1709, A1=229, A2=97;
    float c1[N1], c2[N2], ap1[A1], ap2[A2];
    int i1=0,i2=0,j1=0,j2=0; float lp1=0,lp2=0;
    Biquad inHp, inLp;
public:
    void setSampleRate(float sr){ inHp.highpass(sr,200.0f,0.7f); inLp.lowpass(sr,4500.0f,0.7f); reset(); }
    void reset(){ for(int i=0;i<N1;++i)c1[i]=0; for(int i=0;i<N2;++i)c2[i]=0;
        for(int i=0;i<A1;++i)ap1[i]=0; for(int i=0;i<A2;++i)ap2[i]=0;
        i1=i2=j1=j2=0; lp1=lp2=0; inHp.reset(); inLp.reset(); }
    float process(float x){
        x = inLp.process(inHp.process(x));
        float a=c1[i1]; lp1 += 0.42f*(a-lp1); c1[i1]= x + 0.80f*lp1; if(++i1>=N1)i1=0;
        float b=c2[i2]; lp2 += 0.42f*(b-lp2); c2[i2]= x + 0.76f*lp2; if(++i2>=N2)i2=0;
        float y=(a+b)*0.5f;
        float t1=ap1[j1]; float o1=-0.6f*y+t1; ap1[j1]= y+0.6f*o1; if(++j1>=A1)j1=0; y=rbtube::dn(o1);
        float t2=ap2[j2]; float o2=-0.6f*y+t2; ap2[j2]= y+0.6f*o2; if(++j2>=A2)j2=0; y=rbtube::dn(o2);
        return y;
    }
};

struct ChieftainCore {
    float sr = 48000.0f;
    rbtube::HP1 inputCoupling;
    rbtube::TubeStage v1, v2;           // two real cathode-biased 12AX7 stages
    rbtube::Miller12AX7 inputMiller;    // V1 input Miller loading
    rbtube::Miller12AX7 millerV2;       // tone/volume source -> V2 Miller loading
    rbtube::CouplingCapGridLeak coupleToV2; // tone/volume network -> V2 grid blocking
    rbtube::CouplingCapGridLeak coupleToPi; // master/brilliance -> PI grid blocking
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;           // GZ34 rectifier + B+ nodes
    rbtube::PowerAmpEL34 power;          // 2x EL34 push-pull, cathode-bias, NO NFB
    rbtube::ToneStackYeh tonestack;     // real Matchless TMB R/C network (Yeh model)
    Biquad brillShelf, presShelf, spkBody, spkLowMid, spkPres, spkRoll, spkLp, spkHp;
    SpringReverb spring;

    // params (0..1), interface identical to the old ChieftainCore / ChieftainParams.h
    float pVolume=0.55f, pBass=0.5f, pMid=0.5f, pTreble=0.55f,
          pBrilliance=0.40f, pMaster=0.70f, pReverb=0.0f, pCabSim=1.0f;
    float inScale=2.8f, preGain=1, gainOut=1, outLevel=1;
    float lastPowerLoad=0.0f, lastScreenLoad=0.0f, lastPreampLoad=0.0f;

    void setSampleRate(float s){ sr=s; spring.setSampleRate(s); recalc(); reset(); }
    void setVolume(float v){ pVolume=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setMid(float v){ pMid=clamp01(v); recalc(); }
    void setTreble(float v){ pTreble=clamp01(v); recalc(); }
    void setBrilliance(float v){ pBrilliance=clamp01(v); recalc(); }
    void setMaster(float v){ pMaster=clamp01(v); recalc(); }
    void setReverb(float v){ pReverb=clamp01(v); }
    void setCabSim(float v){ pCabSim=clamp01(v); }

    void reset(){ inputCoupling.reset(); inputMiller.reset(); v1.reset(); v2.reset(); millerV2.reset();
        coupleToV2.reset(); coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        tonestack.reset(); brillShelf.reset(); presShelf.reset();
        spkBody.reset(); spkLowMid.reset(); spkPres.reset(); spkRoll.reset(); spkLp.reset(); spkHp.reset();
        spring.reset(); lastPowerLoad=lastScreenLoad=lastPreampLoad=0.0f; }

    void recalc(){
        inputCoupling.set(sr, 12.0f);                              // input grid-leak coupling

        // TWO real cathode-biased 12AX7 stages, Chieftain values from the
        // schematic (PAGE 1): V1 sees the 68K grid stopper + 1M grid leak (Ri
        // table 0 = 68k), 1.5K cathode; V2 follows the high-Z tone stack so it
        // uses the 250k grid-leak table (Ri 1), 1.5K cathode. Both self-bias
        // (Vk0 solved from the load line) and saturate on their own load line;
        // the cathode loop makes the bias breathe with level. fck = 1/(2*pi*Rk*Ck)
        // with Rk=1.5k, Ck=25uF -> ~4.2 Hz (fully bypassed across the audio band).
        v1.set(sr, 0, 250.0f, 40.0f, 4.2f, 1500.0f);              // V1 (68k grid-leak, 1.5k cathode)
        v2.set(sr, 1, 250.0f, 40.0f, 4.2f, 1500.0f);              // V2 (250k grid-leak, 1.5k cathode)

        // VOLUME 250K (= RS Gain) is the linear inter-stage pre-gain (Guitarix
        // `*(preamp)` style). Turning it up drives the cascade into the EL34s
        // clean->crunch with NO ad-hoc curve. The Chieftain is the CLEAN /
        // big-headroom amp, so the pre-gain floor is generous (clean low) and the
        // ceiling modest (only light crunch at max) -- deliberately NOT a Boogie.
        float vol   = std::pow(pVolume, 1.1f);                    // 250K audio taper
        inScale     = 2.0f * (0.7f + 0.45f * pVolume);            // audio -> grid volts into V1 (keep V1 cleaner)
        preGain     = 0.70f + 3.6f * vol;                         // VOLUME inter-stage pre-gain (raised floor: usable at moderate Volume, not dead until cranked)
        gainOut     = 0.70f + 0.60f * vol;                        // post-V2 level into the PI / power amp
        inputMiller.set(sr,  68000.0f, 55.0f, 8.0f);              // input stopper + V1 Miller, ~25 kHz
        millerV2.set(sr,   180000.0f, 52.0f, 8.0f);               // tone/volume source into V2, ~9 kHz
        coupleToV2.set(sr, 1000000.0f, 22.0e-9f, 220000.0f,
                       0.11f, 0.36f, 0.85f);

        // Matchless TMB tone stack = the REAL R/C network (Yeh model), placed
        // after V1 (PAGE 1). Treble pot 1M, Bass pot 1M, Mid pot 250K, slope
        // 100K; treble cap 5100pF (LARGE -> warm/low treble, the boutique trait),
        // bass/mid caps 22nF. Cross-check Guitarix ts.ac30 = same 1M/1M/100k/22nF
        // family (Chieftain swaps in the 250k mid pot + 5.1nF treble cap).
        tonestack.setComponents(1.0e6, 1.0e6, 250.0e3, 100.0e3, 5100.0e-12, 22.0e-9, 22.0e-9);
        tonestack.update(sr, pTreble, pMid, pBass);

        coupleToPi.set(sr, 1000000.0f, 22.0e-9f, 100000.0f, 0.12f, 0.42f, 1.10f);
        phaseInverter.setMarshall(sr, 0.78f + 1.25f * pMaster + 0.45f * vol, 0.86f);
        supply.set(sr,
                   115.0f, 32.0f,
                   1200.0f, 32.0f,
                   10000.0f, 22.0f,
                   0.13f + 0.05f * vol,
                   0.095f + 0.035f * vol,
                   0.055f + 0.018f * pVolume,
                   0.20f);
        // 2x EL34 push-pull power amp (real pentode table, NO global NFB = the
        // Matchless chime). Cathode-biased (270R 10W + 250uF, ~24V cathode at
        // ~415V plate -> a fairly HOT class-AB point, big headroom). The bias is
        // hotter / sag a touch deeper than the Vox's fixed-ish EL84 -7.5; Master
        // + Volume together push it into power-amp breakup near the top.
        power.set(sr, 2.5f + 9.0f * vol + 4.0f * pMaster, -11.5f, 0.13f);
        power.out   = 0.0075f;                                    // scale plate-volt differential to signal
        outLevel    = 0.62f * (0.85f + 0.30f * pMaster);          // post-power makeup, tracks Master

        // BRILLIANCE 500K + .0047 cap on the PI (PAGE 3): a presence high-shelf.
        // Higher Brilliance = more top sparkle. (A real preamp/PI network, kept
        // separate from the tone stack because it IS separate in the amp.)
        brillShelf.highShelf(sr, 2600.0f, 8.0f * pBrilliance);
        // Long-tail-pair PI + interstage bandwidth limit, then the speaker color.
        presShelf.highShelf(sr, 3200.0f, 2.0f + 1.5f * pTreble);

        // 2x12 / 4x12 EL34 head voicing -- the amp's OWN speaker color, kept MILD
        // and PRE-CAB on purpose (the user's cab IR provides the speaker's low
        // bump, low-mid scoop and HF rolloff; baking the full curve here would
        // double up with the cab). Open, articulate boutique voice.
        spkHp.highpass(sr, 72.0f, 0.72f);                          // OT/speaker low rolloff
        spkBody.peaking(sr, 110.0f, 0.80f, 2.0f);                  // gentle low body
        spkLowMid.peaking(sr, 480.0f, 0.78f, 0.6f);                // light low-mid warmth
        spkPres.highShelf(sr, 2600.0f, -3.0f + 6.0f * pTreble);    // TREBLE-tracked presence (in-chain stack treble is small)
        spkRoll.highShelf(sr, 4200.0f, 4.0f);                      // boutique top chime (pre-cab)
        spkLp.lowpass(sr, 16000.0f, 0.62f);                        // gentle ultrasonic rolloff
    }

    inline float process(float x){
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        x = inputCoupling.process(x);
        x = v1.process(inputMiller.process(x) * inScale * bplus.preamp);          // audio -> grid volts -> V1
        x = tonestack.process(x);                                  // real Matchless TMB stack between V1 and V2
        x = coupleToV2.process(x, 0.70f + 1.6f * preGain);
        x = v2.process(millerV2.process(x) * preGain * bplus.preamp);             // Miller-loaded VOLUME pre-gain -> V2
        x *= gainOut;

        // --- spring reverb: parallel send, returned BEFORE the power amp, so the
        //     wet goes through the EL34s + speaker like the real amp. Off at 0. ---
        if (pReverb > 0.001f){
            float wet = spring.process(x);
            x += wet * (0.85f * pReverb);
        }

        // --- BRILLIANCE presence shelf on the long-tail PI (post-preamp) ---
        x = brillShelf.process(x);
        x = coupleToPi.process(x, 1.0f + 0.10f * preGain);
        lastPreampLoad = 0.10f * std::fabs(x) + 0.04f * pVolume;
        x = phaseInverter.process(x * bplus.preamp);
        lastPowerLoad = 0.78f * std::fabs(x) + 0.16f * pMaster + 0.16f * pVolume;
        lastScreenLoad = 0.50f * std::fabs(x) + 0.10f * pVolume;

        // --- 2x EL34 push-pull power amp (cathode-bias + B+ sag, no NFB) ---
        x = power.process(x * bplus.power * bplus.screen);

        // --- output transformer + 2x12/4x12 boutique voicing ---
        x = presShelf.process(x);
        float cab = spkHp.process(x);
        cab = spkBody.process(cab);
        cab = spkLowMid.process(cab);
        cab = spkPres.process(cab);
        cab = spkRoll.process(cab);
        cab = spkLp.process(cab);
        x += pCabSim * (cab - x);

        // Loudness flattening vs the VOLUME (= RS Gain): the game holds output
        // VOLUME ~fixed and varies distortion, so normalize across the gain sweep
        // -- a stronger makeup at low Volume (clean/quiet) decaying toward unity
        // as the preamp is driven and self-compresses. Master adds a mild swing.
        float gcDb = 40.371f - 76.307f * pVolume + 35.810f * pVolume * pVolume; // fit: lift the usable Volume range toward -16 dBFS (clean amp ramps in below ~0.5, peak-matched to the rest)
        if (gcDb > 20.0f) gcDb = 20.0f; else if (gcDb < -6.0f) gcDb = -6.0f;
        return x * outLevel * std::pow(10.0f, 0.05f * gcDb);
    }

    static inline float smoothstep01(float v){ v=clamp01(v); return v*v*(3.0f-2.0f*v); }
};

} // namespace chieftain
#endif // CHIEFTAIN_CORE_H
