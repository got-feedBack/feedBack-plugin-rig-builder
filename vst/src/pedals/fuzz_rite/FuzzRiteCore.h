#ifndef FUZZ_RITE_CORE_H
#define FUZZ_RITE_CORE_H
//
// FuzzRiteCore — Mosrite FuzzRite (silicon), from the FuzzDog V4 build doc
// (pedals/fuzzrite/FuzzRiteV3.pdf).
//
// Signal path (silicon BOM): IN -> C1(47n)/R1(1M) -> Q1 (BC337-16, grounded
// emitter, R2 470k collector-feedback bias, R3 470k load) -> C2(2n2) -> DEPTH
// pot -> C3(2n2) -> Q2 (BC337-16, grounded emitter, R4/R5 470k) -> VOL -> OUT,
// with C4(47n) feeding Q2's collector back to Q1's collector.
//
// The two grounded-emitter Si stages have no degeneration, so they slam into
// hard transistor clipping — an aggressive, buzzy square-ish fuzz. The tiny 2n2
// interstage caps high-pass everything hard, giving the thin, nasal 60s voice.
// DEPTH sits between Q1's C2-coupled collector signal and the node feeding Q2
// through C3; its wiper feeds VOL. Q2's collector is not an output of the pot:
// it returns to Q1's collector through C4 and changes the clipping loop.
//
// Runs at the oversampled rate (2x, set by the plugin). Transistor clipping
// only — the FuzzRite has no clipping diodes (D1 is reverse-polarity power
// protection).
//
#include <cmath>

namespace fuzzrite {

static constexpr float kPi = 3.14159265358979323846f;
static inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
static inline float dn(float v){ return std::fabs(v)<1.0e-15f?0.f:v; }

class RcHighPass {
    float a=0.f, x1=0.f, y1=0.f;
public:
    void setRC(float sr, float r, float c){ const float s=sr>1000.f?sr:48000.f, rc=r*c, dt=1.f/s; a=rc/(rc+dt); }
    void setHz(float sr, float hz){ const float s=sr>1000.f?sr:48000.f, rc=1.f/(2.f*kPi*hz), dt=1.f/s; a=rc/(rc+dt); }
    void reset(){ x1=y1=0.f; }
    inline float process(float x){ const float y=a*(y1+x-x1); x1=x; y1=dn(y); return y1; }
};

class RcLowPass {
    float a=0.f, y=0.f;
public:
    void setHz(float sr, float hz){ const float s=sr>1000.f?sr:48000.f; a=1.f-std::exp(-2.f*kPi*hz/s); }
    void reset(){ y=0.f; }
    inline float process(float x){ y+=a*(x-y); y=dn(y); return y; }
};

class DcBlock {
    float x1=0.f, y1=0.f;
public:
    void reset(){ x1=y1=0.f; }
    inline float process(float x){ const float y=x-x1+0.9985f*y1; x1=x; y1=dn(y); return y1; }
};

// Saturating asymmetric BJT common-emitter transfer (collector current vs base
// drive, saturating toward the supply rails) — the transistor's own clipping.
static inline float bjtCurve(float v, float posK, float negK, float posRail, float negRail){
    if (v>=0.f) return posRail*(1.f-std::exp(-posK*v));
    return -negRail*(1.f-std::exp(negK*v));
}
static inline float bjtStage(float x, float drive, float bias, float posK, float negK, float posRail, float negRail){
    const float idle = bjtCurve(bias, posK, negK, posRail, negRail);
    return bjtCurve(bias + drive*x, posK, negK, posRail, negRail) - idle;
}

class FuzzRiteCore {
    float sampleRate = 48000.f;
    float depth = 0.62f, volume = 0.70f;

    RcHighPass inHP, c2HP, c3HP, c4FB;
    RcLowPass  outLP;
    DcBlock    dcA, dcB;

    float q2fb = 0.f;                 // C4 collector->collector feedback (1-sample state)

    void updateComponents(){
        inHP.setRC(sampleRate, 1.0e6f, 47.0e-9f);   // C1/R1 ~3 Hz — really just a DC block
        // Both coupling caps are 2n2. C2 sees the 500k DEPTH network; C3 sees
        // Q2's lower input impedance and therefore has the higher corner.
        c2HP.setHz(sampleRate, 160.0f);
        c3HP.setHz(sampleRate, 480.0f);
        c4FB.setHz(sampleRate, 720.0f);              // C4 feedback path band-shaping
        // BC337 collector capacitance and the 470k collector networks provide
        // the only HF rounding in the real pedal. Keep this above the guitar
        // band instead of adding the old arbitrary 6.2 kHz de-fizz filter.
        outLP.setHz(sampleRate, 14500.0f);
    }

public:
    void setSampleRate(float sr){ sampleRate = sr>1000.f?sr:48000.f; reset(); }
    void reset(){
        inHP.reset(); c2HP.reset(); c3HP.reset(); c4FB.reset(); outLP.reset();
        dcA.reset(); dcB.reset(); q2fb=0.f; updateComponents();
    }
    void setDepth(float v){ depth = clamp01(v); }
    void setVolume(float v){ volume = clamp01(v); }

    inline float process(float in){
        float x = inHP.process(in);

        // Q1 and Q2 form a fixed AC feedback loop through C4. Keep the loop
        // below unity in the discrete-time model; the previous 0.08 feedback
        // coefficient could burst into numerical gating on transients.
        const float in1 = x - 0.018f*q2fb;
        float A = -bjtStage(in1, 46.0f, -0.020f, 1.25f, 1.05f, 0.95f, 0.80f);
        A = dcA.process(A);

        // C2 interstage high-pass (thins into the nasal voice)
        const float aHP = c2HP.process(A);

        // The far end of DEPTH feeds Q2 through C3 and R8=22k. Q2 drive is
        // fixed by that network; moving the wiper does not alter it.
        const float thinNode = c3HP.process(aHP);
        const float q2in = 0.050f*thinNode;
        float B = -bjtStage(q2in, 24.0f, 0.016f, 1.15f, 1.02f, 0.92f, 0.78f);
        B = dcB.process(B);
        q2fb = c4FB.process(B);                       // C4 feedback into Q1 (next sample)

        // The 500KB DEPTH wiper selects between the fuller C2 side and the
        // thinner Q2-input side. They have the same polarity, so the sweep
        // cannot create the non-physical cancellation hole of the old model.
        float out = (1.0f-depth)*aHP + depth*thinNode;

        out = outLP.process(out);
        return dn(out);
    }

    float outLevel() const { return volume; }
};

} // namespace fuzzrite
#endif // FUZZ_RITE_CORE_H
