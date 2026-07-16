/* RotaVibe - restrained two-band rotary speaker model.
 *
 * This is not an optical Uni-Vibe. It keeps separate horn/rotor Doppler and
 * amplitude paths, but Depth now controls both effects instead of leaving a
 * large fixed tremolo active at zero.
 */
#include "DistrhoPlugin.hpp"
#include "RotaVibeParams.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float onePoleCoef(float fc, float fs)
{
    return clamp01(1.0f - std::exp(-6.2831853f * fc / fs));
}

static const int kRBuf = 4096;

class RotorTap
{
    float buffer[kRBuf];
    int writeIndex = 0;

public:
    void reset()
    {
        std::memset(buffer, 0, sizeof(buffer));
        writeIndex = 0;
    }

    float process(float x, float delaySamples)
    {
        delaySamples = std::fmax(2.0f, std::fmin(delaySamples, (float)(kRBuf - 4)));
        float pos = (float)writeIndex - delaySamples;
        while (pos < 0.0f)
            pos += (float)kRBuf;
        const int i0 = (int)std::floor(pos);
        const int im1 = (i0 + kRBuf - 1) % kRBuf;
        const int i1 = (i0 + 1) % kRBuf;
        const int i2 = (i0 + 2) % kRBuf;
        const float t = pos - (float)i0;
        const float a = 0.5f * (-buffer[im1] + 3.0f * buffer[i0] - 3.0f * buffer[i1] + buffer[i2]);
        const float b = 0.5f * (2.0f * buffer[im1] - 5.0f * buffer[i0] + 4.0f * buffer[i1] - buffer[i2]);
        const float c = 0.5f * (-buffer[im1] + buffer[i1]);
        const float y = ((a * t + b) * t + c) * t + buffer[i0];
        buffer[writeIndex] = x;
        if (++writeIndex >= kRBuf)
            writeIndex = 0;
        return y;
    }
};

class RotaVibePlugin : public Plugin
{
    float sampleRate = 48000.0f;
    float crossoverState = 0.0f;
    float crossoverCoeff = 0.1f;
    RotorTap hornL, hornR, rotorL, rotorR;
    float hornPhase = 0.0f;
    float rotorPhase = 0.0f;
    float params[kParamCount];

    void prepare(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        crossoverCoeff = onePoleCoef(800.0f, sampleRate);
        crossoverState = 0.0f;
        hornL.reset();
        hornR.reset();
        rotorL.reset();
        rotorR.reset();
        hornPhase = rotorPhase = 0.0f;
    }

public:
    RotaVibePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kRotaVibeDef[i];
        prepare((float)getSampleRate());
    }

protected:
    const char* getLabel() const override { return "RotaVibe"; }
    const char* getDescription() const override { return "two-band rotary speaker"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'R', 'v', '1'); }

    void initParameter(uint32_t i, Parameter& p) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        p.hints = kParameterIsAutomatable;
        p.name = kRotaVibeNames[i];
        p.symbol = kRotaVibeSymbols[i];
        p.ranges.min = kRotaVibeMin[i];
        p.ranges.max = kRotaVibeMax[i];
        p.ranges.def = kRotaVibeDef[i];
    }

    float getParameterValue(uint32_t i) const override
    {
        return i < (uint32_t)kParamCount ? params[i] : 0.0f;
    }

    void setParameterValue(uint32_t i, float v) override
    {
        if (i < (uint32_t)kParamCount)
            params[i] = clamp01(v);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        prepare((float)newSampleRate);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float rateHz = 0.25f + 6.25f * params[kRate];
        const float depth = params[kDepth];
        const float mix = std::pow(params[kMix], 1.12f);
        const float mixAngle = mix * 1.5707963f;
        const float dryLevel = std::cos(mixAngle);
        const float wetLevel = std::sin(mixAngle);
        const float levelComp = 1.0f + 0.16f * mix;
        const float balance = params[kBalance];
        const float hornWidth = depth * 0.32f * 0.001f * sampleRate;
        const float rotorWidth = depth * 0.18f * 0.001f * sampleRate;
        const float hornBase = 0.80f * 0.001f * sampleRate;
        const float rotorBase = 0.95f * 0.001f * sampleRate;
        const float hornWeight = 0.55f + 0.90f * balance;
        const float rotorWeight = 1.45f - 0.90f * balance;

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float mono = 0.5f * (inputs[0][i] + inputs[1][i]);
            crossoverState += crossoverCoeff * (mono - crossoverState);
            const float low = crossoverState;
            const float high = mono - crossoverState;

            hornPhase += 6.2831853f * rateHz / sampleRate;
            rotorPhase += 6.2831853f * (rateHz * 0.82f) / sampleRate;
            if (hornPhase >= 6.2831853f)
                hornPhase -= 6.2831853f;
            if (rotorPhase >= 6.2831853f)
                rotorPhase -= 6.2831853f;
            const float hornSin = std::sin(hornPhase);
            const float rotorSin = std::sin(rotorPhase);

            const float hL = hornL.process(high, hornBase + hornWidth * hornSin);
            const float hR = hornR.process(high, hornBase - hornWidth * hornSin);
            const float rL = rotorL.process(low, rotorBase + rotorWidth * rotorSin);
            const float rR = rotorR.process(low, rotorBase - rotorWidth * rotorSin);
            const float hornAmp = 1.0f + 0.18f * depth * hornSin;
            const float rotorAmp = 1.0f + 0.08f * depth * rotorSin;
            const float wetL = 0.92f * (hL * hornAmp * hornWeight + rL * rotorAmp * rotorWeight);
            const float wetR = 0.92f * (hR * hornAmp * hornWeight + rR * rotorAmp * rotorWeight);
            outputs[0][i] = (inputs[0][i] * dryLevel + wetL * wetLevel) * levelComp;
            outputs[1][i] = (inputs[1][i] * dryLevel + wetR * wetLevel) * levelComp;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaVibePlugin)
};

Plugin* createPlugin()
{
    return new RotaVibePlugin();
}

END_NAMESPACE_DISTRHO
