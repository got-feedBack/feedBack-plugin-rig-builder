/*
 * OctaveUp - clean +12 octave pedal for the game's Pedal_OctaveUp.
 *
 * the game exposes only Tone and Mix. The local Tentacle/Octron references
 * explain the two-path octave-up family, but the in-game pedal needs the wet
 * voice to read as a clean octave above the dry guitar. The wet path therefore
 * uses a fixed 2x phase-vocoder pitch shifter instead of rectifier waveshaping,
 * envelope tracking, or a filter-like phase doubler.
 */
#include "DistrhoPlugin.hpp"
#include "OctaveUpParams.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 6.28318530718f;
static constexpr int kFrameSize = 2048;
static constexpr int kOversample = 8;
static constexpr int kStepSize = kFrameSize / kOversample;
static constexpr int kFftBins = kFrameSize / 2 + 1;
static constexpr float kPitchRatio = 2.0f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = std::fmax(2.0f, std::fmin(hz, sr * 0.45f));
    return 1.0f - std::exp(-kTwoPi * hz / sr);
}

static void fft(std::vector<std::complex<float>>& a, bool inverse)
{
    const int n = (int)a.size();
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[(size_t)i], a[(size_t)j]);
    }

    for (int len = 2; len <= n; len <<= 1)
    {
        const float angle = (inverse ? kTwoPi : -kTwoPi) / (float)len;
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len)
        {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j)
            {
                const std::complex<float> u = a[(size_t)(i + j)];
                const std::complex<float> v = a[(size_t)(i + j + len / 2)] * w;
                a[(size_t)(i + j)] = u + v;
                a[(size_t)(i + j + len / 2)] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse)
    {
        const float invN = 1.0f / (float)n;
        for (auto& x : a)
            x *= invN;
    }
}

} // namespace

class PitchUpShifter
{
    float sampleRate = 48000.0f;
    int rover = kFrameSize - kStepSize;

    std::vector<float> inFifo;
    std::vector<float> outFifo;
    std::vector<float> outputAccum;
    std::vector<float> lastPhase;
    std::vector<float> sumPhase;
    std::vector<float> anaMagn;
    std::vector<float> anaFreq;
    std::vector<float> synMagn;
    std::vector<float> synFreq;
    std::vector<std::complex<float>> fftBuf;

    void processFrame()
    {
        const float freqPerBin = sampleRate / (float)kFrameSize;
        const float expected = kTwoPi * (float)kStepSize / (float)kFrameSize;

        for (int k = 0; k < kFrameSize; ++k)
        {
            const float window = 0.5f - 0.5f * std::cos(kTwoPi * (float)k / (float)kFrameSize);
            fftBuf[(size_t)k] = std::complex<float>(inFifo[(size_t)k] * window, 0.0f);
        }

        fft(fftBuf, false);

        for (int k = 0; k < kFftBins; ++k)
        {
            const float real = fftBuf[(size_t)k].real();
            const float imag = fftBuf[(size_t)k].imag();
            const float magn = std::sqrt(real * real + imag * imag);
            const float phase = std::atan2(imag, real);

            float delta = phase - lastPhase[(size_t)k];
            lastPhase[(size_t)k] = phase;
            delta -= (float)k * expected;

            while (delta < -kPi)
                delta += kTwoPi;
            while (delta > kPi)
                delta -= kTwoPi;

            const float trueFreq = ((float)k + delta * (float)kOversample / kTwoPi) * freqPerBin;
            anaMagn[(size_t)k] = magn;
            anaFreq[(size_t)k] = trueFreq;
        }

        std::fill(synMagn.begin(), synMagn.end(), 0.0f);
        std::fill(synFreq.begin(), synFreq.end(), 0.0f);

        for (int k = 0; k < kFftBins; ++k)
        {
            const int index = (int)((float)k * kPitchRatio + 0.5f);
            if (index < kFftBins)
            {
                synMagn[(size_t)index] += anaMagn[(size_t)k];
                synFreq[(size_t)index] = anaFreq[(size_t)k] * kPitchRatio;
            }
        }

        std::fill(fftBuf.begin(), fftBuf.end(), std::complex<float>(0.0f, 0.0f));

        for (int k = 0; k < kFftBins; ++k)
        {
            const float magn = synMagn[(size_t)k];
            float deltaFreq = synFreq[(size_t)k] - (float)k * freqPerBin;
            deltaFreq /= freqPerBin;
            const float deltaPhase = (deltaFreq * kTwoPi / (float)kOversample)
                                   + (float)k * expected;
            sumPhase[(size_t)k] += deltaPhase;

            const float phase = sumPhase[(size_t)k];
            const std::complex<float> bin(magn * std::cos(phase), magn * std::sin(phase));
            fftBuf[(size_t)k] = bin;
            if (k > 0 && k < kFrameSize / 2)
                fftBuf[(size_t)(kFrameSize - k)] = std::conj(bin);
        }

        fft(fftBuf, true);

        const float olaNorm = 1.0f / (0.375f * (float)kOversample);
        for (int k = 0; k < kFrameSize; ++k)
        {
            const float window = 0.5f - 0.5f * std::cos(kTwoPi * (float)k / (float)kFrameSize);
            outputAccum[(size_t)k] += fftBuf[(size_t)k].real() * window * olaNorm;
        }

        for (int k = 0; k < kStepSize; ++k)
            outFifo[(size_t)k] = outputAccum[(size_t)k];

        for (int k = 0; k < kFrameSize - kStepSize; ++k)
            outputAccum[(size_t)k] = outputAccum[(size_t)(k + kStepSize)];
        std::fill(outputAccum.begin() + (kFrameSize - kStepSize), outputAccum.end(), 0.0f);

        for (int k = 0; k < kFrameSize - kStepSize; ++k)
            inFifo[(size_t)k] = inFifo[(size_t)(k + kStepSize)];
    }

public:
    void reset(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        rover = kFrameSize - kStepSize;

        inFifo.assign(kFrameSize, 0.0f);
        outFifo.assign(kFrameSize, 0.0f);
        outputAccum.assign(kFrameSize * 2, 0.0f);
        lastPhase.assign(kFftBins, 0.0f);
        sumPhase.assign(kFftBins, 0.0f);
        anaMagn.assign(kFftBins, 0.0f);
        anaFreq.assign(kFftBins, 0.0f);
        synMagn.assign(kFftBins, 0.0f);
        synFreq.assign(kFftBins, 0.0f);
        fftBuf.assign(kFrameSize, std::complex<float>(0.0f, 0.0f));
    }

    float process(float in)
    {
        if (inFifo.empty())
            reset(sampleRate);

        inFifo[(size_t)rover] = in;
        const float out = outFifo[(size_t)(rover - (kFrameSize - kStepSize))];

        ++rover;
        if (rover >= kFrameSize)
        {
            rover = kFrameSize - kStepSize;
            processFrame();
        }

        return out;
    }
};

class OctaveUpCore
{
    float sampleRate = 48000.0f;
    float tone = kOctaveUpDef[kTone];
    float mix = kOctaveUpDef[kMix];

    PitchUpShifter shifter;

    float inputHpX1 = 0.0f;
    float inputHpY1 = 0.0f;
    float wetHpX1 = 0.0f;
    float wetHpY1 = 0.0f;
    float wetToneY = 0.0f;
    float wetAirY = 0.0f;
    float dryToneY = 0.0f;

    float inputHpA = 0.0f;
    float wetHpA = 0.0f;
    float wetToneA = 0.0f;
    float wetAirA = 0.0f;
    float dryToneA = 0.0f;

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;
        const float inputHpRc = 1.0f / (kTwoPi * 38.0f);
        inputHpA = inputHpRc / (inputHpRc + dt);

        const float wetHpRc = 1.0f / (kTwoPi * 48.0f);
        wetHpA = wetHpRc / (wetHpRc + dt);

        const float t = tone * tone * (3.0f - 2.0f * tone);
        wetToneA = onePoleCoeffHz(6200.0f + 7600.0f * t, sampleRate);
        wetAirA = onePoleCoeffHz(2600.0f + 3600.0f * t, sampleRate);
        dryToneA = onePoleCoeffHz(13000.0f, sampleRate);
    }

    float highPass(float x, float& x1, float& y1, float a)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = y;
        return y;
    }

    float lowPass(float x, float& z, float a)
    {
        z += a * (x - z);
        return z;
    }

public:
    void reset()
    {
        shifter.reset(sampleRate);
        inputHpX1 = inputHpY1 = wetHpX1 = wetHpY1 = 0.0f;
        wetToneY = wetAirY = dryToneY = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setTone(float v)
    {
        tone = clamp01(v);
        updateFilters();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
    }

    float process(float in)
    {
        const float dry = lowPass(in, dryToneY, dryToneA);
        const float shiftedIn = highPass(in, inputHpX1, inputHpY1, inputHpA);

        float wet = shifter.process(shiftedIn);
        wet = highPass(wet, wetHpX1, wetHpY1, wetHpA);
        wet = lowPass(wet, wetToneY, wetToneA);

        const float airBase = lowPass(wet, wetAirY, wetAirA);
        wet = airBase + (wet - airBase) * (0.45f + 0.50f * tone);

        const float m = mix;
        const float dryLevel = 1.0f - m;
        const float wetLevel = 0.96f * m;
        return (dry * dryLevel + wet * wetLevel) * 0.98f;
    }
};

class OctaveUpPlugin : public Plugin
{
    OctaveUpCore left;
    OctaveUpCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    OctaveUpPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kOctaveUpDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "OctaveUp"; }
    const char* getDescription() const override { return "Clean octave-up pedal"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 8); }
    int64_t getUniqueId() const override { return d_cconst('O', 'c', 'u', 'p'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kOctaveUpNames[index];
        parameter.symbol = kOctaveUpSymbols[index];
        parameter.ranges.min = kOctaveUpMin[index];
        parameter.ranges.max = kOctaveUpMax[index];
        parameter.ranges.def = kOctaveUpDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctaveUpPlugin)
};

Plugin* createPlugin()
{
    return new OctaveUpPlugin();
}

END_NAMESPACE_DISTRHO
