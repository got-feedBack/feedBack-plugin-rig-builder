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
// Q1 inverts and Q2 inverts again, so Q1's and Q2's outputs are OUT OF PHASE:
// the DEPTH pot crossfades between them and the partial cancellation is the
// Fuzz Rite's honk/spit/gate. The C4 collector-to-collector feedback adds the
// sputtery, dying-note instability (scaled by DEPTH so it only bites when the
// fuzz character is dialled up).
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

    RcHighPass inHP, c2HP, c3HP, c4FB, outHP;
    RcLowPass  outLP;
    DcBlock    dcA, dcB;

    float q2fb = 0.f;                 // C4 collector->collector feedback (1-sample state)

    void updateComponents(){
        inHP.setRC(sampleRate, 1.0e6f, 47.0e-9f);   // C1/R1 ~3 Hz — really just a DC block
        // Both interstage caps are 2n2, but they see different loads: C2 drives
        // the high-Z DEPTH pot / output (fuller, lower corner) while C3 drives
        // Q2's lower-Z base (thinner, higher corner). So Q1's path (dominant at
        // low DEPTH) stays thick and Q2's path (dominant at high DEPTH) is thin —
        // which is what makes DEPTH sweep from a fat fuzz to the nasal honk.
        c2HP.setHz(sampleRate, 160.0f);
        c3HP.setHz(sampleRate, 480.0f);
        c4FB.setHz(sampleRate, 720.0f);              // C4 feedback path band-shaping
        outLP.setHz(sampleRate, 6200.0f);            // tame the harshest square-wave fizz
        outHP.setHz(sampleRate, 60.0f);
    }

public:
    void setSampleRate(float sr){ sampleRate = sr>1000.f?sr:48000.f; reset(); }
    void reset(){
        inHP.reset(); c2HP.reset(); c3HP.reset(); c4FB.reset(); outLP.reset(); outHP.reset();
        dcA.reset(); dcB.reset(); q2fb=0.f; updateComponents();
    }
    void setDepth(float v){ depth = clamp01(v); }
    void setVolume(float v){ volume = clamp01(v); }

    inline float process(float in){
        float x = inHP.process(in);

        // ── Q1: grounded-emitter Si stage, HIGH gain, inverting. C4 couples
        //    Q2's collector back here (fixed — the cap is always in circuit);
        //    a SMALL low-frequency feedback term, part of the FuzzRite rasp. ──
        const float in1 = x - 0.08f*q2fb;
        float A = -bjtStage(in1, 46.0f, -0.020f, 1.25f, 1.05f, 0.95f, 0.80f);
        A = dcA.process(A);

        // C2 interstage high-pass (thins into the nasal voice)
        const float aHP = c2HP.process(A);

        // ── DEPTH (grounded-wiper divider between C2 and C3): it sets how hard
        //    Q2 is DRIVEN, not an output blend. Low DEPTH = Q2 barely driven
        //    (mild, thin fuzz); high DEPTH = Q2 slammed (full buzz). This is the
        //    real topology — the old output-crossfade model had a near-total
        //    cancellation null right around DEPTH ~0.56 (the reported "choppy"
        //    broken sound at the default). ──
        const float q2in = c3HP.process(aHP * (0.06f + 0.94f*depth));
        float B = -bjtStage(q2in, 40.0f, 0.016f, 1.15f, 1.02f, 0.92f, 0.78f);
        B = dcB.process(B);
        q2fb = c4FB.process(B);                       // C4 feedback into Q1 (next sample)

        // ── Output node = Q2's collector. Q1's (out-of-phase) signal arrives
        //    there through C4 at a FIXED ratio set by the impedances — the
        //    partial cancellation = the FuzzRite scoop/spit, but Q2 dominates
        //    whenever it's driven, so there is never a null. ──
        float out = B + 0.32f*aHP;

        // Level compensation: Q2 contributes less at low DEPTH, so tilt the
        // makeup so the DEPTH sweep stays within a few dB (default lands ~-16
        // dBFS RMS, in family with the other pedals).
        out *= 0.70f - 0.24f*depth;

        out = outLP.process(out);
        out = outHP.process(out);
        return dn(out);
    }

    float outLevel() const { return volume; }
};

} // namespace fuzzrite
#endif // FUZZ_RITE_CORE_H
