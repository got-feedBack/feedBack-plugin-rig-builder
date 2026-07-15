#ifndef MICRO_AMP_CORE_H
#define MICRO_AMP_CORE_H
//
// MicroAmpCore — MXR Micro Amp (M133), from the GGG schematic
// (pedals/MicroAmp/ggg_mamp_sc.pdf).
//
// A single TL061 JFET op-amp in a non-inverting boost:
//   IN -> C1(0.1u)/R1(22M) -> op-amp + input (biased at half supply via R7/R8)
//      -> gain = 1 + R4/(R5 + R6), R4 = 56k, R6 = 2k7, R5 = 500k GAIN pot
//      -> C5(15u)/R10(10k) -> OUT.
// C2 (47pF) across R4 rolls off ultrasonics for stability. GAIN sweeps the
// closed-loop gain from ~unity (R5 = 500k -> ~+0.9 dB) to ~+26 dB (R5 = 0 ->
// 1 + 56k/2k7 ~= 21.7x). Clean and transparent (22M input impedance doesn't
// load the guitar); driven hard into a hot signal the low-slew, low-GBW TL061
// rails and clips softly. One knob (GAIN).
//
// The op-amp's own gain-bandwidth roll-off, slew limiting and rail clipping are
// modelled by rbshared::OpAmpStage(tl061). Runs at the oversampled rate so the
// soft clip at high GAIN stays clean.
//
#include <cmath>
#include "../../_shared/opamp.hpp"

namespace microamp {

static constexpr float kPi = 3.14159265358979323846f;
static inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
static inline float dn(float v){ return std::fabs(v)<1.0e-15f?0.f:v; }

// 1-pole DC block (input C1/R1 and output C5/R10 are both ~1 Hz corners).
class DcBlock {
    float x1=0.f, y1=0.f, a=0.9995f;
public:
    void setHz(float sr, float hz){ const float s=sr>1000.f?sr:48000.f, rc=1.f/(2.f*kPi*hz); a=rc/(rc+1.f/s); }
    void reset(){ x1=y1=0.f; }
    inline float process(float x){ const float y=a*(y1+x-x1); x1=x; y1=dn(y); return y1; }
};

class MicroAmpCore {
    float sampleRate = 48000.f;
    float gain = 3.6f;

    rbshared::OpAmpStage op;
    DcBlock inDc, outDc;
    float feedbackLp=0.0f, feedbackA=0.0f;
    float stabilityLp=0.0f, stabilityA=1.0f;

    void updateGain(float knob){
        // R5 is 500k reverse-log. At half rotation about 15% of its resistance
        // remains in the feedback leg; this is not a dB-linear interpolation.
        const float k = clamp01(knob);
        const float r5 = 500000.0f * std::pow(1.0f-k, 2.73696559f);
        const float returnR = 2700.0f + r5;
        gain = 1.0f + 56000.0f/returnR;

        // C3 4.7u makes the feedback gain return to unity at DC.
        const float fbHz = 1.0f/(2.0f*kPi*returnR*4.7e-6f);
        feedbackA = 1.0f-std::exp(-2.0f*kPi*fbHz/sampleRate);

        // C2 47p across R4=56k is the real ultrasonic stability pole.
        const float stabHz = 1.0f/(2.0f*kPi*56000.0f*47.0e-12f);
        stabilityA = 1.0f-std::exp(-2.0f*kPi*stabHz/sampleRate);
    }

public:
    void setSampleRate(float sr){
        sampleRate = sr>1000.f?sr:48000.f;
        op.setSpec(rbshared::tl061Spec());
        op.setSampleRate(sampleRate);
        inDc.setHz(sampleRate, 0.08f);  // C1 100n / R1 22M
        outDc.setHz(sampleRate, 1.02f); // C5 15u / (R9+R10)
        reset();
    }
    void reset(){ op.reset(); inDc.reset(); outDc.reset(); feedbackLp=stabilityLp=0.0f; }
    void setGain(float knob){ updateGain(knob); }

    inline float process(float in){
        const float x = inDc.process(in);
        feedbackLp += feedbackA*(x-feedbackLp);
        const float target = x + (x-feedbackLp)*(gain-1.0f);
        float y = op.process(target, gain);
        stabilityLp += stabilityA*(y-stabilityLp);
        y = stabilityLp;
        y = outDc.process(y);
        return dn(0.955f*y); // R9 470R into R10 10k
    }
};

} // namespace microamp
#endif // MICRO_AMP_CORE_H
