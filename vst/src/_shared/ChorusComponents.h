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

// One transistor/LDR phase stage from the Uni-Vibe signal path. This follows
// the component-domain transfer used by Guitarix/Rakarrack: collector,
// emitter-load and collector-output networks are solved separately, then the
// BJT stage combines them. It is intentionally not an ideal all-pass filter.
class TransistorVibeStage
{
    struct OnePole
    {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float n0 = 0.0f;
        float n1 = 0.0f;
        float d1 = 0.0f;

        void reset() { x1 = y1 = 0.0f; }

        float process(float x)
        {
            const float y = x * n0 + x1 * n1 - y1 * d1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    float sampleRate = 48000.0f;
    float cap = 15.0e-9f;
    float oldCollector = 0.0f;
    float emitterFeedback = 0.0f;
    unsigned int updateCounter = 0;
    OnePole vc;
    OnePole vcvo;
    OnePole ecvc;
    OnePole vevo;

    static float bjtShape(float x)
    {
        float vin = clamp(7.5f * (1.0f + x), 0.0f, 15.0f);
        const float vbe = 0.8f - 0.8f / (vin + 1.0f);
        return (vin - vbe) * 0.1333333333f - 0.90588f;
    }

    static void setPole(OnePole& p, float n1s, float n0s,
                        float d1s, float d0s)
    {
        const float norm = 1.0f / (d1s + d0s);
        p.n1 = norm * (n0s - n1s);
        p.n0 = norm * (n1s + n0s);
        p.d1 = norm * (d0s - d1s);
    }

    void updateCoefficients(float ldrOhms)
    {
        const float r1 = 4700.0f;
        const float ldr = clamp(ldrOhms, 10000.0f, 400000.0f);
        const float rv = 4700.0f + ldr;
        const float c2 = 1.0e-6f;
        const float beta = 150.0f;
        const float transistorGain = -beta / (beta + 1.0f);
        const float k = 2.0f * sampleRate;
        const float rSum = r1 + rv;
        const float cSum = c2 + cap;

        const float ed1 = k * rSum * cap;
        const float ed0 = 1.0f + cap / c2;
        const float en1 = k * r1 * cap;
        setPole(vevo, en1, 1.0f, ed1, ed0);

        const float cd1 = ed1;
        const float cd0 = 1.0f + cap / c2;
        const float cn1 = k * transistorGain * rv * cap;
        const float cn0 = transistorGain * (1.0f + cap / c2);
        setPole(vc, cn1, cn0, cd1, cd0);

        const float ecd1 = k * cd1 * c2 / cSum;
        const float ecn1 = k * transistorGain * r1 * cd1 * c2 / (rv * cSum);
        setPole(ecvc, ecn1, 0.0f, ecd1, 1.0f);

        const float od1 = k * rv * c2;
        const float od0 = 1.0f + c2 / cap;
        setPole(vcvo, od1, 1.0f, od1, od0);
        emitterFeedback = 25.0f / ldr;
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
    }

    void setCap(float farads)
    {
        cap = farads > 10.0e-12f ? farads : 10.0e-12f;
    }

    void reset()
    {
        vc.reset();
        vcvo.reset();
        ecvc.reset();
        vevo.reset();
        oldCollector = 0.0f;
        emitterFeedback = 0.0f;
        updateCounter = 0;
    }

    float process(float input, float ldrOhms)
    {
        if ((updateCounter++ & 3u) == 0u)
            updateCoefficients(ldrOhms);
        const float collector = ecvc.process(input)
                              + vc.process(input + emitterFeedback * oldCollector);
        const float collectorOut = vcvo.process(collector);
        oldCollector = collectorOut;
        const float emitterOut = vevo.process(input);
        return bjtShape(collectorOut + emitterOut);
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
