/*
 * GermaniumDrive - component-guided Skywave/Broadcast-style preamp drive.
 *
 * Reference: pedals/germanium drive.pdf. Real controls are Gain, LowCut,
 * Level, GainMode and Voltage. Rocksmith Gain/Tone map onto Gain/LowCut for
 * preset compatibility; GainMode and Voltage use static defaults unless edited.
 */
#include "DistrhoPlugin.hpp"
#include "GermaniumDriveParams.h"
#include "GermaniumDriveCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float quantize3(float v)
{
    v = clamp01(v);
    return v < 0.25f ? 0.0f : (v < 0.75f ? 0.5f : 1.0f);
}

static inline float finalLimit(float x)
{
    return std::tanh(0.98f * x);
}

} // namespace

class GermaniumDrivePlugin : public Plugin
{
    germaniumdrive::GermaniumDriveCore left;
    germaniumdrive::GermaniumDriveCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setLowCut(params[kLowCut]);
        right.setLowCut(params[kLowCut]);
        left.setGainMode(params[kGainMode]);
        right.setGainMode(params[kGainMode]);
        left.setVoltage(params[kVoltage]);
        right.setVoltage(params[kVoltage]);
    }

public:
    GermaniumDrivePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kGermaniumDriveDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "GermaniumDrive"; }
    const char* getDescription() const override { return "Skywave/Broadcast style germanium preamp drive"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('G', 'd', 'r', 'v'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kGermaniumDriveNames[index];
        parameter.symbol = kGermaniumDriveSymbols[index];
        parameter.ranges.min = kGermaniumDriveMin[index];
        parameter.ranges.max = kGermaniumDriveMax[index];
        parameter.ranges.def = kGermaniumDriveDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = (index == (uint32_t)kGainMode || index == (uint32_t)kVoltage)
            ? quantize3(value)
            : clamp01(value);
        applyAll();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        const float sr = (float)newSampleRate;
        osL.reset();
        osR.reset();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];
        const float level = 3.7f * params[kLevel];
        float ubL[kOS];
        float ubR[kOS];

        for (uint32_t i = 0; i < frames; ++i)
        {
            osL.upsample(inL[i], ubL);
            osR.upsample(inR[i], ubR);
            for (int k = 0; k < kOS; ++k)
            {
                ubL[k] = left.process(ubL[k]);
                ubR[k] = right.process(ubR[k]);
            }

            outL[i] = finalLimit(osL.downsample(ubL) * level);
            outR[i] = finalLimit(osR.downsample(ubR) * level);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GermaniumDrivePlugin)
};

Plugin* createPlugin()
{
    return new GermaniumDrivePlugin();
}

END_NAMESPACE_DISTRHO
