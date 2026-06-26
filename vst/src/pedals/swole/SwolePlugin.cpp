/*
 * Swole - Lazy Sprocket / Boss SG-1 style slow-gear pedal.
 *
 * Local reference: pedals/swole.gif. The real circuit is not a conventional
 * compressor: the input transistor feeds a JFET gain element, while a 741
 * detector, diode/zener clamp and transistor drivers charge the JFET control
 * network. Sensitivity sets how easily the control opens; Attack sets the RC
 * charge time of the swell.
 */
#include "DistrhoPlugin.hpp"
#include "SwoleParams.h"
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float dn(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

static inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float coeffMs(float ms, float sr)
{
    return 1.0f - std::exp(-1.0f / std::fmax(1.0f, ms * 0.001f * sr));
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.75f);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

class RcHighPass
{
    float a = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void setRC(float sr, float rOhm, float cFarad)
    {
        const float rc = rOhm * cFarad;
        const float dt = 1.0f / (sr > 1000.0f ? sr : 48000.0f);
        a = rc / (rc + dt);
    }

    void reset()
    {
        x1 = y1 = 0.0f;
    }

    float process(float x)
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
        const float rc = rOhm * cFarad;
        const float dt = 1.0f / (sr > 1000.0f ? sr : 48000.0f);
        a = dt / (rc + dt);
    }

    void setHz(float sr, float hz)
    {
        const float safeSr = sr > 1000.0f ? sr : 48000.0f;
        const float clamped = hz < 8.0f ? 8.0f : (hz > safeSr * 0.45f ? safeSr * 0.45f : hz);
        a = 1.0f - std::exp(-2.0f * kPi * clamped / safeSr);
    }

    void reset()
    {
        y = 0.0f;
    }

    float process(float x)
    {
        y += a * (x - y);
        y = dn(y);
        return y;
    }
};

static inline float transistorBuffer(float x, float drive)
{
    const float shaped = std::tanh(x * drive) / drive;
    return 0.90f * x + 0.10f * shaped;
}

static inline float transistorDriver(float x, float drive)
{
    const float bias = 0.018f * drive;
    const float y = std::tanh((x + bias) * drive) / drive - bias * 0.82f;
    return 0.82f * x + 0.18f * y;
}

} // namespace

class SwoleCore
{
    float sampleRate = 48000.0f;
    float sensitivity = kSwoleDef[kSensitivity];
    float attack = kSwoleDef[kAttack];

    RcHighPass inputC1;       // C1 47 nF into the T1 bias network.
    RcHighPass fetInputC3;    // C3 1 uF into the 2N5457/K30 gain cell.
    RcHighPass recoveryC4;    // C4 1 uF into the T2 recovery/output stage.
    RcHighPass outputC6;      // C6 1 uF output coupling.
    RcLowPass inputMiller;
    RcLowPass detectorC17;    // IC1 input smoothing around the 390 ohm/1 uF node.
    RcLowPass detectorC11;    // 741 feedback cap.
    RcLowPass outputLoad;

    rbshared::OpAmpStage detectorOpAmp;
    rbcomponents::AntiParallelDiodePair detectorDiodes;

    float detectorEnv = 0.0f;
    float gateControl = 0.0f;
    float meter = 0.0f;
    float detectorAttackA = 0.0f;
    float detectorReleaseA = 0.0f;
    float gateOpenA = 0.0f;
    float gateCloseA = 0.0f;
    float trim = 0.58f;

    void update()
    {
        const float sens = audioTaper(sensitivity);
        const float att = audioTaper(attack);

        inputC1.setRC(sampleRate, 220000.0f, 47.0e-9f);
        fetInputC3.setRC(sampleRate, 470000.0f + 22000.0f, 1.0e-6f);
        recoveryC4.setRC(sampleRate, 1000000.0f, 1.0e-6f);
        outputC6.setRC(sampleRate, 100000.0f, 1.0e-6f);

        inputMiller.setHz(sampleRate, 11800.0f - 2200.0f * sens);
        detectorC17.setRC(sampleRate, 390.0f, 1.0e-6f);
        detectorC11.setRC(sampleRate, 1000000.0f, 0.01e-6f);
        outputLoad.setHz(sampleRate, 15000.0f - 2400.0f * sens);

        detectorAttackA = coeffMs(1.1f + 2.8f * (1.0f - sens), sampleRate);
        detectorReleaseA = coeffMs(78.0f + 210.0f * (1.0f - sens), sampleRate);

        // VR2/C15/C16 charge behavior: low settings open almost immediately;
        // high settings create the SG-1 slow-gear swell.
        const float attackMs = 18.0f + 860.0f * std::pow(att, 1.35f);
        gateOpenA = coeffMs(attackMs, sampleRate);
        gateCloseA = coeffMs(42.0f + 105.0f * (1.0f - sens), sampleRate);

        detectorDiodes.setSpec(rbcomponents::diode1N914());
        detectorDiodes.setSourceR(47000.0f);
    }

    float detector(float x)
    {
        const float sens = audioTaper(sensitivity);
        float d = detectorC17.process(std::fabs(x) * (3.0f + 16.0f * sens));
        d = detectorOpAmp.process(d + detectorC11.process(d) * 0.18f, 28.0f);
        d = std::fabs(detectorDiodes.process(d));

        // D4 5.6 V zener, scaled by OpAmpStage's nominal 3 V per audio unit.
        d = d > 1.86f ? 1.86f + (d - 1.86f) * 0.08f : d;

        const float a = d > detectorEnv ? detectorAttackA : detectorReleaseA;
        detectorEnv += a * (d - detectorEnv);
        detectorEnv = dn(detectorEnv);
        return detectorEnv;
    }

    float jfetGain(float control) const
    {
        const float sens = audioTaper(sensitivity);
        const float open = smoothstep(control);
        const float offDb = -50.0f + 18.0f * (1.0f - sens);
        const float offGain = dbToGain(offDb);

        // 2N5457/K30 used as a voltage-controlled resistance: closed gate
        // gives a deep shunt; open gate approaches the fixed 470 k/1 M path.
        const float rds = 260.0f + 210000.0f * std::pow(1.0f - open, 2.35f);
        const float divider = 470000.0f / (470000.0f + rds);
        return offGain + (1.0f - offGain) * divider * open;
    }

public:
    void reset()
    {
        inputC1.reset();
        fetInputC3.reset();
        recoveryC4.reset();
        outputC6.reset();
        inputMiller.reset();
        detectorC17.reset();
        detectorC11.reset();
        outputLoad.reset();
        detectorOpAmp.reset();
        detectorDiodes.reset();
        detectorEnv = 0.0f;
        gateControl = 0.0f;
        meter = 0.0f;
        update();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        detectorOpAmp.setSpec(rbshared::lm741Spec());
        detectorOpAmp.setSampleRate(sampleRate);
        reset();
    }

    void setSensitivity(float v)
    {
        sensitivity = clamp01(v);
        update();
    }

    void setAttack(float v)
    {
        attack = clamp01(v);
        update();
    }

    float process(float in)
    {
        const float sens = audioTaper(sensitivity);
        float x = inputC1.process(in);
        x = inputMiller.process(transistorBuffer(x, 1.55f + 0.28f * sens));

        const float env = detector(x);
        const float threshold = 0.018f + 0.145f * std::pow(1.0f - sens, 1.8f);
        const float window = threshold * (2.8f + 3.4f * sens) + 0.006f;
        const float wantedOpen = smoothstep((env - threshold) / window);
        const float target = wantedOpen * (0.82f + 0.18f * trim);
        const float a = target > gateControl ? gateOpenA : gateCloseA;
        gateControl += a * (target - gateControl);
        gateControl = clamp01(gateControl);

        meter += coeffMs(38.0f, sampleRate) * ((1.0f - gateControl) - meter);

        x = fetInputC3.process(x);
        float y = x * jfetGain(gateControl);
        y = transistorDriver(y, 1.38f + 0.48f * sens);
        y = recoveryC4.process(y);
        y = transistorDriver(y * (1.65f + 0.55f * sens), 1.22f + 0.35f * sens);
        y = outputLoad.process(y);
        y = outputC6.process(y);

        // The game pedal is labeled as a heavy dynamics effect; keep a small
        // post-circuit color, but do not turn the SG-1 core into a fuzz.
        const float drive = 1.05f + 0.85f * sens * gateControl;
        y = 0.86f * y + 0.14f * (std::tanh(y * drive) / drive);
        return std::tanh(y * 1.18f) * 0.92f;
    }
};

class SwolePlugin : public Plugin
{
    SwoleCore left;
    SwoleCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setSensitivity(params[kSensitivity]);
        right.setSensitivity(params[kSensitivity]);
        left.setAttack(params[kAttack]);
        right.setAttack(params[kAttack]);
    }

public:
    SwolePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kSwoleDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Swole"; }
    const char* getDescription() const override { return "Lazy Sprocket / SG-1 style slow gear"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'w', 'O', 'l'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kSwoleNames[index];
        parameter.symbol = kSwoleSymbols[index];
        parameter.ranges.min = kSwoleMin[index];
        parameter.ranges.max = kSwoleMax[index];
        parameter.ranges.def = kSwoleDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwolePlugin)
};

Plugin* createPlugin()
{
    return new SwolePlugin();
}

END_NAMESPACE_DISTRHO
