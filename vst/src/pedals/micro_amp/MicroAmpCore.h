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

    void updateGain(float knob){
        // gain = 1 + 56k/(R5 + 2k7); the 500k reverse-log pot makes the sweep
        // ~linear in dB, so map it straight to a dB-linear glide between the
        // circuit's real endpoints.
        const float minG = 1.0f + 56000.0f/(500000.0f + 2700.0f);   // ~1.111 (+0.9 dB)
        const float maxG = 1.0f + 56000.0f/2700.0f;                  // ~21.74 (+26.7 dB)
        gain = minG * std::pow(maxG/minG, clamp01(knob));
    }

public:
    void setSampleRate(float sr){
        sampleRate = sr>1000.f?sr:48000.f;
        op.setSpec(rbshared::tl061Spec());
        op.setSampleRate(sampleRate);
        inDc.setHz(sampleRate, 1.0f);
        outDc.setHz(sampleRate, 1.0f);
        reset();
    }
    void reset(){ op.reset(); inDc.reset(); outDc.reset(); }
    void setGain(float knob){ updateGain(knob); }

    inline float process(float in){
        const float x = inDc.process(in);
        // non-inverting op-amp: ideal output = x * gain, then the TL061's own
        // bandwidth / slew / rail clipping shape it.
        float y = op.process(x*gain, gain);
        y = outDc.process(y);
        return dn(y);
    }
};

} // namespace microamp
#endif // MICRO_AMP_CORE_H
