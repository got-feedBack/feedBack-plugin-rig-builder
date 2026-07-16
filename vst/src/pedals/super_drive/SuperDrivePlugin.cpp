/*
 * SuperDrive - component-guided Boss SD-1 style overdrive.
 *
 * Reference: pedals/super drive.pdf. The real panel is Level, Tone and Drive;
 * Rocksmith Gain/Tone are mapped onto Drive/Tone for preset compatibility.
 */
#include "DistrhoPlugin.hpp"
#include "SuperDriveParams.h"
#include "SuperDriveCore.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

class SuperDrivePlugin : public Plugin
{
    superdrive::SuperDriveCore left;
    superdrive::SuperDriveCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    float params[kParamCount];

    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        left.setParams(params[kDrive], params[kTone], params[kLevel]);
        right.setParams(params[kDrive], params[kTone], params[kLevel]);
    }

public:
    SuperDrivePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kSuperDriveDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(kOS * sr);
        right.setSampleRate(kOS * sr);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "SuperDrive"; }
    const char* getDescription() const override { return "SD-1 style super overdrive"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'p', 'D', 'r'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kSuperDriveNames[index];
        parameter.symbol = kSuperDriveSymbols[index];
        parameter.ranges.min = kSuperDriveMin[index];
        parameter.ranges.max = kSuperDriveMax[index];
        parameter.ranges.def = kSuperDriveDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SuperDrivePlugin)
};

Plugin* createPlugin()
{
    return new SuperDrivePlugin();
}

END_NAMESPACE_DISTRHO
