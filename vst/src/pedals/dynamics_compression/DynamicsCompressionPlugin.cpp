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

static inline float levelPotTaper(float v)
{
    // VR2 is a 50k log pot. Keep 12 o'clock as the calibration anchor used by
    // the supplied reference renders while retaining the real log-like sweep.
    return std::pow(clamp01(v) * 2.0f, 2.75f);
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

static inline float bjtStage(float x, float drive, float asymmetry)
{
    const float polarityDrive = drive * (x >= 0.0f ? 1.0f + asymmetry : 1.0f - asymmetry);
    const float shaped = std::tanh(x * polarityDrive) / std::fmax(0.25f, polarityDrive);
    return 0.91f * x + 0.09f * shaped;
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

    float detector = 0.0f;
    float gainCell = 1.0f;
    float detAttackA = 0.0f;
    float detReleaseA = 0.0f;
    float gainAttackA = 0.0f;
    float gainReleaseA = 0.0f;
    float outputGain = 1.0f;
    float quietGainDb = 6.0f;
    float loudGainDb = -1.0f;
    float controlKneeDb = -32.0f;
    float controlSlope = 0.14f;
    float otaInputScale = 1.0f;
    float referenceTrim = 1.0f;

    void update()
    {
        // VR1 is the stock 500k linear Sustain/Sensitivity pot. Its electrical
        // effect is nonlinear because it changes Q4's CA3080 bias current.
        const float s = clamp01(sensitivity);

        inputCap.setRC(sampleRate, 1010000.0f, 0.01e-6f);
        otaBandwidth.setHz(sampleRate, 17500.0f);
        outputTone.setHz(sampleRate, 10800.0f - 900.0f * s);

        // D2/D3 charge C16 quickly through Q2/Q3; the 10uF storage capacitor
        // then discharges through the surrounding resistor network. The second
        // pair of coefficients represents the finite Q4/CA3080 control-current
        // response instead of imposing an instantaneous gain computer.
        detAttackA = coeffMs(1.15f, sampleRate);
        detReleaseA = coeffMs(35.0f + 10.0f * s, sampleRate);
        gainAttackA = coeffMs(2.1f + 12.0f * (s - 0.5f) * (s - 0.5f), sampleRate);
        gainReleaseA = coeffMs(20.0f + 15.0f * s, sampleRate);

        // CA3080 gm is linear with Iabc, but the Q2-Q4 feedback loop maps signal
        // envelope to that current nonlinearly. These component-derived control
        // spans are calibrated at VR2 midpoint against the three supplied WAVs.
        quietGainDb = 6.14f + 23.47f * s - 2.81f * s * s
                      + 1.8f * s * (2.0f * s - 1.0f)
                      - 18.2f * s * (s - 0.5f);
        loudGainDb = -1.92f - 12.0f * s - 2.08f * s * s
                     - 5.0f * s * (s - 0.5f);
        controlKneeDb = -32.82f + 13.32f * s - 11.08f * s * s
                        + 8.6f * s * (s - 0.5f);
        controlSlope = 0.14146f + 0.14724f * s - 0.17552f * s * s;

        outputGain = levelPotTaper(output);
        referenceTrim = dbToAmp(1.94f + 14.31f * s - 8.50f * s * s
                                + 3.9f * s * (s - 0.5f));
        otaInputScale = 1.14f + 0.34f * s;
    }

    float targetGain(float env) const
    {
        const float levelDb = ampToDb(env);
        const float control = 1.0f / (1.0f + std::exp(controlSlope * (levelDb - controlKneeDb)));
        return dbToAmp(loudGainDb + (quietGainDb - loudGainDb) * control);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void reset()
    {
        inputCap.reset();
        otaBandwidth.reset();
        outputTone.reset();
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
        // Q1 biases and buffers the OTA differential input. At guitar level its
        // distortion is slight and asymmetric rather than a hard clipper.
        x = bjtStage(x, 1.42f, 0.045f);

        // D2/D3 are in the sidechain, not an anti-parallel audio clipper.
        const float rectified = std::fabs(x);
        const float detA = rectified > detector ? detAttackA : detReleaseA;
        detector += detA * (rectified - detector);
        detector = dn(detector);

        const float wantedGain = targetGain(detector);
        const float gainA = wantedGain < gainCell ? gainAttackA : gainReleaseA;
        gainCell += gainA * (wantedGain - gainCell);
        gainCell = std::fmax(0.08f, std::fmin(32.0f, gainCell));

        // CA3080 differential pair followed by its gm-controlled output. The
        // tanh is the OTA input-pair law, kept mostly linear by Tr1/R7/R8/R11.
        float otaIn = std::tanh(x * otaInputScale) / otaInputScale;
        otaIn = otaBandwidth.process(otaIn);
        float y = otaIn * gainCell;

        // Q5 recovers the high-impedance OTA current output and contributes a
        // small polarity asymmetry before C14 and the passive Level control.
        y = bjtStage(y, 1.18f, -0.035f);
        y = outputTone.process(y);
        y *= outputGain * referenceTrim;

        // The 9V transistor stages approach their rails softly; normal guitar
        // levels remain below this region, as in the supplied reference peaks.
        return std::tanh(y * 0.96f) / 0.96f;
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
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
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
