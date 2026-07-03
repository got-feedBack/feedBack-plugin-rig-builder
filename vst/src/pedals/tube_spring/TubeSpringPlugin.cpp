/*
 * TubeSpring / Real Spring - component-guided spring tank reverb.
 *
 * Local reference: pedals/spring reverb.gif. The circuit is a +/-35 V
 * discrete driver, not a tube preamp: C1 input coupling, VR1/R1/R3 bias and
 * dwell trim, Q1 BC546 voltage gain, D1/D2 biased complementary emitter
 * followers around Q2/Q3/Q4, C4 10 uF bipolar tank coupling, and the spring
 * tank pickup as the wet output.
 *
 * Rocksmith exposes Mix and Depth. Mix remains the wet/dry blend; Depth is
 * mapped to the schematic's VR1/dwell behaviour: more tank drive, longer decay,
 * stronger class-AB nonlinearity, and more spring drip.
 */
#include "DistrhoPlugin.hpp"
#include "TubeSpringParams.h"
#include <cmath>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr int kInputDiffuserCount = 3;
static constexpr int kTankLineCount = 6;
static constexpr int kDispersionCount = 3;

// Schematic values used to anchor the filter/coupling constants.
static constexpr float kC1InputCoupling = 100.0e-9f;
static constexpr float kC4TankCoupling = 10.0e-6f;
static constexpr float kR1BaseStop = 22000.0f;
static constexpr float kR3BiasTop = 10000.0f;
static constexpr float kR4CollectorLoad = 10000.0f;
static constexpr float kVR1Dwell = 10000.0f;
static constexpr float kEmitterBallast = 22.0f;
static constexpr float kTankPrimaryR = 180.0f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    if (hz < 20.0f)
        return 20.0f;
    return hz > nyquist ? nyquist : hz;
}

static inline float onePoleCoeff(float hz, float sr)
{
    hz = clampFreq(hz, sr);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

static inline float highpassFromRC(float capacitance, float resistance)
{
    const float tau = 2.0f * kPi * capacitance * resistance;
    return tau > 1.0e-12f ? 1.0f / tau : 20.0f;
}

static inline int msToSamples(float ms, float sr)
{
    int samples = (int)std::floor(ms * 0.001f * sr + 0.5f);
    return samples < 1 ? 1 : samples;
}

static inline float sanitize(float x)
{
    if (!std::isfinite(x))
        return 0.0f;
    if (x > 8.0f)
        return 8.0f;
    if (x < -8.0f)
        return -8.0f;
    return x;
}

static inline float softRectifier(float x, float knee)
{
    return 0.5f * (x + std::sqrt(x * x + knee * knee));
}

static inline float softClip(float x)
{
    return std::tanh(sanitize(x));
}

class OnePole
{
    float a = 0.0f;
    float z = 0.0f;

public:
    void set(float sr, float hz)
    {
        a = onePoleCoeff(hz, sr);
    }

    void reset()
    {
        z = 0.0f;
    }

    float process(float x)
    {
        z += a * (x - z);
        return z;
    }
};

class Biquad
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f)
            na0 = 1.0f;
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        a1 = na1 * invA0;
        a2 = na2 * invA0;
    }

public:
    void reset()
    {
        z1 = z2 = 0.0f;
    }

    float process(float x)
    {
        x = sanitize(x);
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return sanitize(y);
    }

    void setHighPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setBandPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(alpha, 0.0f, -alpha, 1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setPeak(float sr, float hz, float q, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a,
            1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }
};

class DelayBuffer
{
    std::vector<float> data;
    int writeIndex = 0;

public:
    void resize(int samples)
    {
        if (samples < 2)
            samples = 2;
        data.assign((size_t)samples, 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = 0.0f;
        writeIndex = 0;
    }

    float read(int delaySamples) const
    {
        const int size = (int)data.size();
        if (size <= 1)
            return 0.0f;
        if (delaySamples >= size)
            delaySamples = size - 1;
        int index = writeIndex - delaySamples;
        while (index < 0)
            index += size;
        return data[(size_t)index];
    }

    void write(float x)
    {
        if (data.empty())
            return;
        data[(size_t)writeIndex] = sanitize(x);
        ++writeIndex;
        if (writeIndex >= (int)data.size())
            writeIndex = 0;
    }
};

class AllpassDelay
{
    DelayBuffer buffer;
    int delaySamples = 1;
    float feedback = 0.58f;

public:
    void prepare(float sr, float maxMs)
    {
        buffer.resize(msToSamples(maxMs, sr) + 8);
    }

    void reset()
    {
        buffer.reset();
    }

    void set(float sr, float delayMs, float fb)
    {
        delaySamples = msToSamples(delayMs, sr);
        feedback = fb;
    }

    float process(float x)
    {
        const float delayed = buffer.read(delaySamples);
        const float y = delayed - feedback * x;
        buffer.write(x + feedback * y);
        return sanitize(y);
    }
};

class SpringLine
{
    DelayBuffer buffer;
    AllpassDelay dispersers[kDispersionCount];
    int delaySamples = 1;
    float feedback = 0.84f;
    float damping = 0.38f;
    float dampState = 0.0f;

public:
    void prepare(float sr, float maxMs)
    {
        buffer.resize(msToSamples(maxMs, sr) + 8);
        for (int i = 0; i < kDispersionCount; ++i)
            dispersers[i].prepare(sr, 16.0f);
    }

    void reset()
    {
        dampState = 0.0f;
        buffer.reset();
        for (int i = 0; i < kDispersionCount; ++i)
            dispersers[i].reset();
    }

    void set(float sr, float delayMs, float fb, float damp, float dispersionMs, float skew)
    {
        delaySamples = msToSamples(delayMs * skew, sr);
        feedback = fb;
        damping = damp;
        for (int i = 0; i < kDispersionCount; ++i)
        {
            const float stage = (float)i + 1.0f;
            const float delay = (dispersionMs * (0.52f + 0.34f * stage)) * skew;
            const float apFb = 0.58f + 0.055f * (float)i;
            dispersers[i].set(sr, delay, apFb);
        }
    }

    float process(float x)
    {
        const float delayed = buffer.read(delaySamples);
        dampState = delayed * (1.0f - damping) + dampState * damping;
        buffer.write(x + dampState * feedback);

        float y = delayed;
        for (int i = 0; i < kDispersionCount; ++i)
            y = dispersers[i].process(y);
        return sanitize(y);
    }
};

} // namespace

class TubeSpringCore
{
    float sampleRate = 48000.0f;
    float stereoSkew = 1.0f;
    float mix = kTubeSpringDef[kMix];
    float depth = kTubeSpringDef[kDepth];

    // Input/bias network: C1 into R3/VR1/R1 around Q1's base.
    Biquad c1InputHp;
    Biquad q1MillerLp;
    Biquad q1EmitterBypassHp;

    // Class-AB driver and transformer/tank coupling around D1/D2, R7/R8, C4.
    Biquad tankCouplingHp;
    Biquad tankDriverLp;
    OnePole railReservoir;

    // Spring tank and pickup.
    AllpassDelay inputDiffusers[kInputDiffuserCount];
    SpringLine lines[kTankLineCount];
    Biquad tankHp;
    Biquad tankLp;
    Biquad pickupBody;
    Biquad pickupClangA;
    Biquad pickupClangB;
    Biquad pickupNotch;

    // Transient "drip" generated by spring acceleration at the input transducer.
    Biquad dripA;
    Biquad dripB;
    float fastEnv = 0.0f;
    float slowEnv = 0.0f;
    float fastCoeff = 0.0f;
    float slowCoeff = 0.0f;

    float q1Memory = 0.0f;
    float driverCurrent = 0.0f;

    void updateCircuit()
    {
        const float d = smoothstep(depth);

        const float inputBiasR =
            1.0f / (1.0f / kR1BaseStop + 1.0f / kR3BiasTop + 1.0f / (kVR1Dwell * (0.38f + 0.62f * d)));
        const float c1Hz = highpassFromRC(kC1InputCoupling, inputBiasR);
        c1InputHp.setHighPass(sampleRate, c1Hz * (0.88f + 0.18f * d), 0.70f);

        // C3 is only 10 pF, but Q1 Miller multiplication makes it an audible
        // stability rolloff when the dwell trim is high.
        q1MillerLp.setLowPass(sampleRate, 10400.0f - 2700.0f * d, 0.66f);
        q1EmitterBypassHp.setHighPass(sampleRate, 54.0f + 18.0f * d, 0.72f);

        const float c4Hz = highpassFromRC(kC4TankCoupling, kTankPrimaryR + 2.0f * kEmitterBallast);
        tankCouplingHp.setHighPass(sampleRate, c4Hz * (0.85f + 0.20f * (1.0f - d)), 0.58f);
        tankDriverLp.setLowPass(sampleRate, 4750.0f + 600.0f * d, 0.70f);
        railReservoir.set(sampleRate, 9.5f);

        fastCoeff = onePoleCoeff(310.0f, sampleRate);
        slowCoeff = onePoleCoeff(21.0f, sampleRate);

        tankHp.setHighPass(sampleRate, 145.0f + 35.0f * (1.0f - d), 0.70f);
        tankLp.setLowPass(sampleRate, 3650.0f + 950.0f * d, 0.62f);
        pickupBody.setPeak(sampleRate, 820.0f + 70.0f * d, 1.08f, 2.4f + 1.6f * d);
        pickupClangA.setBandPass(sampleRate, 1820.0f + 260.0f * d, 5.6f + 2.0f * d);
        pickupClangB.setBandPass(sampleRate, 2860.0f + 420.0f * d, 4.4f + 2.4f * d);
        pickupNotch.setPeak(sampleRate, 5200.0f, 1.15f, -3.2f);
        dripA.setBandPass(sampleRate, 1700.0f + 360.0f * d, 5.2f + 4.0f * d);
        dripB.setBandPass(sampleRate, 3150.0f + 540.0f * d, 4.2f + 3.5f * d);
    }

    void updateTank()
    {
        const float d = smoothstep(depth);

        static const float diffuserMs[kInputDiffuserCount] = { 3.2f, 8.9f, 14.7f };
        const float diffuserFb = 0.48f + 0.12f * d;
        for (int i = 0; i < kInputDiffuserCount; ++i)
            inputDiffusers[i].set(sampleRate, diffuserMs[i] * stereoSkew, diffuserFb);

        static const float lineMs[kTankLineCount] = { 26.7f, 31.9f, 39.4f, 47.8f, 58.2f, 72.6f };
        static const float dispersionMs[kTankLineCount] = { 1.9f, 2.6f, 3.7f, 4.8f, 6.1f, 7.4f };

        const float stretch = 0.96f + 0.13f * d;
        // Longer, more spring-like decay: a real tank rings ~1.5-2.5 s. Raise the
        // line feedback and lighten the loop damping (Depth still lengthens it).
        const float fbBase = 0.84f + 0.085f * d;
        const float dampBase = 0.34f - 0.12f * d;
        for (int i = 0; i < kTankLineCount; ++i)
        {
            const float lineSkew = stereoSkew * (0.986f + 0.006f * (float)i);
            const float fb = fbBase * (1.0f - 0.020f * (float)i);
            const float damp = dampBase + 0.015f * (float)(i % 3);
            lines[i].set(sampleRate, lineMs[i] * stretch, fb, damp, dispersionMs[i] * (1.0f + 0.35f * d), lineSkew);
        }
    }

    float processInputDriver(float in, float d)
    {
        float x = c1InputHp.process(in);
        x = q1EmitterBypassHp.process(x);

        // Q1 BC546 gain stage. R4/R1 ratio anchors the small-signal gain; VR1
        // raises dwell before the class-AB followers load the signal.
        // Dwell raises drive, but the original 9->30x swing made Depth act as a
        // big VOLUME control. Tighten the range so Depth is mostly character.
        const float q1Gain = (kR4CollectorLoad / kR1BaseStop) * (13.0f + 9.0f * d);
        float q1 = q1MillerLp.process(x * q1Gain);
        q1 += 0.018f * q1Memory;
        q1Memory = q1;

        // Single-ended Q1 clipping is slightly asymmetric.
        q1 = softClip(q1 * (0.78f + 0.28f * d));
        q1 += 0.030f * q1 * q1;

        // D1/D2 and the complementary emitter follower pair produce a soft
        // class-AB handoff. The 22R emitter resistors limit current into the
        // tank transformer and tame the crossover edge.
        const float biasSpread = 0.070f - 0.028f * d;
        const float knee = 0.052f + 0.018f * (1.0f - d);
        const float upper = softRectifier(q1 - biasSpread, knee);
        const float lower = softRectifier(-q1 - biasSpread, knee);
        float pushPull = upper - lower;

        const float currentSense = std::fabs(pushPull);
        driverCurrent += onePoleCoeff(72.0f, sampleRate) * (currentSense - driverCurrent);
        const float reservoir = railReservoir.process(driverCurrent);
        const float railCompression = 1.0f / (1.0f + reservoir * (0.22f + 0.24f * d));

        pushPull = softClip(pushPull * (1.65f + 0.76f * d) * railCompression);
        pushPull = tankDriverLp.process(pushPull);
        return tankCouplingHp.process(pushPull);
    }

    float processDrip(float tankDrive, float d)
    {
        const float absX = std::fabs(tankDrive);
        fastEnv += fastCoeff * (absX - fastEnv);
        slowEnv += slowCoeff * (absX - slowEnv);
        const float transient = fastEnv > slowEnv ? fastEnv - slowEnv : 0.0f;
        const float transientSigned = tankDrive >= 0.0f ? transient : -transient;
        const float drip = dripA.process(transientSigned) + 0.65f * dripB.process(transientSigned);
        return drip * (0.14f + 1.15f * d);
    }

public:
    void setStereoSkew(float skew)
    {
        stereoSkew = skew;
        updateTank();
    }

    void reset()
    {
        fastEnv = slowEnv = q1Memory = driverCurrent = 0.0f;
        c1InputHp.reset();
        q1MillerLp.reset();
        q1EmitterBypassHp.reset();
        tankCouplingHp.reset();
        tankDriverLp.reset();
        railReservoir.reset();
        tankHp.reset();
        tankLp.reset();
        pickupBody.reset();
        pickupClangA.reset();
        pickupClangB.reset();
        pickupNotch.reset();
        dripA.reset();
        dripB.reset();
        for (int i = 0; i < kInputDiffuserCount; ++i)
            inputDiffusers[i].reset();
        for (int i = 0; i < kTankLineCount; ++i)
            lines[i].reset();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        for (int i = 0; i < kInputDiffuserCount; ++i)
            inputDiffusers[i].prepare(sampleRate, 24.0f);
        for (int i = 0; i < kTankLineCount; ++i)
            lines[i].prepare(sampleRate, 135.0f);
        updateCircuit();
        updateTank();
        reset();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
    }

    void setDepth(float v)
    {
        depth = clamp01(v);
        updateCircuit();
        updateTank();
    }

    float process(float in)
    {
        const float dry = sanitize(in);
        const float d = smoothstep(depth);

        float tankDrive = processInputDriver(dry, d);
        const float drip = processDrip(tankDrive, d);
        tankDrive = tankDrive * (0.58f + 0.55f * d) + drip;

        for (int i = 0; i < kInputDiffuserCount; ++i)
            tankDrive = inputDiffusers[i].process(tankDrive);

        float tank = 0.0f;
        tank += lines[0].process(tankDrive) * 0.92f;
        tank += lines[1].process(tankDrive) * -0.74f;
        tank += lines[2].process(tankDrive) * 0.69f;
        tank += lines[3].process(tankDrive) * -0.58f;
        tank += lines[4].process(tankDrive) * 0.47f;
        tank += lines[5].process(tankDrive) * -0.39f;
        tank *= 0.245f;

        float wet = tankHp.process(tank);
        wet = tankLp.process(wet);
        wet = pickupBody.process(wet);
        wet += pickupClangA.process(tank) * (0.030f + 0.050f * d);
        wet += pickupClangB.process(tank) * (0.018f + 0.034f * d);
        wet += drip * (0.010f + 0.020f * d);
        wet = pickupNotch.process(wet);

        // The tank pickup is low-level and bandwidth-limited; avoid turning
        // dwell into a boost pedal while still letting the spring tail breathe.
        // The trim now COMPENSATES dwell (drops as Depth/drive rise) so the wet
        // RMS stays ~flat across Depth -> Depth changes character, not volume.
        wet = softClip(wet * (1.02f + 0.12f * d)) * (0.78f - 0.16f * d);

        // Equal-power dry/wet crossfade: typical Mix keeps plenty of dry (mix
        // 0.3 -> 89% dry) and the loudness stays ~constant, so full Mix no longer
        // balloons. wetTrim is ~flat in Depth (Depth = character, not volume).
        const float a = std::pow(mix, 1.9f) * 1.5707963f;   // gentler Mix taper: 1/4 knob ~subtle, not near-full
        const float dryLevel = std::cos(a);
        const float wetLevel = std::sin(a) * (0.88f - 0.06f * d);
        return sanitize((dry * dryLevel + wet * wetLevel) * 0.985f);
    }
};

class TubeSpringPlugin : public Plugin
{
    TubeSpringCore left;
    TubeSpringCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setDepth(params[kDepth]);
        right.setDepth(params[kDepth]);
    }

public:
    TubeSpringPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTubeSpringDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        left.setStereoSkew(0.991f);
        right.setStereoSkew(1.023f);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "TubeSpring"; }
    const char* getDescription() const override { return "Discrete high-voltage spring tank reverb"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('T', 'b', 'S', 'p'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kTubeSpringNames[index];
        parameter.symbol = kTubeSpringSymbols[index];
        parameter.ranges.min = kTubeSpringMin[index];
        parameter.ranges.max = kTubeSpringMax[index];
        parameter.ranges.def = kTubeSpringDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = clamp01(value);
        applyAll();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
        left.setStereoSkew(0.991f);
        right.setStereoSkew(1.023f);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            outL[i] = left.process(inL[i]);
            outR[i] = right.process(inR[i]);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TubeSpringPlugin)
};

Plugin* createPlugin()
{
    return new TubeSpringPlugin();
}

END_NAMESPACE_DISTRHO
