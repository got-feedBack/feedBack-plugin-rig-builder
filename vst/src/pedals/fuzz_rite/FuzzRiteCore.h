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
// Runs at the oversampled rate (4x, set by the plugin). Transistor clipping
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
        // C2 is only 2.2 nF and sees the position-dependent 500 k track in
        // parallel with the following 22 k/base network. The complete reference
        // sweep places the effective first interstage corner near 420 Hz.
        c2HP.setHz(sampleRate, 420.0f);
        c3HP.setHz(sampleRate, 480.0f);
        // C4 is 47 nF between the collector networks. The effective loaded
        // impedance places this feedback branch near 720 Hz in the calibrated
        // discrete-time model; extending it to 180 Hz cancels sustained notes.
        c4FB.setHz(sampleRate, 720.0f);
        // BC337 collector capacitance and the 470k collector networks provide
        // the only HF rounding in the real pedal. Keep this above the guitar
        // band instead of adding the old arbitrary 6.2 kHz de-fizz filter.
        outLP.setHz(sampleRate, 20000.0f);
    }

public:
    void setSampleRate(float sr){ sampleRate = sr>1000.f?sr:48000.f; reset(); }
    void reset(){
        inHP.reset(); c2HP.reset(); c3HP.reset(); c4FB.reset(); outLP.reset();
        dcA.reset(); dcB.reset();
        q2fb=0.f; updateComponents();
    }
    void setDepth(float v){ depth = clamp01(v); }
    void setVolume(float v){ volume = clamp01(v); }

    inline float process(float in){
        float x = inHP.process(in);

        // Q1 and Q2 form an AC collector-feedback loop through C4. Keep the
        // collector feedback below the cancellation point. A stronger
        // coefficient suppresses sustained-note windows even though the DSP
        // remains finite, which sounds like hard audio dropouts.
        const float in1 = x - 0.018f*q2fb;
        // The 500 kB track and 500 kA Volume load alter the collector load as
        // the wiper approaches pin 3. Keep that secondary effect modest; Depth
        // is still an output tap, not a synthetic gain control.
        const float loaded = std::sqrt(depth);
        const float q1Drive = 66.0f + 16.0f * loaded * loaded;
        float A = -bjtStage(in1, q1Drive, -0.020f, 2.35f, 2.00f, 0.95f, 0.80f);
        A = dcA.process(A);

        // C2 interstage high-pass (thins into the nasal voice)
        const float aHP = c2HP.process(A);

        // The far end of DEPTH feeds Q2 through C3 and R8=22k. Q2 drive is
        // fixed by that network; moving the wiper does not alter it.
        const float q2DriveHp = c3HP.process(aHP);
        const float q2in = 0.050f*q2DriveHp;
        float B = -bjtStage(q2in, 24.0f, 0.016f, 1.15f, 1.02f, 0.92f, 0.78f);
        B = dcB.process(B);
        q2fb = c4FB.process(B);                       // C4 feedback into Q1 (next sample)

        // Pin 3 of DEPTH is BEFORE C3. At low frequency C3 is open and this
        // loaded node follows aHP; at high frequency C3/R8 shunts it through
        // 500 k, leaving roughly R8/(500k+R8) = 0.042 of the signal. The wiper
        // reads between that node and the un-loaded C2 side.
        // Solving the ideal 500 k / 22 k divider in isolation produced a 0.042
        // endpoint and an unrealistically dark, chopped output. The collector,
        // C2, C3, R8 and 500 kA Volume load form a finite source/load network;
        // the reference sweep gives about 4-5 dB endpoint loss, not 24 dB.
        const float loadedNode = 0.60f*aHP - 0.12f*q2DriveHp;

        // Pin 1 is the direct C2 side and pin 3 is the loaded Q2-input side.
        // The supplied Depth sweep confirms the panel direction: clockwise moves
        // from the louder direct endpoint toward the thinner loaded endpoint.
        // The dominant behavior remains this passive tap; the smaller collector
        // loading correction is applied above.
        float out = (1.0f-loaded)*aHP + loaded*loadedNode;

        out = outLP.process(out);

        // Static correction for the position-dependent source impedance seen by
        // the 500 kA Volume pot. Fitted to the complete min/noon/max renders;
        // unlike envelope makeup, it cannot pump or alter note decays.
        const float loadCalibration = 1.188f - 0.565f*depth + 0.678f*depth*depth;
        out *= loadCalibration;
        return dn(out);
    }

    float outLevel() const { return volume; }
};

} // namespace fuzzrite
#endif // FUZZ_RITE_CORE_H
