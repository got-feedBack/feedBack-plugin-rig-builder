/*
 * BassEmulator - guitar-to-bass emulator for Rocksmith Pedal_BassEmulator.
 *
 * No exact schematic is available. The Rocksmith artwork exposes Body and
 * Tone, so this model uses a fixed -12 semitone pitch shifter and then voices
 * only that shifted signal as a dark bass. Keeping the octave shift fixed
 * avoids the false low notes that pitch trackers can produce on transients.
 */
#include "DistrhoPlugin.hpp"
#include "BassEmulatorParams.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float onePoleCoeff(float hz, float sr)
{
    hz = std::fmax(10.0f, std::fmin(hz, sr * 0.45f));
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

} // namespace

class BassEmulatorCore
{
    static constexpr int kBufferSize = 32768;

    float sampleRate = 48000.0f;
    float body = kBassEmulatorDef[kBody];
    float tone = kBassEmulatorDef[kTone];

    float buffer[kBufferSize] {};
    int writeIndex = 0;
    float shiftPhase = 0.0f;
    float windowSamples = 2400.0f;
    float spliceSamples = 256.0f;
    float splicePos = 256.0f;
    float shiftedTone1 = 0.0f;
    float shiftedTone2 = 0.0f;
    float bodyLow = 0.0f;
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;

    float toneA = 0.0f;
    float bodyA = 0.0f;
    float hpA = 0.0f;

    void update()
    {
        const float windowMs = 34.0f + 18.0f * (1.0f - body);
        windowSamples = sampleRate * windowMs * 0.001f;
        windowSamples = std::fmax(512.0f, std::fmin(windowSamples, (float)kBufferSize * 0.45f));
        spliceSamples = std::fmax(96.0f, std::fmin(sampleRate * 0.0045f, windowSamples * 0.22f));
        while (shiftPhase >= windowSamples)
            shiftPhase -= windowSamples;
        if (splicePos > spliceSamples)
            splicePos = spliceSamples;

        toneA = onePoleCoeff(190.0f + 980.0f * tone, sampleRate);
        bodyA = onePoleCoeff(95.0f + 215.0f * body, sampleRate);

        const float dt = 1.0f / sampleRate;
        const float hpHz = 30.0f;
        const float hpRc = 1.0f / (2.0f * kPi * hpHz);
        hpA = hpRc / (hpRc + dt);
    }

    float readDelay(float delay) const
    {
        float readPos = (float)writeIndex - delay;
        while (readPos < 0.0f)
            readPos += (float)kBufferSize;
        while (readPos >= (float)kBufferSize)
            readPos -= (float)kBufferSize;

        const int i0 = (int)readPos;
        const int i1 = (i0 + 1) % kBufferSize;
        const float frac = readPos - (float)i0;
        return buffer[i0] + frac * (buffer[i1] - buffer[i0]);
    }

    float octaveDown(float in)
    {
        buffer[writeIndex] = in;

        // Ratio 0.5: the read delay grows by 0.5 samples per output sample,
        // so the read head advances at half speed and produces exactly -12 st.
        shiftPhase += 0.5f;
        while (shiftPhase >= windowSamples)
        {
            shiftPhase -= windowSamples;
            splicePos = 0.0f;
        }

        const float newVoice = readDelay(shiftPhase + 2.0f);
        float shifted = newVoice;
        if (splicePos < spliceSamples)
        {
            const float oldVoice = readDelay(shiftPhase + windowSamples + 2.0f);
            const float t = splicePos / spliceSamples;
            const float xfade = 0.5f - 0.5f * std::cos(kPi * t);
            shifted = oldVoice * (1.0f - xfade) + newVoice * xfade;
            splicePos += 1.0f;
        }

        if (++writeIndex >= kBufferSize)
            writeIndex = 0;
        return shifted;
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

public:
    void reset()
    {
        std::memset(buffer, 0, sizeof(buffer));
        writeIndex = 0;
        shiftPhase = 0.0f;
        splicePos = spliceSamples;
        shiftedTone1 = shiftedTone2 = bodyLow = 0.0f;
        hpX1 = hpY1 = 0.0f;
        update();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setBody(float v)
    {
        body = clamp01(v);
        update();
    }

    void setTone(float v)
    {
        tone = clamp01(v);
        update();
    }

    float process(float in)
    {
        const float shifted = octaveDown(in);
        shiftedTone1 += toneA * (shifted - shiftedTone1);
        shiftedTone2 += toneA * (shiftedTone1 - shiftedTone2);
        bodyLow += bodyA * (shiftedTone2 - bodyLow);

        float wet = shiftedTone2 * (0.66f + 0.24f * tone)
                  + bodyLow * (0.12f + 0.24f * body);
        wet = highPass(wet);

        return wet * (1.02f - 0.08f * body);
    }
};

class BassEmulatorPlugin : public Plugin
{
    BassEmulatorCore left;
    BassEmulatorCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setBody(params[kBody]);
        right.setBody(params[kBody]);
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
    }

public:
    BassEmulatorPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBassEmulatorDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BassEmulator"; }
    const char* getDescription() const override { return "guitar to bass octave emulator"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 's', 'E', 'm'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBassEmulatorNames[index];
        parameter.symbol = kBassEmulatorSymbols[index];
        parameter.ranges.min = kBassEmulatorMin[index];
        parameter.ranges.max = kBassEmulatorMax[index];
        parameter.ranges.def = kBassEmulatorDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassEmulatorPlugin)
};

Plugin* createPlugin()
{
    return new BassEmulatorPlugin();
}

END_NAMESPACE_DISTRHO
