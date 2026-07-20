#ifndef BIG_BUZZ_CORE_H
#define BIG_BUZZ_CORE_H

#include <cmath>
#include "../../_shared/semiconductors.hpp"

namespace bigbuzz {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float dn(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

class RcHighPass
{
    float a = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void setRC(float sr, float rOhm, float cFarad)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        const float rc = rOhm * cFarad;
        const float dt = 1.0f / s;
        a = rc / (rc + dt);
    }

    void reset() { x1 = y1 = 0.0f; }

    inline float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = dn(y);
        return y1;
    }
};

class RcLowPass
{
    float a = 0.0f;
    float y = 0.0f;

public:
    void setRC(float sr, float rOhm, float cFarad)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        const float rc = rOhm * cFarad;
        a = 1.0f - std::exp(-1.0f / (rc * s));
    }

    void setHz(float sr, float hz)
    {
        const float s = sr > 1000.0f ? sr : 48000.0f;
        a = 1.0f - std::exp(-2.0f * kPi * hz / s);
    }

    void reset() { y = 0.0f; }

    inline float process(float x)
    {
        y += a * (x - y);
        y = dn(y);
        return y;
    }
};

// Nodal model of one V1 Big Muff clipping cell. Voltages are solved around the
// DC operating point of the 2N5133. The 1N914 pair is AC-coupled by C6/C7 and
// sits between collector and base, in parallel with R17/R15 and C12/C11.
class MuffFeedbackClipStage
{
    float sampleRate = 192000.0f;
    float inputR = 10000.0f;
    float feedbackR = 470000.0f;
    float baseR = 100000.0f;
    float collectorR = 10000.0f;
    float emitterR = 120.0f;
    float feedbackC = 500.0e-12f;
    float diodeCouplingC = 1.0e-6f;
    float supplyV = 9.0f;

    // 2N5133 datasheet: hFE typ 220 @ 1 mA. Is is a conventional silicon
    // small-signal value; the solved bias and circuit resistors set the stage.
    float beta = 220.0f;
    float transistorIs = 1.0e-14f;
    float thermalV = 0.02585f;
    rbcomponents::DiodeSpec diode = rbcomponents::diode1N914();

    float idleB = 0.0f, idleE = 0.0f, idleC = 0.0f;
    float idleIc = 0.0f, idleIb = 0.0f;
    float b = 0.0f, e = 0.0f, c = 0.0f, d = 0.0f;
    float previousCb = 0.0f;
    float previousCd = 0.0f;

    static void solveLinear(float a[4][5], int count)
    {
        for (int col = 0; col < count; ++col) {
            int pivot = col;
            for (int row = col + 1; row < count; ++row)
                if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
            if (pivot != col)
                for (int k = col; k <= count; ++k) {
                    const float t = a[col][k]; a[col][k] = a[pivot][k]; a[pivot][k] = t;
                }
            const float divisor = std::fabs(a[col][col]) > 1.0e-20f ? a[col][col] : 1.0e-20f;
            for (int k = col; k <= count; ++k) a[col][k] /= divisor;
            for (int row = 0; row < count; ++row) {
                if (row == col) continue;
                const float factor = a[row][col];
                for (int k = col; k <= count; ++k) a[row][k] -= factor * a[col][k];
            }
        }
    }

    void transistor(float vbe, float& ic, float& ib, float& gm, float& gb) const
    {
        const float exponent = std::fmax(-40.0f, std::fmin(40.0f, vbe / thermalV));
        const float ev = std::exp(exponent);
        ic = transistorIs * (ev - 1.0f);
        gm = transistorIs * ev / thermalV;
        ib = ic / beta;
        gb = gm / beta;
    }

    void diodePair(float voltage, float& current, float& conductance) const
    {
        const float vt = diode.ideality * thermalV;
        const float exponent = std::fmax(-40.0f, std::fmin(40.0f, voltage / vt));
        const float ep = std::exp(exponent);
        const float en = 1.0f / ep;
        current = diode.isAmp * (ep - en);
        conductance = diode.isAmp * (ep + en) / vt;
    }

    void solveIdle()
    {
        idleB = 0.62f;
        idleE = 0.08f;
        idleC = collectorR > 20000.0f ? 4.5f : 7.0f;
        for (int iteration = 0; iteration < 12; ++iteration) {
            float ic, ib, gm, gb;
            transistor(idleB - idleE, ic, ib, gm, gb);
            const float fB = idleB / baseR + ib + (idleB - idleC) / feedbackR;
            const float fE = idleE / emitterR - ic - ib;
            const float fC = (idleC - supplyV) / collectorR + ic
                           + (idleC - idleB) / feedbackR;
            float a[4][5] = {};
            a[0][0] = 1.0f / baseR + gb + 1.0f / feedbackR;
            a[0][1] = -gb;
            a[0][2] = -1.0f / feedbackR;
            a[0][3] = -fB;
            a[1][0] = -(gm + gb);
            a[1][1] = 1.0f / emitterR + gm + gb;
            a[1][3] = -fE;
            a[2][0] = gm - 1.0f / feedbackR;
            a[2][1] = -gm;
            a[2][2] = 1.0f / collectorR + 1.0f / feedbackR;
            a[2][3] = -fC;
            solveLinear(a, 3);
            idleB += a[0][3];
            idleE += a[1][3];
            idleC += a[2][3];
        }
        float gm, gb;
        transistor(idleB - idleE, idleIc, idleIb, gm, gb);
    }

public:
    void set(float sr, float rin, float rf, float rb, float rc, float re,
             float cf, float couplingC)
    {
        sampleRate = sr > 1000.0f ? sr : 192000.0f;
        inputR = rin;
        feedbackR = rf;
        baseR = rb;
        collectorR = rc;
        emitterR = re;
        feedbackC = cf;
        diodeCouplingC = couplingC;
        reset();
    }

    void reset()
    {
        solveIdle();
        b = e = c = d = 0.0f;
        previousCb = previousCd = 0.0f;
    }

    float process(float inputVoltage)
    {
        const float gFeedbackC = feedbackC * sampleRate;
        const float gDiodeC = diodeCouplingC * sampleRate;

        for (int iteration = 0; iteration < 7; ++iteration) {
            float ic, ib, gm, gb;
            transistor((idleB + b) - (idleE + e), ic, ib, gm, gb);
            const float deltaIc = ic - idleIc;
            const float deltaIb = ib - idleIb;
            float diodeCurrent, diodeG;
            diodePair(d - b, diodeCurrent, diodeG);

            const float fB = b / baseR + (b - inputVoltage) / inputR
                           + (b - c) / feedbackR + deltaIb
                           + gFeedbackC * (b - c + previousCb)
                           - diodeCurrent;
            const float fE = e / emitterR - deltaIc - deltaIb;
            const float fC = c / collectorR + (c - b) / feedbackR + deltaIc
                           + gFeedbackC * (c - b - previousCb)
                           + gDiodeC * (c - d - previousCd);
            const float fD = diodeCurrent + gDiodeC * (d - c + previousCd);

            float a[4][5] = {};
            a[0][0] = 1.0f / baseR + 1.0f / inputR + 1.0f / feedbackR
                    + gb + gFeedbackC + diodeG;
            a[0][1] = -gb;
            a[0][2] = -1.0f / feedbackR - gFeedbackC;
            a[0][3] = -diodeG;
            a[0][4] = -fB;

            a[1][0] = -(gm + gb);
            a[1][1] = 1.0f / emitterR + gm + gb;
            a[1][4] = -fE;

            a[2][0] = gm - 1.0f / feedbackR - gFeedbackC;
            a[2][1] = -gm;
            a[2][2] = 1.0f / collectorR + 1.0f / feedbackR
                    + gFeedbackC + gDiodeC;
            a[2][3] = -gDiodeC;
            a[2][4] = -fC;

            a[3][0] = -diodeG;
            a[3][2] = -gDiodeC;
            a[3][3] = diodeG + gDiodeC;
            a[3][4] = -fD;
            solveLinear(a, 4);

            b += a[0][4];
            e += a[1][4];
            c += a[2][4];
            d += a[3][4];
            if (std::fabs(a[0][4]) + std::fabs(a[1][4])
              + std::fabs(a[2][4]) + std::fabs(a[3][4]) < 1.0e-7f)
                break;
        }

        previousCb = c - b;
        previousCd = c - d;
        return dn(c);
    }
};

// Nodal 2N5133 common-emitter stage used for Q4 (collector-to-base bias and
// feedback) and Q1 (fixed base divider). Coupling capacitors remain in the
// surrounding network, so the signal source is AC-coupled and does not alter
// the solved DC operating point.
class MuffBjtStage
{
    float sampleRate = 192000.0f;
    float inputR = 36000.0f;
    float baseGroundR = 100000.0f;
    float baseSupplyR = 0.0f;
    float feedbackR = 470000.0f;
    float collectorR = 39000.0f;
    float emitterR = 120.0f;
    float feedbackC = 500.0e-12f;
    float supplyV = 9.0f;

    float beta = 220.0f;
    float transistorIs = 1.0e-14f;
    float thermalV = 0.02585f;

    float idleB = 0.0f, idleE = 0.0f, idleC = 0.0f;
    float idleIc = 0.0f, idleIb = 0.0f;
    float b = 0.0f, e = 0.0f, c = 0.0f;
    float previousBC = 0.0f;

    static void solveLinear(float a[3][4])
    {
        for (int col = 0; col < 3; ++col) {
            int pivot = col;
            for (int row = col + 1; row < 3; ++row)
                if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
            if (pivot != col)
                for (int k = col; k < 4; ++k) {
                    const float value = a[col][k];
                    a[col][k] = a[pivot][k];
                    a[pivot][k] = value;
                }
            const float divisor = std::fabs(a[col][col]) > 1.0e-20f
                                ? a[col][col] : 1.0e-20f;
            for (int k = col; k < 4; ++k) a[col][k] /= divisor;
            for (int row = 0; row < 3; ++row) {
                if (row == col) continue;
                const float factor = a[row][col];
                for (int k = col; k < 4; ++k) a[row][k] -= factor * a[col][k];
            }
        }
    }

    void transistor(float vbe, float& ic, float& ib, float& gm, float& gb) const
    {
        const float exponent = std::fmax(-40.0f, std::fmin(40.0f, vbe / thermalV));
        const float ev = std::exp(exponent);
        ic = transistorIs * (ev - 1.0f);
        gm = transistorIs * ev / thermalV;
        ib = ic / beta;
        gb = gm / beta;
    }

    void solveIdle()
    {
        idleB = baseSupplyR > 0.0f
              ? supplyV * baseGroundR / (baseGroundR + baseSupplyR)
              : 0.62f;
        idleE = std::fmax(0.02f, idleB - 0.58f);
        idleC = 4.5f;

        const float gBg = 1.0f / baseGroundR;
        const float gBt = baseSupplyR > 0.0f ? 1.0f / baseSupplyR : 0.0f;
        const float gFb = feedbackR > 0.0f ? 1.0f / feedbackR : 0.0f;
        for (int iteration = 0; iteration < 12; ++iteration) {
            float ic, ib, gm, gb;
            transistor(idleB - idleE, ic, ib, gm, gb);
            const float fB = idleB * gBg + (idleB - supplyV) * gBt
                           + ib + (idleB - idleC) * gFb;
            const float fE = idleE / emitterR - ic - ib;
            const float fC = (idleC - supplyV) / collectorR + ic
                           + (idleC - idleB) * gFb;
            float a[3][4] = {};
            a[0][0] = gBg + gBt + gb + gFb;
            a[0][1] = -gb;
            a[0][2] = -gFb;
            a[0][3] = -fB;
            a[1][0] = -(gm + gb);
            a[1][1] = 1.0f / emitterR + gm + gb;
            a[1][3] = -fE;
            a[2][0] = gm - gFb;
            a[2][1] = -gm;
            a[2][2] = 1.0f / collectorR + gFb;
            a[2][3] = -fC;
            solveLinear(a);
            idleB += a[0][3];
            idleE += a[1][3];
            idleC += a[2][3];
        }
        float gm, gb;
        transistor(idleB - idleE, idleIc, idleIb, gm, gb);
    }

public:
    void set(float sr, float rin, float rBaseGround, float rBaseSupply,
             float rFeedback, float rc, float re, float cFeedback)
    {
        sampleRate = sr > 1000.0f ? sr : 192000.0f;
        inputR = rin;
        baseGroundR = rBaseGround;
        baseSupplyR = rBaseSupply;
        feedbackR = rFeedback;
        collectorR = rc;
        emitterR = re;
        feedbackC = cFeedback;
        reset();
    }

    void reset()
    {
        solveIdle();
        b = e = c = 0.0f;
        previousBC = 0.0f;
    }

    float process(float inputVoltage)
    {
        const float gBg = 1.0f / baseGroundR;
        const float gBt = baseSupplyR > 0.0f ? 1.0f / baseSupplyR : 0.0f;
        const float gFb = feedbackR > 0.0f ? 1.0f / feedbackR : 0.0f;
        const float gCf = feedbackC * sampleRate;

        for (int iteration = 0; iteration < 6; ++iteration) {
            float ic, ib, gm, gb;
            transistor((idleB + b) - (idleE + e), ic, ib, gm, gb);
            const float deltaIc = ic - idleIc;
            const float deltaIb = ib - idleIb;
            const float fB = b * (gBg + gBt) + (b - inputVoltage) / inputR
                           + deltaIb + (b - c) * gFb
                           + gCf * (b - c - previousBC);
            const float fE = e / emitterR - deltaIc - deltaIb;
            const float fC = c / collectorR + deltaIc + (c - b) * gFb
                           + gCf * (c - b + previousBC);
            float a[3][4] = {};
            a[0][0] = gBg + gBt + 1.0f / inputR + gb + gFb + gCf;
            a[0][1] = -gb;
            a[0][2] = -gFb - gCf;
            a[0][3] = -fB;
            a[1][0] = -(gm + gb);
            a[1][1] = 1.0f / emitterR + gm + gb;
            a[1][3] = -fE;
            a[2][0] = gm - gFb - gCf;
            a[2][1] = -gm;
            a[2][2] = 1.0f / collectorR + gFb + gCf;
            a[2][3] = -fC;
            solveLinear(a);
            b += a[0][3];
            e += a[1][3];
            c += a[2][3];
            if (std::fabs(a[0][3]) + std::fabs(a[1][3])
              + std::fabs(a[2][3]) < 1.0e-7f)
                break;
        }

        previousBC = b - c;
        return dn(c);
    }
};

class BigBuzzCore
{
    // Version 1 Big Muff / triangle-era schematic from pedals/buzz 2.jpg:
    // Q1-Q4 = 2N5133, D1-D4 = 1N914, Sustain/Tone/Volume = 100 k linear.
    static constexpr float kC1 = 1.0e-6f;
    static constexpr float kR2 = 36000.0f;
    static constexpr float kR14 = 100000.0f;
    static constexpr float kR13 = 39000.0f;
    static constexpr float kR9 = 470000.0f;
    static constexpr float kC10 = 500.0e-12f;
    static constexpr float kC4 = 0.68e-6f;
    static constexpr float kSustainPot = 100000.0f;
    static constexpr float kC5 = 0.68e-6f;
    static constexpr float kR19 = 10000.0f;
    static constexpr float kR20 = 100000.0f;
    static constexpr float kR17 = 470000.0f;
    static constexpr float kC12 = 500.0e-12f;
    static constexpr float kC13 = 1.0e-6f;
    static constexpr float kR12 = 10000.0f;
    static constexpr float kR16 = 100000.0f;
    static constexpr float kR15 = 470000.0f;
    static constexpr float kC11 = 500.0e-12f;
    static constexpr float kC9 = 0.004e-6f;
    static constexpr float kC8 = 0.01e-6f;
    static constexpr float kR5 = 27000.0f;
    static constexpr float kR8 = 27000.0f;
    static constexpr float kTonePot = 100000.0f;
    static constexpr float kC3 = 0.68e-6f;
    static constexpr float kR7 = 470000.0f;
    static constexpr float kR3 = 100000.0f;
    static constexpr float kC2 = 0.68e-6f;
    static constexpr float kVolumePot = 100000.0f;

    float sampleRate = 48000.0f;
    float sustain = 0.64f;
    float tone = 0.46f;

    RcHighPass inputC1;
    RcHighPass q4ToSustain;
    RcHighPass sustainToQ3;
    RcHighPass q3ToQ2;
    RcHighPass toneToQ1;
    RcHighPass outputC2;
    RcLowPass toneLow;
    RcLowPass toneHighBase;
    RcLowPass q1Load;
    MuffBjtStage inputStage;
    MuffFeedbackClipStage clip1;
    MuffFeedbackClipStage clip2;
    MuffBjtStage recoveryStage;

    static inline float parallel(float a, float b)
    {
        return (a * b) / (a + b);
    }

    void updateComponentValues()
    {
        const float s = clamp01(sustain);
        const float t = clamp01(tone);

        inputC1.setRC(sampleRate, kR2 + kR14, kC1);
        q4ToSustain.setRC(sampleRate, kSustainPot, kC4);

        // The Sustain pot is a 100 k series/shunt network before C5/R19.
        const float sustainSource = 1000.0f + (1.0f - s) * 99000.0f;
        sustainToQ3.setRC(sampleRate, kR19 + sustainSource + kR20, kC5);
        q3ToQ2.setRC(sampleRate, kR12 + kR16, kC13);
        toneToQ1.setRC(sampleRate, parallel(kR7, kR3), kC3);
        outputC2.setRC(sampleRate, kVolumePot, kC2);

        // Reference-calibrated branch approximation of the passive network.
        // It retains the schematic RC values while avoiding the false branch
        // leakage produced by a fixed Q1 load at the two pot endpoints.
        toneLow.setRC(sampleRate, kR8, kC8);
        toneHighBase.setRC(sampleRate, kR5, kC9);
        q1Load.setHz(sampleRate, 7200.0f + 500.0f * t);

    }

    void configureTransistorStages()
    {
        // Q4: R2 input, R14 base shunt, R9/C10 collector feedback,
        // R13 collector load and R22 emitter resistor.
        inputStage.set(sampleRate, kR2, kR14, 0.0f, kR9,
                       kR13, 120.0f, kC10);
        // Q3 and Q2 clipping cells.
        clip1.set(sampleRate, kR19, kR17, kR20, 10000.0f,
                  120.0f, kC12, 1.0e-6f);
        clip2.set(sampleRate, kR12, kR15, kR16, 39000.0f,
                  120.0f, kC11, 1.0e-6f);
        // Q1: the tone-network solver already includes its loaded Thevenin
        // output, so no second synthetic source resistance is inserted here.
        recoveryStage.set(sampleRate, 10.0f, kR3, kR7, 0.0f,
                          10000.0f, 2200.0f, 0.0f);
    }

    float toneStack(float x)
    {
        const float t = clamp01(tone);
        const float low = toneLow.process(x);
        const float high = x - toneHighBase.process(x);
        const float centre = 4.0f * t * (1.0f - t);
        const float passiveNotch = 1.0f - 0.20f * centre;
        const float loadedHighBranch = 1.10f + 0.90f * centre;
        // The physical network has substantial insertion loss before Q1.
        return 0.30f * passiveNotch
             * ((1.0f - t) * low + loadedHighBranch * t * high);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void reset()
    {
        inputC1.reset();
        q4ToSustain.reset();
        sustainToQ3.reset();
        q3ToQ2.reset();
        toneToQ1.reset();
        outputC2.reset();
        toneLow.reset();
        toneHighBase.reset();
        q1Load.reset();
        updateComponentValues();
        configureTransistorStages();
    }

    void setSustain(float v)
    {
        sustain = clamp01(v);
        updateComponentValues();
    }

    void setTone(float v)
    {
        tone = clamp01(v);
        updateComponentValues();
    }

    float process(float in)
    {
        const float s = clamp01(sustain);
        float x = inputC1.process(0.965f * in);

        // Q4 input booster: 2N5133 with R13/R14/R22 and 470 k + 500 pF feedback.
        x = inputStage.process(x);
        x = q4ToSustain.process(x);

        // Real Sustain pot: mostly drive into Q3, not output volume.
        const float potLoss = 0.003f + 0.997f * s;
        x = sustainToQ3.process(x * potLoss);

        float y = clip1.process(x);
        y = q3ToQ2.process(y);

        y = clip2.process(y);

        y = toneStack(y);
        y = toneToQ1.process(y);

        // Q1 recovery/output transistor into the 100 k Volume pot.
        y = recoveryStage.process(y);
        y = q1Load.process(y);
        y = outputC2.process(y);

        // Q1 and both diode-feedback stages already provide the circuit's
        // limiting. A second broadband tanh here made the bright setting flat
        // and brittle, and the old inverse-Sustain trim made the pedal quieter
        // as its real Sustain control was raised.
        return y * 0.12f;
    }
};

} // namespace bigbuzz

#endif // BIG_BUZZ_CORE_H
