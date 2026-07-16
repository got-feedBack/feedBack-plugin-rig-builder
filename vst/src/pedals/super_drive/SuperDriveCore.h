#ifndef SUPER_DRIVE_CORE_H
#define SUPER_DRIVE_CORE_H

/* Boss SD-1 core from pedals/super drive.pdf. Runs at wrapper 4x rate. */
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

namespace superdrive {

static constexpr float kPi = 3.14159265358979323846f;
static inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

class RcHighPass
{
    float a = 0.f, x1 = 0.f, y1 = 0.f;
public:
    void setRC(float sr, float r, float c)
    {
        const float dt = 1.f / sr;
        const float rc = r * c;
        a = rc / (rc + dt);
    }
    void reset() { x1 = y1 = 0.f; }
    float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = std::fabs(y) < 1.e-15f ? 0.f : y;
        return y1;
    }
};

class RcLowPass
{
    float a = 0.f, y = 0.f;
public:
    void setHz(float sr, float hz)
    {
        a = 1.f - std::exp(-2.f * kPi * hz / sr);
    }
    void reset() { y = 0.f; }
    float process(float x)
    {
        y += a * (x - y);
        if (std::fabs(y) < 1.e-15f)
            y = 0.f;
        return y;
    }
};

class ControlSmoother
{
    float current = 0.f, target = 0.f, coefficient = 1.f;
public:
    void setSampleRate(float sr)
    {
        coefficient = 1.f - std::exp(-1.f / (0.020f * sr));
    }
    void reset(float value) { current = target = value; }
    void setTarget(float value) { target = value; }
    float next()
    {
        current += coefficient * (target - current);
        if (std::fabs(target - current) < 1.e-6f)
            current = target;
        return current;
    }
};

class AnalogIir1
{
    double b0 = 1.0, b1 = 0.0, a1 = 0.0;
    double x1 = 0.0, y1 = 0.0;
public:
    void setAnalog(float sr, double n1, double n0, double d1, double d0)
    {
        const double k = 2.0 * static_cast<double>(sr);
        const double denominator = d1 * k + d0;
        b0 = (n1 * k + n0) / denominator;
        b1 = (-n1 * k + n0) / denominator;
        a1 = (-d1 * k + d0) / denominator;
    }
    void reset() { x1 = y1 = 0.0; }
    float process(float input)
    {
        const double x = static_cast<double>(input);
        double y = b0*x + b1*x1 - a1*y1;
        x1 = x;
        y1 = y;
        if (!std::isfinite(y))
        {
            reset();
            y = 0.0;
        }
        if (std::fabs(y) < 1.e-15)
            y = 0.0;
        return static_cast<float>(y);
    }
};

// Bilinear transform of an analog third-order transfer function. Coefficients
// are ordered from s^3 to the constant term; lower-order stages pass zeroes for
// the unused leading coefficients.
class AnalogIir3
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, b3 = 0.0;
    double a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double x1 = 0.0, x2 = 0.0, x3 = 0.0;
    double y1 = 0.0, y2 = 0.0, y3 = 0.0;
public:
    void setAnalog(float sr,
                   double n3, double n2, double n1, double n0,
                   double d3, double d2, double d1, double d0)
    {
        const double k = 2.0 * static_cast<double>(sr);
        const double k2 = k * k;
        const double k3 = k2 * k;

        const double nb0 = n3*k3 + n2*k2 + n1*k + n0;
        const double nb1 = -3.0*n3*k3 - n2*k2 + n1*k + 3.0*n0;
        const double nb2 = 3.0*n3*k3 - n2*k2 - n1*k + 3.0*n0;
        const double nb3 = -n3*k3 + n2*k2 - n1*k + n0;
        const double da0 = d3*k3 + d2*k2 + d1*k + d0;
        const double da1 = -3.0*d3*k3 - d2*k2 + d1*k + 3.0*d0;
        const double da2 = 3.0*d3*k3 - d2*k2 - d1*k + 3.0*d0;
        const double da3 = -d3*k3 + d2*k2 - d1*k + d0;
        const double invA0 = 1.0 / da0;

        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        b3 = nb3 * invA0;
        a1 = da1 * invA0;
        a2 = da2 * invA0;
        a3 = da3 * invA0;
    }

    void reset()
    {
        x1 = x2 = x3 = y1 = y2 = y3 = 0.0;
    }

    float process(float input)
    {
        const double x = static_cast<double>(input);
        double y = b0*x + b1*x1 + b2*x2 + b3*x3
                 - a1*y1 - a2*y2 - a3*y3;
        x3 = x2; x2 = x1; x1 = x;
        y3 = y2; y2 = y1; y1 = y;
        if (!std::isfinite(y))
        {
            reset();
            y = 0.0;
        }
        if (std::fabs(y) < 1.e-15)
            y = 0.0;
        return static_cast<float>(y);
    }
};

class SuperDriveCore
{
    float sampleRate = 192000.f;
    float drive = .45f, tone = .5f, level = .62f;
    float lastDrive = -1.f, lastTone = -1.f, lastLevel = -1.f;
    float feedbackR = 33000.f, outputGain = 0.f;

    RcHighPass inputC1, bufferC2, toneOutputC7, finalC8;
    RcLowPass inputBufferPole, outputBufferPole;
    AnalogIir1 gainTransfer;
    AnalogIir3 toneTransfer;
    ControlSmoother driveSmooth, toneSmooth, levelSmooth;
    rbshared::OpAmpStage clipOpamp, toneOpamp;
    rbcomponents::AsymDiodeStringClipper feedbackDiodes;

    static float driveTaper(float value)
    {
        // dm-SD1's measured control law is a useful behavioural anchor for the
        // A1M drive track: retain usable resolution in the lower half.
        const float v = clamp01(value);
        return v * v * v;
    }

    static float wTaper(float value)
    {
        // VR3 is the unusual W20k tone pot. Its two halves follow an S curve,
        // not a linear crossfade.
        const float v = clamp01(value);
        const float iv = 1.f - v;
        return .5f * (1.f - iv*iv*iv*iv) + .5f * v*v*v*v;
    }

    void setGainTransfer(float d)
    {
        // IC1b non-inverting stage: R6=4.7k and C3=47n in the ground leg,
        // R5=33k plus VR1=1M in the feedback path.
        feedbackR = 33000.f + 1000000.f * driveTaper(d);
        feedbackDiodes.setSourceR(feedbackR);
        const double c3 = 47.e-9;
        const double r6 = 4700.0;
        gainTransfer.setAnalog(sampleRate,
                               (r6 + feedbackR) * c3, 1.0,
                               r6 * c3, 1.0);
    }

    void setToneTransfer(float control)
    {
        // Exact small-signal transfer of C4/R7, C5/R8, C6/R9 and IC1a from
        // the SD-1 schematic. VR3 is split into its physical 20k track halves.
        const double t = static_cast<double>(wTaper(control));
        const double rl = (1.0 - t) * 20000.0;
        const double rr = t * 20000.0;
        const double r1 = 10000.0, c1 = 18.e-9;
        const double r2 = 10000.0, c2 = 10.e-9;
        const double r3 = 470.0, c3 = 27.e-9;

        const double n2 = c2*c3*r2*r3*(rr + rl) + c2*c3*r2*rl*rr;
        const double n1 = c3*r3*(rr + rl) + c3*rl*rr
                        + c2*r2*(rr + rl) + c3*r2*rr;
        const double n0 = rr + rl;

        const double d3 = c1*c2*c3*r1*r2*r3*(rr + rl)
                        + c1*c2*c3*r1*r2*rl*rr;
        const double d2 = n2 + c1*c3*r1*r3*(rr + rl)
                        + c1*c3*r1*rl*rr + c1*c2*r1*r2*(rr + rl)
                        + c2*c3*r1*r2*rl;
        const double d1 = c3*r3*(rr + rl) + c3*rl*rr
                        + c2*r2*(rr + rl) + c1*r1*(rr + rl)
                        + c3*r1*rl;
        const double d0 = rr + rl;
        toneTransfer.setAnalog(sampleRate,
                               0.0, n2, n1, n0,
                               d3, d2, d1, d0);
    }

    void updateSmoothedControls()
    {
        const float d = driveSmooth.next();
        const float t = toneSmooth.next();
        const float l = levelSmooth.next();
        if (std::fabs(d - lastDrive) > 1.e-6f)
        {
            setGainTransfer(d);
            lastDrive = d;
        }
        if (std::fabs(t - lastTone) > 1.e-6f)
        {
            setToneTransfer(t);
            lastTone = t;
        }
        if (std::fabs(l - lastLevel) > 1.e-6f)
        {
            // VR2 feeds the Q6 output buffer. Keep its calibrated voltage below
            // Q6's nonlinear region; otherwise LEVEL adds compression that is
            // not present in the reference pedal renders.
            outputGain = .77f * l; // VR2 100kB, reference-calibrated loading into Q6
            lastLevel = l;
        }
    }

    static float q6Follower(float x)
    {
        const float p = 1.34f, n = 1.28f;
        return x >= 0.f ? p * std::tanh(x / p) : -n * std::tanh((-x) / n);
    }

public:
    SuperDriveCore() = default;

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.f ? sr : 192000.f;
        clipOpamp.setSpec(rbshared::upc4558Spec());
        toneOpamp.setSpec(rbshared::upc4558Spec());
        feedbackDiodes.setSpec(rbcomponents::diode1S2473());
        feedbackDiodes.setSeries(1, 2); // D4 versus the D5+D6 string
        clipOpamp.setSampleRate(sampleRate);
        toneOpamp.setSampleRate(sampleRate);
        driveSmooth.setSampleRate(sampleRate);
        toneSmooth.setSampleRate(sampleRate);
        levelSmooth.setSampleRate(sampleRate);

        inputC1.setRC(sampleRate, 480000.f, 47.e-9f); // R1+R2/C1
        bufferC2.setRC(sampleRate, 100000.f, 18.e-9f); // C2/R4, 88.4 Hz
        toneOutputC7.setRC(sampleRate, 100000.f, 150.e-9f);
        finalC8.setRC(sampleRate, 1000000.f, 47.e-9f);
        inputBufferPole.setHz(sampleRate, 18000.f);
        outputBufferPole.setHz(sampleRate, 36000.f);
        reset();
    }

    void reset()
    {
        inputC1.reset();
        bufferC2.reset();
        toneOutputC7.reset();
        finalC8.reset();
        inputBufferPole.reset();
        outputBufferPole.reset();
        gainTransfer.reset();
        toneTransfer.reset();
        clipOpamp.reset();
        toneOpamp.reset();
        feedbackDiodes.reset();
        driveSmooth.reset(drive);
        toneSmooth.reset(tone);
        levelSmooth.reset(level);
        lastDrive = lastTone = lastLevel = -1.f;
        updateSmoothedControls();
    }

    void setParams(float d, float t, float l)
    {
        drive = clamp01(d);
        tone = clamp01(t);
        level = clamp01(l);
        driveSmooth.setTarget(drive);
        toneSmooth.setTarget(tone);
        levelSmooth.setTarget(level);
    }

    float process(float input)
    {
        updateSmoothedControls();

        // Q5 emitter follower and the real 88.4 Hz C2/R4 input high-pass.
        float x = inputC1.process(input);
        x = inputBufferPole.process(.992f * x);
        x = bufferC2.process(x);

        // The diodes clip the voltage developed across R5+VR1 in the feedback
        // loop. The dry non-inverting component is then added back before the
        // uPC4558's finite bandwidth, slew and output swing are applied.
        const float linearOutput = gainTransfer.process(x);
        const float feedbackVoltage = linearOutput - x;
        // OpAmpStage uses normalized units (3 V per unit); the diode solver
        // works in volts and solves the actual 1S2473 Shockley branches.
        const float clippedFeedback = feedbackDiodes.process(3.f * feedbackVoltage) / 3.f;
        const float noiseGain = 1.f + feedbackR / 4700.f;
        float y = clipOpamp.process(x + clippedFeedback, noiseGain);

        y = toneTransfer.process(y);
        y = toneOpamp.process(y, 3.f);
        y = toneOutputC7.process(y) * outputGain;
        y = outputBufferPole.process(q6Follower(y));
        return finalC8.process(y);
    }
};

} // namespace superdrive
#endif
