/* StereoAnalogVibe - reference-bounded optical phase vibe.
 *
 * This rack has no exact local schematic, so it reuses the measured Uni-Vibe
 * topology: lamp/LDR inertia and four unequal RC all-pass cells. Waveform
 * changes the oscillator shape without introducing the abrupt square-wave
 * switching used by the old generic model.
 */
#include "DistrhoPlugin.hpp"
#include "StereoAnalogVibeParams.h"
#include "../../_shared/ChorusComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class OpticalVibeChannel
{
    float sampleRate = 48000.0f;
    rbmod::LampLdrModel lamp;
    rbmod::FirstOrderAllPass stages[4];
    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass outputLp;

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        lamp.setTimeConstants(5.0f, 18.0f, 3.0f, 22.0f);
        lamp.setSampleRate(sampleRate);
        stages[0].setCap(15.0e-9f);
        stages[1].setCap(220.0e-9f);
        stages[2].setCap(470.0e-12f);
        stages[3].setCap(4.7e-9f);
        for (int i = 0; i < 4; ++i)
            stages[i].setSampleRate(sampleRate);
        inputHp.setHz(28.0f, sampleRate);
        inputLp.setHz(13000.0f, sampleRate);
        outputLp.setHz(11000.0f, sampleRate);
        reset();
    }

    void reset()
    {
        lamp.reset();
        inputHp.reset();
        inputLp.reset();
        outputLp.reset();
        for (int i = 0; i < 4; ++i)
            stages[i].reset();
    }

    float process(float in, float phase, float waveform, float mix)
    {
        const float sine = std::sin(rbmod::kTwoPi * phase);
        const float softSquare = std::tanh(1.55f * sine) / std::tanh(1.55f);
        const float shaped = sine + rbmod::clamp01(waveform) * 0.42f * (softSquare - sine);
        const float lfo = 0.5f + 0.5f * shaped;
        const float amount = std::pow(rbmod::clamp01(mix), 1.15f);
        const float drive = rbmod::clamp01(0.10f + (0.12f + 0.72f * amount) * lfo);
        const float light = lamp.processLight(drive);
        const float ldrR = rbmod::LampLdrModel::nsl7530Resistance(light);

        float wet = inputLp.process(inputHp.process(in));
        const float spread[4] = { 1.00f, 0.84f, 1.22f, 0.94f };
        for (int i = 0; i < 4; ++i)
            wet = stages[i].process(wet, 4700.0f + 0.35f * ldrR * spread[i]);
        wet = outputLp.process(wet);

        // The reference Uni-Vibe chorus sum uses near-equal dry/all-pass
        // branches. Interpolate toward that sum instead of crossfading through
        // a deep cancellation at noon and becoming almost static at full wet.
        const float chorus = 0.52f * in + 0.52f * wet;
        const float effectAmount = 0.82f * amount;
        const float levelComp = std::pow(10.0f, (4.37f * amount) / 20.0f);
        return (in + effectAmount * (chorus - in)) * levelComp;
    }
};

class StereoAnalogVibePlugin : public Plugin
{
    OpticalVibeChannel left;
    OpticalVibeChannel right;
    float sampleRate = 48000.0f;
    float phase = 0.0f;
    float speedNow = kStereoAnalogVibeDef[kSpeed];
    float waveNow = kStereoAnalogVibeDef[kWaveform];
    float mixNow = kStereoAnalogVibeDef[kMix];
    float smoothA = 1.0f;
    float params[kParamCount];

    void prepare(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        smoothA = 1.0f - std::exp(-1.0f / (0.012f * sampleRate));
        left.setSampleRate(sampleRate);
        right.setSampleRate(sampleRate);
        phase = 0.0f;
        speedNow = params[kSpeed];
        waveNow = params[kWaveform];
        mixNow = params[kMix];
    }

public:
    StereoAnalogVibePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kStereoAnalogVibeDef[i];
        prepare((float)getSampleRate());
    }

protected:
    const char* getLabel() const override { return "StereoAnalogVibe"; }
    const char* getDescription() const override { return "stereo optical phase vibe"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'V', 'i', '1'); }

    void initParameter(uint32_t i, Parameter& p) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        p.hints = kParameterIsAutomatable;
        p.name = kStereoAnalogVibeNames[i];
        p.symbol = kStereoAnalogVibeSymbols[i];
        p.ranges.min = kStereoAnalogVibeMin[i];
        p.ranges.max = kStereoAnalogVibeMax[i];
        p.ranges.def = kStereoAnalogVibeDef[i];
    }

    float getParameterValue(uint32_t i) const override
    {
        return i < (uint32_t)kParamCount ? params[i] : 0.0f;
    }

    void setParameterValue(uint32_t i, float v) override
    {
        if (i < (uint32_t)kParamCount)
            params[i] = rbmod::clamp01(v);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        prepare((float)newSampleRate);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            speedNow += smoothA * (params[kSpeed] - speedNow);
            waveNow += smoothA * (params[kWaveform] - waveNow);
            mixNow += smoothA * (params[kMix] - mixNow);
            phase += (0.10f + 9.90f * speedNow) / sampleRate;
            if (phase >= 1.0f)
                phase -= std::floor(phase);
            float rightPhase = phase + 0.04f;
            if (rightPhase >= 1.0f)
                rightPhase -= 1.0f;
            outputs[0][i] = left.process(inputs[0][i], phase, waveNow, mixNow);
            outputs[1][i] = right.process(inputs[1][i], rightPhase, waveNow, mixNow);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoAnalogVibePlugin)
};

Plugin* createPlugin()
{
    return new StereoAnalogVibePlugin();
}

END_NAMESPACE_DISTRHO
