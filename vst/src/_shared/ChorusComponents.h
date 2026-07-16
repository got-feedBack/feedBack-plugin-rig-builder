#ifndef RB_PEDAL_CHORUS_COMPONENTS_H
#define RB_PEDAL_CHORUS_COMPONENTS_H

#include <cmath>
#include <vector>

namespace rbmod {

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 6.28318530717958647692f;

static inline float clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float clamp01(float v)
{
    return clamp(v, 0.0f, 1.0f);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.7f);
}

static inline float reverseAudioTaper(float v)
{
    return 1.0f - std::pow(1.0f - clamp01(v), 1.8f);
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = clamp(hz, 5.0f, sr * 0.45f);
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

struct StereoInputPair
{
    float left;
    float right;
};

static inline StereoInputPair stereoPedalFeeds(float inL, float inR)
{
    const float eps = 1.0e-10f;
    const bool leftSilent = std::fabs(inL) <= eps;
    const bool rightSilent = std::fabs(inR) <= eps;

    if (!leftSilent && rightSilent)
        inR = inL;
    else if (leftSilent && !rightSilent)
        inL = inR;

    return { inL, inR };
}

class LowPass
{
    float y = 0.0f;
    float a = 0.0f;

public:
    void setHz(float hz, float sr)
    {
        a = onePoleCoeffHz(hz, sr);
    }

    void reset()
    {
        y = 0.0f;
    }

    float process(float x)
    {
        y += a * (x - y);
        return y;
    }
};

class HighPass
{
    float x1 = 0.0f;
    float y1 = 0.0f;
    float a = 0.0f;

public:
    void setHz(float hz, float sr)
    {
        const float dt = 1.0f / sr;
        const float rc = 1.0f / (2.0f * kPi * clamp(hz, 4.0f, sr * 0.40f));
        a = rc / (rc + dt);
    }

    void reset()
    {
        x1 = 0.0f;
        y1 = 0.0f;
    }

    float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = y;
        return y;
    }
};

class DelayBuffer
{
    std::vector<float> data;
    int writeIndex = 0;

public:
    void resizeForMs(float sampleRate, float maxMs)
    {
        int samples = (int)(sampleRate * maxMs * 0.001f) + 8;
        if (samples < 16)
            samples = 16;
        data.assign((size_t)samples, 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = 0.0f;
        writeIndex = 0;
    }

    float read(float delaySamples) const
    {
        const int size = (int)data.size();
        if (size <= 4)
            return 0.0f;
        delaySamples = clamp(delaySamples, 1.0f, (float)(size - 3));

        float pos = (float)writeIndex - delaySamples;
        while (pos < 0.0f)
            pos += (float)size;
        while (pos >= (float)size)
            pos -= (float)size;

        const int i0 = (int)std::floor(pos);
        const int i1 = (i0 + 1) % size;
        const float frac = pos - (float)i0;
        return data[(size_t)i0] + (data[(size_t)i1] - data[(size_t)i0]) * frac;
    }

    float readCubic(float delaySamples) const
    {
        const int size = (int)data.size();
        if (size <= 6)
            return 0.0f;
        delaySamples = clamp(delaySamples, 2.0f, (float)(size - 4));

        float pos = (float)writeIndex - delaySamples;
        while (pos < 0.0f)
            pos += (float)size;
        while (pos >= (float)size)
            pos -= (float)size;

        const int i0 = (int)std::floor(pos);
        const int im1 = (i0 + size - 1) % size;
        const int i1 = (i0 + 1) % size;
        const int i2 = (i0 + 2) % size;
        const float t = pos - (float)i0;
        const float xm1 = data[(size_t)im1];
        const float x0 = data[(size_t)i0];
        const float x1 = data[(size_t)i1];
        const float x2 = data[(size_t)i2];

        // Four-point Catmull-Rom interpolation preserves more high-frequency
        // comb depth than linear interpolation while the delay head moves.
        const float a = 0.5f * (-xm1 + 3.0f * x0 - 3.0f * x1 + x2);
        const float b = 0.5f * (2.0f * xm1 - 5.0f * x0 + 4.0f * x1 - x2);
        const float c = 0.5f * (-xm1 + x1);
        return ((a * t + b) * t + c) * t + x0;
    }

    void write(float x)
    {
        if (data.empty())
            return;
        data[(size_t)writeIndex] = x;
        ++writeIndex;
        if (writeIndex >= (int)data.size())
            writeIndex = 0;
    }
};

class NoiseSource
{
    unsigned int state = 0x4f1bbcd3u;

public:
    void seed(unsigned int s)
    {
        state = s ? s : 0x4f1bbcd3u;
    }

    float next()
    {
        state = state * 1664525u + 1013904223u;
        const unsigned int bits = (state >> 9) | 0x3f800000u;
        union { unsigned int u; float f; } pun = { bits };
        return (pun.f - 1.5f) * 2.0f;
    }
};

class BbdCompander
{
    float sampleRate = 48000.0f;
    float env = 0.0f;
    float a = 0.0f;

public:
    void setSampleRate(float sr, float tauMs = 22.0f)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        a = 1.0f - std::exp(-1.0f / (0.001f * tauMs * sampleRate));
        reset();
    }

    void reset()
    {
        env = 0.0f;
    }

    float process(float x, float amount)
    {
        env += a * (std::fabs(x) - env);
        const float gain = 1.0f / (1.0f + (0.35f + 1.05f * amount) * env);
        return softClip(x * gain * (1.05f + 0.18f * amount));
    }
};

class FirstOrderAllPass
{
    float x1 = 0.0f;
    float y1 = 0.0f;
    float sampleRate = 48000.0f;
    float capFarads = 0.01e-6f;

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
    }

    void setCap(float cap)
    {
        capFarads = cap > 10.0e-12f ? cap : 10.0e-12f;
    }

    void reset()
    {
        x1 = 0.0f;
        y1 = 0.0f;
    }

    float process(float x, float resistanceOhms)
    {
        const float r = clamp(resistanceOhms, 220.0f, 10000000.0f);
        const float fc = 1.0f / (2.0f * kPi * r * capFarads);
        const float w = std::tan(kPi * clamp(fc, 0.2f, sampleRate * 0.42f) / sampleRate);
        const float a = (1.0f - w) / (1.0f + w);
        const float y = -a * x + x1 + a * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

class LampLdrModel
{
    float sampleRate = 48000.0f;
    float lamp = 0.0f;
    float ldrLight = 0.0f;
    float lampUpSeconds = 0.020f;
    float lampDownSeconds = 0.075f;
    float ldrUpSeconds = 0.012f;
    float ldrDownSeconds = 0.090f;
    float lampUp = 0.0f;
    float lampDown = 0.0f;
    float ldrUp = 0.0f;
    float ldrDown = 0.0f;

    void updateCoefficients()
    {
        lampUp = 1.0f - std::exp(-1.0f / (lampUpSeconds * sampleRate));
        lampDown = 1.0f - std::exp(-1.0f / (lampDownSeconds * sampleRate));
        ldrUp = 1.0f - std::exp(-1.0f / (ldrUpSeconds * sampleRate));
        ldrDown = 1.0f - std::exp(-1.0f / (ldrDownSeconds * sampleRate));
    }

public:
    void setTimeConstants(float lampUpMs, float lampDownMs,
                          float ldrUpMs, float ldrDownMs)
    {
        lampUpSeconds = clamp(lampUpMs, 0.5f, 500.0f) * 0.001f;
        lampDownSeconds = clamp(lampDownMs, 0.5f, 500.0f) * 0.001f;
        ldrUpSeconds = clamp(ldrUpMs, 0.5f, 500.0f) * 0.001f;
        ldrDownSeconds = clamp(ldrDownMs, 0.5f, 500.0f) * 0.001f;
        updateCoefficients();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        updateCoefficients();
        reset();
    }

    void reset()
    {
        lamp = 0.0f;
        ldrLight = 0.0f;
    }

    float processLight(float drive)
    {
        const float target = smoothstep(clamp01(drive));
        lamp += ((target > lamp) ? lampUp : lampDown) * (target - lamp);
        const float optical = std::pow(clamp01(lamp), 0.72f);
        ldrLight += ((optical > ldrLight) ? ldrUp : ldrDown) * (optical - ldrLight);
        return ldrLight;
    }

    static float nsl7530Resistance(float light)
    {
        // NSL-7530 datasheet: >=6.7M dark, 6.7K-13.3K at 2fc,
        // about 500R at 100fc. The exponent keeps the useful sweep in
        // the 16K-33K Uni-Vibe build range before the lamp reaches full.
        const float l = clamp01(light);
        const float dark = 6700000.0f;
        const float bright = 500.0f;
        return dark * std::pow(bright / dark, std::pow(l, 0.58f));
    }
};

} // namespace rbmod

#endif // RB_PEDAL_CHORUS_COMPONENTS_H
