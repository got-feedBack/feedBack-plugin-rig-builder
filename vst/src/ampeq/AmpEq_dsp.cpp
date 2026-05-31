/*
 * Amp EQ — Fender '59 Bassman 5F6-A passive FMV tone stack, DPF VST3.
 *
 * The Bass/Mid/Treble controls are NOT independent bands. This models the real
 * passive RC network: the controls interact, and at noon the stack has the
 * characteristic Fender mid scoop (~ -12 dB near 800 Hz). The closed-form
 * analog transfer function of the circuit (Yeh & Smith, "Discretization of the
 * '59 Fender Bassman Tone Stack", DAFx-06) is computed from the component
 * values + pot positions, then discretized with the bilinear transform into a
 * 3rd-order digital filter that is recomputed whenever a control moves.
 *
 * The BassFreq/TrebleFreq/MidShift knobs scale C2/C1/C3 (the tone-stack caps),
 * shifting the corner frequencies — i.e. swapping a cap value on the real board.
 */
#include "DistrhoPlugin.hpp"
#include "AmpEqParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

// 3rd-order tone stack: analog H(s) (Yeh) -> bilinear -> digital H(z).
class ToneStack {
    float B0, B1, B2, B3, A1, A2, A3;       // normalized digital coeffs (A0 = 1)
    float x1, x2, x3, y1, y2, y3;
    float fs;
public:
    ToneStack() { fs = 48000.f; flat(); reset(); }
    void reset() { x1 = x2 = x3 = y1 = y2 = y3 = 0.f; }
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; }
    void flat() { B0 = 1.f; B1 = B2 = B3 = A1 = A2 = A3 = 0.f; }

    // t = treble, m = mid, l = bass pot positions (0..1); cN = cap multipliers.
    void update(float t, float m, float l, float c1m, float c2m, float c3m) {
        const float R1 = AEQ_R1, R2 = AEQ_R2, R3 = AEQ_R3, R4 = AEQ_R4;
        const float C1 = AEQ_C1 * c1m, C2 = AEQ_C2 * c2m, C3 = AEQ_C3 * c3m;

        // --- continuous-time coefficients (Yeh & Smith DAFx-06) ---
        const float b1 = t*C1*R1 + m*C3*R3 + l*(C1*R2 + C2*R2) + (C1*R3 + C2*R3);
        const float b2 = t*(C1*C2*R1*R4 + C1*C3*R1*R4)
                       - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                       + m*(C1*C3*R1*R3 + C1*C3*R3*R3 + C2*C3*R3*R3)
                       + l*(C1*C2*R1*R2 + C1*C2*R2*R4 + C1*C3*R2*R4)
                       + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                       + (C1*C2*R1*R3 + C1*C2*R3*R4 + C1*C3*R3*R4);
        const float b3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                       - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                       + m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                       + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4 + t*l*C1*C2*C3*R1*R2*R4;
        const float a0 = 1.0f;
        const float a1 = (C1*R1 + C1*R3 + C2*R3 + C2*R4 + C3*R4)
                       + m*C3*R3 + l*(C1*R2 + C2*R2);
        const float a2 = m*(C1*C3*R1*R3 - C2*C3*R3*R4 + C1*C3*R3*R3 + C2*C3*R3*R3)
                       - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                       + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                       + l*(C1*C2*R2*R4 + C1*C2*R1*R2 + C1*C3*R2*R4 + C2*C3*R2*R4)
                       + (C1*C2*R1*R4 + C1*C3*R1*R4 + C1*C2*R3*R4 + C1*C2*R1*R3 + C1*C3*R3*R4 + C2*C3*R3*R4);
        const float a3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                       - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                       + m*(C1*C2*C3*R3*R3*R4 + C1*C2*C3*R1*R3*R3 - C1*C2*C3*R1*R3*R4)
                       + l*(C1*C2*C3*R1*R2*R4) + C1*C2*C3*R1*R3*R4;

        // --- bilinear transform (c = 2/T) ---
        const float c  = 2.0f * fs;
        const float c2 = c * c, c3 = c2 * c;
        const float B0n = -b1*c - b2*c2 - b3*c3;
        const float B1n = -b1*c + b2*c2 + 3.f*b3*c3;
        const float B2n =  b1*c + b2*c2 - 3.f*b3*c3;
        const float B3n =  b1*c - b2*c2 + b3*c3;
        const float A0  = -a0 - a1*c - a2*c2 - a3*c3;
        const float A1n = -3.f*a0 - a1*c + a2*c2 + 3.f*a3*c3;
        const float A2n = -3.f*a0 + a1*c + a2*c2 - 3.f*a3*c3;
        const float A3n = -a0 + a1*c - a2*c2 + a3*c3;

        if (std::fabs(A0) < 1e-30f) { flat(); return; }
        const float inv = 1.0f / A0;
        B0 = B0n*inv; B1 = B1n*inv; B2 = B2n*inv; B3 = B3n*inv;
        A1 = A1n*inv; A2 = A2n*inv; A3 = A3n*inv;
    }

    inline float process(float x) {
        const float y = B0*x + B1*x1 + B2*x2 + B3*x3 - A1*y1 - A2*y2 - A3*y3;
        x3 = x2; x2 = x1; x1 = x;
        y3 = y2; y2 = y1; y1 = y;
        return y;
    }
};

class AmpEqPlugin : public Plugin {
    ToneStack L, R;
    float fParams[aNumParams];

    void recalc() {
        const float t = aeqPot(fParams[aTreble]);
        const float m = aeqPot(fParams[aMid]);
        const float l = aeqPot(fParams[aBass]);
        const float c1 = aeqCapMul(fParams[aTrebleFreq]);
        const float c2 = aeqCapMul(fParams[aBassFreq]);
        const float c3 = aeqCapMul(fParams[aMidShift]);
        L.update(t, m, l, c1, c2, c3);
        R.update(t, m, l, c1, c2, c3);
    }
public:
    AmpEqPlugin() : Plugin(aNumParams, 0, 0) {
        for (int i = 0; i < aNumParams; ++i) fParams[i] = 0.5f;
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr);
        recalc();
    }
protected:
    const char* getLabel()       const override { return "AmpEQ"; }
    const char* getDescription() const override { return "Fender Bassman FMV passive tone stack (Bass/Mid/Treble)"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'A', 'E', 'Q'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)aNumParams) return;
        p.hints = kParameterIsAutomatable;
        p.name = kAmpNames[i]; p.symbol = kAmpNames[i];
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 0.5f;
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)aNumParams) ? fParams[i] : 0.5f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)aNumParams) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) {
            oL[i] = L.process(iL[i]) * AEQ_MAKEUP;
            oR[i] = R.process(iR[i]) * AEQ_MAKEUP;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpEqPlugin)
};

Plugin* createPlugin() { return new AmpEqPlugin(); }

END_NAMESPACE_DISTRHO
