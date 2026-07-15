#ifndef CHORUS_ENSEMBLE_CORE_H
#define CHORUS_ENSEMBLE_CORE_H
//
// ChorusEnsembleCore — Boss CE-1 Chorus Ensemble, from the factory service
// notes (ET-10D) + the MN3002 datasheet. MN3002 BBD = 512 stages (delay
// 0.32-25.6 ms over its 10-800 kHz clock range; the CE-1 clocks it 60-200 kHz
// -> ~1.3..4.3 ms), THD 0.4%, S/N 70 dB, insertion loss ~10 dB, response
// fi <= 0.3*fcp. TA7136P preamp, LFO-modulated clock, Q10 output LPF (kills
// clock leak + the dark BBD warmth), noise-killer gate, STEREO "Ensemble" out.
//
//   IN -> preamp -> input LP -> BBD (delay set by clock + LFO) -> output LP
//      -> transistor Noise Killer -> jack-switched mono/stereo outputs.
//
// Mono core -> stereo out (process fills outL/outR). Runs at base rate.
//
#include <cmath>
#include "../../_shared/ChorusComponents.h"
#include "../../_shared/opamp.hpp"

namespace chorusensemble {

static constexpr float kPi = 3.14159265358979323846f;
static inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
static inline float dn(float v){ return std::fabs(v)<1.0e-15f?0.f:v; }

class ChorusEnsembleCore {
    float sampleRate=48000.f;
    float level=0.6f, intensity=0.62f, depth=0.5f, rate=0.45f;
    bool  vibrato=false, effectOn=true, inputHigh=false;

    rbmod::HighPass  inputHP;
    rbmod::LowPass   inputLP, bbdLP1, bbdLP2;
    rbmod::DelayBuffer bbd;
    rbshared::OpAmpStage preamp;
    float lfoPhase=0.f;
    float nkEnv=0.f, nkGain=0.f, nkAtk=0.f, nkRel=0.f;

    float lfoRateHz() const {
        // Vibrato re-fit 2026-07-15 against the UAD CE-1 renders (band-energy
        // LFO extraction over the Brit DI): min/half/max measured 3.4/5.4/12.4
        // Hz — a quadratic taper, not linear (the old linear map ran 7.1 Hz at
        // half, audibly too fast mid-knob).
        if (vibrato) return 3.4f + 9.0f*rate*rate;       // 3.4..12.4 Hz (UAD-matched)
        return 0.417f + 2.66f*intensity;                 // 2.4 s..325 ms (matches UAD 1.9/2.8 half/max)
    }

public:
    void setSampleRate(float sr){
        sampleRate = sr>1000.f?sr:48000.f;
        bbd.resizeForMs(sampleRate, 14.0f);
        inputHP.setHz(30.f, sampleRate);
        inputLP.setHz(6200.f, sampleRate);               // pre-BBD band limit
        bbdLP1.setHz(4800.f, sampleRate);                // Q10 output LPF (dark BBD)
        bbdLP2.setHz(5200.f, sampleRate);
        preamp.setSpec(rbshared::ta7136apSpec());
        preamp.setSampleRate(sampleRate);
        nkAtk=1.f-std::exp(-1.f/(0.004f*sampleRate));
        nkRel=1.f-std::exp(-1.f/(0.160f*sampleRate));
        reset();
    }
    void setLevel(float v){ level=clamp01(v); }
    void setIntensity(float v){ intensity=clamp01(v); }
    void setDepth(float v){ depth=clamp01(v); }
    void setRate(float v){ rate=clamp01(v); }
    void setMode(float v){ vibrato=(v>=0.5f); }
    void setEffect(float v){ effectOn=(v>=0.5f); }
    void setInputSens(float v){ inputHigh=(v>=0.5f); }

    void reset(){
        inputHP.reset(); inputLP.reset(); bbdLP1.reset(); bbdLP2.reset();
        bbd.reset(); preamp.reset(); lfoPhase=0.f; nkEnv=nkGain=0.f;
    }

    void process(float in, float& outL, float& outR){
        // ── LFO: triangle for chorus, sine for vibrato ──
        lfoPhase += lfoRateHz()/sampleRate;
        if (lfoPhase>=1.f) lfoPhase-=std::floor(lfoPhase);
        const float tri = 1.0f - 4.0f*std::fabs(lfoPhase-0.5f);   // -1..1 triangle
        const float sn  = std::sin(rbmod::kTwoPi*lfoPhase);
        const float lfo = vibrato ? sn : tri;

        // TA7136P input stage. LEVEL is the real 50KA input pot, not a final
        // digital output trim. LOW attenuates instrument input before the high
        // closed-loop-gain preamp; HIGH exposes its characteristic overload.
        float x = inputHP.process(in);
        const float pot=std::pow(level,1.7f);
        const float sensitivity=inputHigh?1.f:0.22f;
        const float preGain=20.f;
        x=preamp.process(x*pot*sensitivity*preGain,preGain);
        x = inputLP.process(x);

        // ── BBD delay (MN3002 = 512 STAGES per datasheet, NOT 1024): delay =
        //    512/(2·fcp). At the CE-1's 60-200 kHz clock -> ~1.3..4.3 ms.
        //    Centre ~2.9 ms, LFO-swept within that window. ──
        const float clockCentre=100000.f;
        // Sweep spans re-fit 2026-07-15 against the UAD CE-1 renders:
        // - VIBRATO: DEPTH^1.5 taper up to the FULL BBD range (UAD's max sweep
        //   tracked ~3 ms p2p = the whole 1.3-4.3 ms MN3002 window). Default
        //   depth 0.5 lands ~±28 cents; the old linear map hit ±71 at default.
        // - CHORUS: FIXED wide sweep — on the real unit (and UAD) INTENSITY
        //   only moves the LFO RATE; scaling the sweep width by it left low
        //   intensities with almost no modulation (the "muy sutil" report).
        const float clockSpan=vibrato?(0.02f+0.48f*std::pow(depth,1.5f)):0.35f;
        float clockHz=clockCentre*(1.f+clockSpan*lfo);
        if(clockHz<60000.f) clockHz=60000.f;
        if(clockHz>200000.f) clockHz=200000.f;
        const float delayMs=1000.f*512.f/(2.f*clockHz);
        bbd.write(x);
        float wet = bbd.read(delayMs*0.001f*sampleRate);
        wet = bbdLP2.process(bbdLP1.process(wet));

        // MN3002 typical THD is about 0.4%; apply it before the discrete
        // transistor Noise Killer. The gate follows the input and only removes
        // the low-level BBD floor, rather than riding musical dynamics.
        wet += 0.004f*(wet*wet*wet-wet);
        nkEnv += (std::fabs(x)>nkEnv?nkAtk:nkRel)*(std::fabs(x)-nkEnv);
        const float gateTarget=clamp01((nkEnv-0.00018f)/0.0012f);
        nkGain += (gateTarget>nkGain?nkAtk:nkRel)*(gateTarget-nkGain);
        wet *= nkGain;
        wet = dn(wet);

        // ── stereo Ensemble output ──
        float L, R;
        if (!effectOn) {
            // NORMAL: effect off, but the preamp still colours the signal — the
            // CE-1's famous "preamp only" path (buffered, warm, mono).
            L = R = x;
        } else if (vibrato) {
            L = wet;                                 // mono jack: effect
            R = x;                                   // stereo jack: direct path
        } else {
            const float dry = x;
            // EQUAL dry+wet mix, like the real unit: the BBD wet returns through
            // the compander at unity and the mix resistors are equal. INTENSITY
            // only moves the LFO (rate+depth) — scaling the wet level by it made
            // the chorus audibly subtle (user report; measured only 12 dB of
            // comb AM vs >18 dB with the equal mix).
            L = 0.70f*(dry+wet);                     // switched MONO jack sum
            R = dry;                                 // second jack exposes direct path
        }
        outL=dn(L); outR=dn(R);
    }
};

} // namespace chorusensemble
#endif // CHORUS_ENSEMBLE_CORE_H
