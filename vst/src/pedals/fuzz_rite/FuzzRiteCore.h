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
    float env  = 0.f, envA = 0.f, envR = 0.f;

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
        envA = 1.f - std::exp(-1.f/(0.0020f*sampleRate));   // 2 ms
        envR = 1.f - std::exp(-1.f/(0.060f*sampleRate));    // 60 ms
    }

public:
    void setSampleRate(float sr){ sampleRate = sr>1000.f?sr:48000.f; reset(); }
    void reset(){
        inHP.reset(); c2HP.reset(); c3HP.reset(); c4FB.reset(); outLP.reset(); outHP.reset();
        dcA.reset(); dcB.reset(); q2fb=0.f; env=0.f; updateComponents();
    }
    void setDepth(float v){ depth = clamp01(v); }
    void setVolume(float v){ volume = clamp01(v); }

    inline float process(float in){
        // envelope (for the decay gate / sputter)
        const float lvl = std::fabs(in);
        env += (lvl>env ? envA : envR) * (lvl - env);

        float x = inHP.process(in);

        // ── Q1: grounded-emitter Si stage, HIGH gain, inverting. C4 feedback
        //    from Q2 (scaled by DEPTH) injected at the input node = the gated,
        //    sputtery instability. ──
        const float in1 = x - 0.15f*depth*q2fb;
        float A = -bjtStage(in1, 46.0f, -0.020f, 1.25f, 1.05f, 0.95f, 0.80f);
        A = dcA.process(A);

        // C2 interstage high-pass (thins into the nasal voice)
        const float aHP = c2HP.process(A);

        // ── Q2: second grounded-emitter Si stage, inverting again -> its output
        //    is OUT OF PHASE with Q1's. ──
        const float q2in = c3HP.process(aHP);
        float B = -bjtStage(q2in, 40.0f, 0.016f, 1.15f, 1.02f, 0.92f, 0.78f);
        B = dcB.process(B);
        q2fb = c4FB.process(B);                       // store C4 feedback for next sample

        // ── DEPTH crossfade between Q1's out (aHP) and Q2's out (B, opposite
        //    phase). The partial cancellation in the middle is the FuzzRite honk. ──
        float out = aHP*(1.0f - 0.88f*depth) + B*(0.95f*depth);

        // Level compensation: the cancellation robs level as DEPTH climbs, so a
        // gentle depth-tilt keeps the min/max-DEPTH loudness within a few dB
        // (you still rebalance with VOL, but it isn't a 7 dB jump).
        out *= 0.76f + 0.40f*depth;

        // ── decay gate: the fuzz spits and dies as the note fades; stronger with
        //    DEPTH up (the C4 loop starving). Soft knee so it sputters musically
        //    instead of hard-dropping. ──
        const float g = env/(env + 0.0042f);           // soft knee ~ -47 dBFS
        out *= 1.0f - depth*0.34f*(1.0f - g);

        out = outLP.process(out);
        out = outHP.process(out);
        return dn(out);
    }

    float outLevel() const { return volume; }
};

} // namespace fuzzrite
#endif // FUZZ_RITE_CORE_H
