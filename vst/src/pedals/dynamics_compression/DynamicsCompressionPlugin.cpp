/*
 * DynamicsCompression - MXR Dyna Comp / Ross-style CA3080 OTA compressor.
 *
 * Real panel: Output + Sensitivity. The model follows the classic schematic:
 * BJT input buffer, CA3080 transconductance gain cell, rectified sidechain and
 * fixed RC ballistics. No closed-loop RMS makeup is used; the Output pot is the
 * real loudness control.
 */
#include "DistrhoPlugin.hpp"
#include "DynamicsCompressionParams.h"
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

static inline float dbToAmp(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float ampToDb(float amp)
{
    return 20.0f * std::log10(std::fmax(amp, 1.0e-8f));
}

static inline float coeffMs(float ms, float sr)
{
    return 1.0f - std::exp(-1.0f / std::fmax(1.0f, ms * 0.001f * sr));
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.65f);
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

class OnePoleLowPass
{
    float a = 0.0f;
    float y = 0.0f;

public:
    void setHz(float sr, float hz)
    {
        a = 1.0f - std::exp(-2.0f * kPi * hz / (sr > 1000.0f ? sr : 48000.0f));
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

static inline float bjtBuffer(float x, float drive)
{
    const float shaped = std::tanh(x * drive);
    return 0.86f * x + 0.14f * shaped / std::fmax(0.25f, drive);
}

} // namespace

class DynamicsCompressionCore
{
    float sampleRate = 48000.0f;
    float output = kDynamicsCompressionDef[kOutput];
    float sensitivity = kDynamicsCompressionDef[kSensitivity];

    RcHighPass inputCap;
    OnePoleLowPass otaBandwidth;
    OnePoleLowPass outputTone;
    rbshared::OpAmpStage recoveryAmp;
    rbcomponents::AntiParallelDiodePair otaInputClamp;

    float detector = 0.0f;
    float gainCell = 1.0f;
    float detAttackA = 0.0f;
    float detReleaseA = 0.0f;
    float gainAttackA = 0.0f;
    float gainReleaseA = 0.0f;
    float outputGain = 1.0f;
    float makeupGain = 1.0f;
    float thresholdDb = -24.0f;
    float ratio = 5.0f;
    float maxReductionDb = 18.0f;
    float otaDrive = 1.0f;

    void update()
    {
        const float s = audioTaper(sensitivity);

        inputCap.setRC(sampleRate, 1010000.0f, 0.01e-6f);
        otaBandwidth.setHz(sampleRate, 11800.0f - 3200.0f * s);
        outputTone.setHz(sampleRate, 7600.0f - 1700.0f * s);

        // CA3080 sidechain: very fast charge, slower sustain/release as the
        // Sensitivity pot raises the control current.
        detAttackA = coeffMs(1.4f + 2.2f * (1.0f - s), sampleRate);
        detReleaseA = coeffMs(85.0f + 410.0f * s, sampleRate);
        gainAttackA = coeffMs(2.2f + 4.0f * (1.0f - s), sampleRate);
        gainReleaseA = coeffMs(70.0f + 360.0f * s, sampleRate);

        thresholdDb = -10.0f - 34.0f * s;
        ratio = 1.45f + 11.5f * s;
        maxReductionDb = 8.0f + 22.0f * s;
        makeupGain = dbToAmp(1.0f + 12.0f * s);
        outputGain = dbToAmp(-18.0f + 30.0f * clamp01(output));
        otaDrive = 1.05f + 0.72f * s;

        otaInputClamp.setSpec(rbcomponents::diode1N914());
        otaInputClamp.setSourceR(8200.0f - 3200.0f * s);
    }

    float targetGain(float env) const
    {
        const float levelDb = ampToDb(env);
        const float over = levelDb - thresholdDb;
        if (over <= -3.0f)
            return 1.0f;

        const float kneeDb = 6.0f;
        float effectiveOver = over;
        if (over < 0.5f * kneeDb)
        {
            const float x = over + 0.5f * kneeDb;
            effectiveOver = (x * x) / (2.0f * kneeDb);
        }

        const float reductionDb = std::fmin(maxReductionDb, effectiveOver * (1.0f - 1.0f / ratio));
        return dbToAmp(-reductionDb);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        recoveryAmp.setSpec(rbshared::m5218Spec());
        recoveryAmp.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        inputCap.reset();
        otaBandwidth.reset();
        outputTone.reset();
        recoveryAmp.reset();
        otaInputClamp.reset();
        detector = 0.0f;
        gainCell = 1.0f;
        update();
    }

    void setOutput(float v)
    {
        output = clamp01(v);
        update();
    }

    void setSensitivity(float v)
    {
        sensitivity = clamp01(v);
        update();
    }

    float process(float in)
    {
        float x = inputCap.process(in);
        x = bjtBuffer(x, 1.18f + 0.22f * sensitivity);

        const float rectified = std::fabs(x);
        const float detA = rectified > detector ? detAttackA : detReleaseA;
        detector += detA * (rectified - detector);
        detector = dn(detector);

        const float wantedGain = targetGain(detector);
        const float gainA = wantedGain < gainCell ? gainAttackA : gainReleaseA;
        gainCell += gainA * (wantedGain - gainCell);
        gainCell = std::fmax(0.025f, std::fmin(1.0f, gainCell));

        // The OTA input pair rounds before the current-controlled gain cell.
        float otaIn = otaInputClamp.process(x * otaDrive);
        otaIn = otaBandwidth.process(otaIn);
        float y = otaIn * gainCell * makeupGain;
        y = recoveryAmp.process(y, 3.0f + 7.0f * sensitivity);
        y = outputTone.process(y);
        y *= outputGain;
        return std::tanh(y * 1.06f) * 0.98f;
    }
};

class DynamicsCompressionPlugin : public Plugin
{
    DynamicsCompressionCore left;
    DynamicsCompressionCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setOutput(params[kOutput]);
        right.setOutput(params[kOutput]);
        left.setSensitivity(params[kSensitivity]);
        right.setSensitivity(params[kSensitivity]);
    }

public:
    DynamicsCompressionPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDynamicsCompressionDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "DynamicsCompression"; }
    const char* getDescription() const override { return "CA3080 OTA Dyna Comp-style compressor"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', 'c', 'm', 'p'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDynamicsCompressionNames[index];
        parameter.symbol = kDynamicsCompressionSymbols[index];
        parameter.ranges.min = kDynamicsCompressionMin[index];
        parameter.ranges.max = kDynamicsCompressionMax[index];
        parameter.ranges.def = kDynamicsCompressionDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicsCompressionPlugin)
};

Plugin* createPlugin()
{
    return new DynamicsCompressionPlugin();
}

END_NAMESPACE_DISTRHO
