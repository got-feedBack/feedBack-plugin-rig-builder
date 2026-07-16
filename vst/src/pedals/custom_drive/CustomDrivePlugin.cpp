/*
 * CustomDrive - component-guided OCD-style MOSFET/diode overdrive.
 *
 * Reference: pedals/custom drive.png. Real panel: Drive, Tone, HP/LP Voice and
 * Volume. Rocksmith Gain/Tone/Voice map onto Drive/Tone/Voice.
 */
#include "DistrhoPlugin.hpp"
#include "CustomDriveParams.h"
#include "CustomDriveCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

class CustomDrivePlugin : public Plugin
{
    customdrive::CustomDriveCore left;
    customdrive::CustomDriveCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        left.setParams(params[kDrive], params[kTone], params[kVoice], params[kVolume]);
        right.setParams(params[kDrive], params[kTone], params[kVoice], params[kVolume]);
    }

public:
    CustomDrivePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kCustomDriveDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "CustomDrive"; }
    const char* getDescription() const override { return "OCD-style MOSFET/diode overdrive"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('C', 'd', 'r', 'v'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == (uint32_t)kVoice)
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        parameter.name = kCustomDriveNames[index];
        parameter.symbol = kCustomDriveSymbols[index];
        parameter.ranges.min = kCustomDriveMin[index];
        parameter.ranges.max = kCustomDriveMax[index];
        parameter.ranges.def = kCustomDriveDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        params[index] = (index == (uint32_t)kVoice) ? (value >= 0.5f ? 1.0f : 0.0f)
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

            const float wetL = osL.downsample(ubL);
            const float wetR = osR.downsample(ubR);
            outL[i] = wetL;
            outR[i] = wetR;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomDrivePlugin)
};

Plugin* createPlugin()
{
    return new CustomDrivePlugin();
}

END_NAMESPACE_DISTRHO
