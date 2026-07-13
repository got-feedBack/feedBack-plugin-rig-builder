#ifndef CHORUS_ENSEMBLE_CORE_H
#define CHORUS_ENSEMBLE_CORE_H
//
// ChorusEnsembleCore — Boss CE-1 Chorus Ensemble, from the factory service
// notes (ET-10D). MN3002 BBD (1024 stages, clock 60-200 kHz -> ~2.6..8.5 ms),
// TA7504S preamp, LFO-modulated clock, Q10 output LPF (kills clock leak, gives
// the dark BBD warmth), a noise-killer gate, and the STEREO "Ensemble" output.
//
//   IN -> preamp -> input LP -> BBD (delay set by clock + LFO) -> output LP
//      -> compander (BBD level ride) -> stereo mix:
//        CHORUS : L = dry + wet,  R = dry - wet   (the wide ensemble spread)
//        VIBRATO: L = R = wet     (100% wet pitch modulation)
//
// Mono core -> stereo out (process fills outL/outR). Runs at base rate.
//
#include <cmath>
#include "../../_shared/ChorusComponents.h"

namespace chorusensemble {

static constexpr float kPi = 3.14159265358979323846f;
static inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
static inline float dn(float v){ return std::fabs(v)<1.0e-15f?0.f:v; }

class ChorusEnsembleCore {
    float sampleRate=48000.f;
    float level=0.6f, intensity=0.62f, depth=0.5f, rate=0.45f;
    bool  vibrato=false;

    rbmod::HighPass  inputHP;
    rbmod::LowPass   inputLP, bbdLP1, bbdLP2;
    rbmod::DelayBuffer bbd;
    rbmod::BbdCompander compander;
    float lfoPhase=0.f;
    float pre=0.f, dcOut=0.f, dcx1=0.f;

    float lfoRateHz() const {
        if (vibrato) return 3.0f + 8.0f * rate;          // sine 3..11 Hz (325..90 ms)
        return 0.28f + 1.4f * intensity;                 // triangle, slow chorus sweep
    }

public:
    void setSampleRate(float sr){
        sampleRate = sr>1000.f?sr:48000.f;
        bbd.resizeForMs(sampleRate, 14.0f);
        inputHP.setHz(30.f, sampleRate);
        inputLP.setHz(6200.f, sampleRate);               // pre-BBD band limit
        bbdLP1.setHz(4800.f, sampleRate);                // Q10 output LPF (dark BBD)
        bbdLP2.setHz(5200.f, sampleRate);
        compander.setSampleRate(sampleRate, 20.0f);
        reset();
    }
    void setLevel(float v){ level=clamp01(v); }
    void setIntensity(float v){ intensity=clamp01(v); }
    void setDepth(float v){ depth=clamp01(v); }
    void setRate(float v){ rate=clamp01(v); }
    void setMode(float v){ vibrato=(v>=0.5f); }

    void reset(){
        inputHP.reset(); inputLP.reset(); bbdLP1.reset(); bbdLP2.reset();
        bbd.reset(); compander.reset(); lfoPhase=0.f; pre=dcOut=dcx1=0.f;
    }

    void process(float in, float& outL, float& outR){
        // ── LFO: triangle for chorus, sine for vibrato ──
        lfoPhase += lfoRateHz()/sampleRate;
        if (lfoPhase>=1.f) lfoPhase-=std::floor(lfoPhase);
        const float tri = 1.0f - 4.0f*std::fabs(lfoPhase-0.5f);   // -1..1 triangle
        const float sn  = std::sin(rbmod::kTwoPi*lfoPhase);
        const float lfo = vibrato ? sn : tri;

        // ── preamp (TA7504S): a touch of gain + gentle soft saturation ──
        float x = inputHP.process(in);
        pre += 0.5f*(x - pre);                     // mild slew/warmth
        x = std::tanh(1.12f*x);
        x = inputLP.process(x);

        // ── BBD delay: base + LFO-swept (MN3002, clock 60-200 kHz) ──
        const float baseMs = 5.2f;
        const float widthMs = vibrato ? (0.6f + 3.4f*depth)     // vibrato: deep sweep
                                      : (0.4f + 2.6f*intensity); // chorus: gentler
        const float delayMs = baseMs + widthMs*lfo;
        bbd.write(x);
        float wet = bbd.read(delayMs*0.001f*sampleRate);
        wet = bbdLP2.process(bbdLP1.process(wet));
        wet = compander.process(wet, 0.40f + 0.40f*(vibrato?depth:intensity));
        wet = dn(wet);

        // ── stereo Ensemble output ──
        float L, R;
        if (vibrato) {
            L = R = wet;                             // 100% wet pitch modulation
        } else {
            const float dry = x;
            const float w = wet * (0.55f + 0.45f*intensity);
            L = dry + w;                             // dry + wet
            R = dry - w;                             // dry - wet  = the wide CE-1 spread
        }
        // output level — near-unity (a modulation pedal shouldn't boost); the
        // dry+wet Ensemble sum already adds level, so the trim keeps it in
        // family with the other chorus pedals (~input level).
        const float lvl = 0.30f + 0.63f*level;
        outL = dn(L)*lvl;
        outR = dn(R)*lvl;
    }
};

} // namespace chorusensemble
#endif // CHORUS_ENSEMBLE_CORE_H
