#ifndef STUDIO_REVERB_MODELS_HPP
#define STUDIO_REVERB_MODELS_HPP

#include <cmath>
#include <cstring>

static inline float rbClip01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float rbClamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float rbFlush(float v)
{
    return std::fabs(v) < 1.0e-18f ? 0.0f : v;
}

static inline float rbSoftLimit(float v)
{
    return std::tanh(v);
}

// Guarded loop limiter: LINEAR below +/-0.9 (keeps the reverb tail clean, as the
// real 12-bit-linear / passive-plate units are), soft ceiling only near +/-1 as a
// safety net. Replaces the plain tanh that was coloring the recirculating tail.
static inline float rbLoopLimit(float v)
{
    const float a = v < 0.0f ? -v : v;
    if (a <= 0.9f) return v;
    const float s = v < 0.0f ? -1.0f : 1.0f;
    return s * (0.9f + 0.1f * std::tanh((a - 0.9f) * 10.0f));
}

// 8x8 normalized Hadamard (energy-preserving FDN mixing matrix).
static inline void rbHadamard8(float* v)
{
    float a0 = v[0] + v[1], a1 = v[0] - v[1];
    float a2 = v[2] + v[3], a3 = v[2] - v[3];
    float a4 = v[4] + v[5], a5 = v[4] - v[5];
    float a6 = v[6] + v[7], a7 = v[6] - v[7];
    float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
    float b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
    const float s = 0.35355339059f;
    v[0] = (b0 + b4) * s; v[1] = (b1 + b5) * s; v[2] = (b2 + b6) * s; v[3] = (b3 + b7) * s;
    v[4] = (b0 - b4) * s; v[5] = (b1 - b5) * s; v[6] = (b2 - b6) * s; v[7] = (b3 - b7) * s;
}

class RbOnePoleLp
{
    float a = 0.1f;
    float z = 0.0f;

public:
    void setCutoff(float fs, float hz)
    {
        hz = rbClamp(hz, 5.0f, fs * 0.45f);
        a = 1.0f - std::exp(-6.28318530718f * hz / fs);
        a = rbClamp(a, 0.00001f, 1.0f);
    }

    void clear() { z = 0.0f; }

    inline float process(float x)
    {
        z += a * (x - z);
        z = rbFlush(z);
        return z;
    }
};

class RbDcBlock
{
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void clear()
    {
        x1 = 0.0f;
        y1 = 0.0f;
    }

    inline float process(float x)
    {
        const float y = x - x1 + 0.995f * y1;
        x1 = x;
        y1 = rbFlush(y);
        return y1;
    }
};

template<int MaxSamples>
class RbDelay
{
    float buf[MaxSamples];
    int w = 0;

public:
    RbDelay() { clear(); }

    void clear()
    {
        std::memset(buf, 0, sizeof(buf));
        w = 0;
    }

    inline float read(float delaySamples) const
    {
        delaySamples = rbClamp(delaySamples, 1.0f, (float)(MaxSamples - 3));
        float rp = (float)w - delaySamples;
        while (rp < 0.0f)
            rp += (float)MaxSamples;
        int i0 = (int)rp;
        const float frac = rp - (float)i0;
        int i1 = i0 + 1;
        if (i1 >= MaxSamples)
            i1 = 0;
        return buf[i0] + frac * (buf[i1] - buf[i0]);
    }

    inline void write(float x)
    {
        buf[w] = rbFlush(x);
        if (++w >= MaxSamples)
            w = 0;
    }
};

template<int MaxSamples>
class RbAllpass
{
    RbDelay<MaxSamples> d;
    float delay = 100.0f;
    float g = 0.55f;

public:
    void clear() { d.clear(); }

    void set(float delaySamples, float feedback)
    {
        delay = rbClamp(delaySamples, 1.0f, (float)(MaxSamples - 3));
        g = rbClamp(feedback, -0.85f, 0.85f);
    }

    inline float process(float x)
    {
        const float b = d.read(delay);
        const float y = b - g * x;
        d.write(x + g * b);
        return rbFlush(y);
    }
};

class Emt140PlateCore
{
    static const int kLines = 8;   // dense plate: 8-line Hadamard FDN (was a too-sparse 4)

    float fs = 48000.0f;
    float wetMax = 0.35f;
    float dryGain = 1.0f;
    float wetGain = 0.0f;
    float width = 0.8f;
    float inputGain = 0.18f;
    float fbGain[kLines] = { 0.85f };
    float lineDelay[kLines] = { 0.0f };
    float phase[kLines] = { 0.0f };
    float inc[kLines] = { 0.0f };
    float modDepth = 0.0f;

    RbDcBlock inDc;
    RbOnePoleLp inBand;
    RbOnePoleLp outBandL;
    RbOnePoleLp outBandR;
    RbOnePoleLp lineDamp[kLines];
    RbDelay<32768> line[kLines];
    RbAllpass<8192> diffuse[7];   // more input diffusion for echo density
    RbAllpass<8192> postL[2];
    RbAllpass<8192> postR[2];

    static int samplesForMs(float sampleRate, float ms)
    {
        return (int)(sampleRate * ms * 0.001f + 0.5f);
    }

    void configureDelays()
    {
        const float lineMs[kLines] = { 18.3f, 22.7f, 27.9f, 33.1f, 38.9f, 44.3f, 51.7f, 58.9f };
        for (int i = 0; i < kLines; ++i)
            lineDelay[i] = (float)samplesForMs(fs, lineMs[i]);

        const float diffMs[7] = { 2.7f, 3.9f, 5.7f, 7.9f, 10.3f, 13.1f, 16.7f };
        for (int i = 0; i < 7; ++i)
            diffuse[i].set((float)samplesForMs(fs, diffMs[i]), 0.60f + 0.028f * (float)i);

        postL[0].set((float)samplesForMs(fs, 8.9f), 0.54f);
        postL[1].set((float)samplesForMs(fs, 14.7f), 0.48f);
        postR[0].set((float)samplesForMs(fs, 9.7f), 0.55f);
        postR[1].set((float)samplesForMs(fs, 15.9f), 0.47f);

        // SUBTLE modulation to de-metallize the FDN modes (a digital plate rings
        // metallically without it — the same reason Dattorro/Lexicon modulate;
        // kept small so it doesn't sound chorused, unlike the Lexicon cores).
        const float rates[kLines] = { 0.11f, 0.13f, 0.17f, 0.19f, 0.23f, 0.29f, 0.31f, 0.37f };
        for (int i = 0; i < kLines; ++i) {
            phase[i] = 0.41f * (float)i;
            inc[i] = 6.28318530718f * rates[i] / fs;
        }
        modDepth = 16.0f * (fs / 48000.0f);
    }

public:
    void setWetMax(float w) { wetMax = rbClip01(w); }

    void setSampleRate(float sampleRate)
    {
        fs = sampleRate > 1000.0f ? sampleRate : 48000.0f;
        configureDelays();
        clear();
    }

    void clear()
    {
        inDc.clear();
        inBand.clear();
        outBandL.clear();
        outBandR.clear();
        for (int i = 0; i < kLines; ++i) {
            line[i].clear();
            lineDamp[i].clear();
        }
        for (int i = 0; i < 7; ++i)
            diffuse[i].clear();
        for (int i = 0; i < 2; ++i) {
            postL[i].clear();
            postR[i].clear();
        }
    }

    void setParams(float time, float tone, float depth, float mix)
    {
        time = rbClip01(time);
        tone = rbClip01(tone);
        depth = rbClip01(depth);
        mix = rbClip01(mix);

        const float t60 = 0.5f + std::pow(time, 1.2f) * 4.3f;             // 0.5-4.8 s (real EMT damper)
        const float dampCut = 4500.0f + std::pow(tone, 1.2f) * 12000.0f;  // brighter, shimmery tail
        const float outCut = 8000.0f + tone * 8000.0f;                    // higher floor -> plate top end
        inBand.setCutoff(fs, 14500.0f + tone * 4500.0f);
        outBandL.setCutoff(fs, outCut);
        outBandR.setCutoff(fs, outCut);

        for (int i = 0; i < kLines; ++i) {
            const float delaySec = lineDelay[i] / fs;
            fbGain[i] = rbClamp(std::pow(10.0f, -3.0f * delaySec / t60), 0.45f, 0.988f);
            lineDamp[i].setCutoff(fs, dampCut * (0.88f + 0.03f * (float)i));
        }

        const float diffFb = 0.56f + depth * 0.18f;
        const float diffMs[7] = { 2.7f, 3.9f, 5.7f, 7.9f, 10.3f, 13.1f, 16.7f };
        for (int i = 0; i < 7; ++i)
            diffuse[i].set((float)samplesForMs(fs, diffMs[i]), rbClamp(diffFb + 0.02f * (float)i, 0.45f, 0.80f));

        width = 0.42f + depth * 0.58f;
        inputGain = 0.135f + 0.05f * depth;

        const float m = mix * wetMax;
        const float a = m * 1.57079632679f;
        dryGain = std::cos(a);
        wetGain = std::sin(a) * 0.72f;
    }

    inline void process(float xL, float xR, float& outL, float& outR)
    {
        float x = 0.5f * (xL + xR);
        x = inDc.process(x);
        x = inBand.process(x);
        x = rbSoftLimit(x * 1.18f) * 0.86f;   // input drive (fine on the way in)

        for (int i = 0; i < 7; ++i)
            x = diffuse[i].process(x);

        float y[kLines];
        float v[kLines];
        for (int i = 0; i < kLines; ++i) {
            phase[i] += inc[i];
            if (phase[i] > 6.28318530718f)
                phase[i] -= 6.28318530718f;
            const float wander = (std::sin(phase[i]) + 0.6f * std::sin(phase[i] * 1.67f + 0.8f * (float)i)) * modDepth;
            y[i] = line[i].read(lineDelay[i] + wander);
            v[i] = lineDamp[i].process(y[i]);
        }

        rbHadamard8(v);

        const float signs[kLines] = { 1.00f, -0.82f, 0.70f, -0.60f, 0.52f, -0.44f, 0.36f, -0.30f };
        for (int i = 0; i < kLines; ++i)
            line[i].write(rbLoopLimit(inputGain * signs[i] * x + fbGain[i] * v[i]));

        float wetL = 0.42f * y[0] - 0.30f * y[2] + 0.26f * y[4] - 0.20f * y[6] + 0.18f * y[1];
        float wetR = -0.24f * y[1] + 0.34f * y[3] - 0.28f * y[5] + 0.22f * y[7] - 0.16f * y[0];

        wetL = postL[1].process(postL[0].process(wetL));
        wetR = postR[1].process(postR[0].process(wetR));
        wetL = outBandL.process(wetL);
        wetR = outBandR.process(wetR);

        const float mid = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR) * width;
        wetL = mid + side;
        wetR = mid - side;

        outL = xL * dryGain + wetL * wetGain;
        outR = xR * dryGain + wetR * wetGain;
    }
};

class Lexicon224VerbCore
{
    static const int kLines = 8;

    float fs = 48000.0f;
    float wetMax = 0.32f;
    float dryGain = 1.0f;
    float wetGain = 0.0f;
    float width = 0.9f;
    float inputGain = 0.12f;
    float modDepth = 0.0f;
    float phase[kLines] = { 0.0f };
    float inc[kLines] = { 0.0f };
    float baseDelay[kLines] = { 0.0f };
    float fbGain[kLines] = { 0.0f };

    RbDcBlock dcL;
    RbDcBlock dcR;
    RbOnePoleLp inputBand;
    RbOnePoleLp inputBand2;
    RbOnePoleLp outputBandL;
    RbOnePoleLp outputBandR;
    RbOnePoleLp outputBand2L;
    RbOnePoleLp outputBand2R;
    RbOnePoleLp lineDamp[kLines];
    RbDelay<65536> line[kLines];
    RbDelay<32768> early;
    RbDelay<16384> preDelay;
    float preSamples = 0.0f;
    RbAllpass<16384> diff[6];

    static int samplesForMs(float sampleRate, float ms)
    {
        return (int)(sampleRate * ms * 0.001f + 0.5f);
    }

    void configureDelays()
    {
        const float lineMs[kLines] = { 29.7f, 37.1f, 41.9f, 53.3f, 67.9f, 83.9f, 97.3f, 121.7f };
        for (int i = 0; i < kLines; ++i)
            baseDelay[i] = (float)samplesForMs(fs, lineMs[i]);

        const float diffMs[6] = { 7.1f, 11.3f, 16.7f, 23.9f, 31.1f, 43.7f };
        for (int i = 0; i < 6; ++i)
            diff[i].set((float)samplesForMs(fs, diffMs[i]), 0.58f + 0.02f * (float)(i & 1));

        const float rates[kLines] = { 0.071f, 0.083f, 0.097f, 0.113f, 0.137f, 0.159f, 0.181f, 0.211f };
        for (int i = 0; i < kLines; ++i) {
            phase[i] = 0.47f * (float)i;
            inc[i] = 6.28318530718f * rates[i] / fs;
        }
    }

    static void hadamard8(float* v)
    {
        float a0 = v[0] + v[1], a1 = v[0] - v[1];
        float a2 = v[2] + v[3], a3 = v[2] - v[3];
        float a4 = v[4] + v[5], a5 = v[4] - v[5];
        float a6 = v[6] + v[7], a7 = v[6] - v[7];

        float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
        float b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;

        const float s = 0.35355339059f;
        v[0] = (b0 + b4) * s;
        v[1] = (b1 + b5) * s;
        v[2] = (b2 + b6) * s;
        v[3] = (b3 + b7) * s;
        v[4] = (b0 - b4) * s;
        v[5] = (b1 - b5) * s;
        v[6] = (b2 - b6) * s;
        v[7] = (b3 - b7) * s;
    }

public:
    void setWetMax(float w) { wetMax = rbClip01(w); }

    void setSampleRate(float sampleRate)
    {
        fs = sampleRate > 1000.0f ? sampleRate : 48000.0f;
        configureDelays();
        clear();
    }

    void clear()
    {
        dcL.clear();
        dcR.clear();
        inputBand.clear();
        inputBand2.clear();
        outputBandL.clear();
        outputBandR.clear();
        outputBand2L.clear();
        outputBand2R.clear();
        early.clear();
        preDelay.clear();
        for (int i = 0; i < kLines; ++i) {
            line[i].clear();
            lineDamp[i].clear();
        }
        for (int i = 0; i < 6; ++i)
            diff[i].clear();
    }

    void setParams(float time, float tone, float depth, float mix)
    {
        time = rbClip01(time);
        tone = rbClip01(tone);
        depth = rbClip01(depth);
        mix = rbClip01(mix);

        const float t60 = 0.6f + std::pow(time, 1.8f) * 15.0f;        // long halls (~0.6-15 s)
        const float band = 4800.0f + tone * 4700.0f;                  // 4.8-9.5 kHz: the 224's ~8 kHz band-limit
        const float dampCut = 1800.0f + std::pow(tone, 1.25f) * 10500.0f;
        inputBand.setCutoff(fs, band);
        inputBand2.setCutoff(fs, band);                               // 2nd pole -> steeper (7-pole-ish) skirt
        outputBandL.setCutoff(fs, band * 0.92f);
        outputBandR.setCutoff(fs, band * 0.92f);
        outputBand2L.setCutoff(fs, band * 0.92f);
        outputBand2R.setCutoff(fs, band * 0.92f);
        preSamples = (float)samplesForMs(fs, 12.0f + time * 22.0f);   // PRE-DELAY (real 224 has a dedicated slider)

        for (int i = 0; i < kLines; ++i) {
            const float delaySec = baseDelay[i] / fs;
            fbGain[i] = rbClamp(std::pow(10.0f, -3.0f * delaySec / t60), 0.40f, 0.988f);
            lineDamp[i].setCutoff(fs, dampCut * (0.84f + 0.035f * (float)i));
        }

        const float diffFb = 0.55f + 0.13f * depth;
        const float diffMs[6] = { 7.1f, 11.3f, 16.7f, 23.9f, 31.1f, 43.7f };
        for (int i = 0; i < 6; ++i)
            diff[i].set((float)samplesForMs(fs, diffMs[i]), rbClamp(diffFb + 0.015f * (float)(i & 1), 0.50f, 0.74f));

        width = 0.58f + 0.42f * depth;
        inputGain = 0.105f + 0.035f * depth;
        modDepth = (1.0f + 31.0f * std::pow(depth, 1.35f)) * (fs / 48000.0f);

        const float m = mix * wetMax;
        const float a = m * 1.57079632679f;
        dryGain = std::cos(a);
        wetGain = std::sin(a) * 1.60f;
    }

    inline void process(float xL, float xR, float& outL, float& outR)
    {
        xL = dcL.process(xL);
        xR = dcR.process(xR);
        float x = inputBand2.process(inputBand.process(0.5f * (xL + xR)));
        x = rbSoftLimit(x * 1.06f) * 0.94f;

        preDelay.write(x);
        x = preDelay.read(preSamples);

        early.write(x);
        float earlyL = 0.18f * early.read((float)samplesForMs(fs, 11.7f))
                     - 0.13f * early.read((float)samplesForMs(fs, 19.3f))
                     + 0.11f * early.read((float)samplesForMs(fs, 29.9f));
        float earlyR = -0.15f * early.read((float)samplesForMs(fs, 13.1f))
                     + 0.14f * early.read((float)samplesForMs(fs, 23.7f))
                     + 0.10f * early.read((float)samplesForMs(fs, 34.1f));

        for (int i = 0; i < 6; ++i)
            x = diff[i].process(x);

        float y[kLines];
        float v[kLines];
        for (int i = 0; i < kLines; ++i) {
            phase[i] += inc[i];
            if (phase[i] > 6.28318530718f)
                phase[i] -= 6.28318530718f;
            // Randomized (incommensurate) modulation, not a pure sine, so the tail
            // shimmers like the 224 instead of a detectable coherent vibrato.
            const float wander = (std::sin(phase[i]) + 0.6f * std::sin(phase[i] * 1.73f + 1.1f * (float)i))
                                 * modDepth * (0.45f + 0.04f * (float)i);
            y[i] = line[i].read(baseDelay[i] + wander);
            v[i] = lineDamp[i].process(y[i]);
        }

        hadamard8(v);

        const float signs[kLines] = { 1.00f, -0.82f, 0.66f, -0.54f, 0.46f, -0.38f, 0.31f, -0.27f };
        for (int i = 0; i < kLines; ++i)
            line[i].write(rbLoopLimit(inputGain * signs[i] * x + fbGain[i] * v[i]));

        float wetL = 0.34f * y[0] - 0.25f * y[2] + 0.31f * y[4] - 0.22f * y[6] + earlyL;
        float wetR = -0.28f * y[1] + 0.33f * y[3] - 0.24f * y[5] + 0.29f * y[7] + earlyR;

        wetL = outputBand2L.process(outputBandL.process(wetL));
        wetR = outputBand2R.process(outputBandR.process(wetR));

        const float mid = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR) * width;
        wetL = mid + side;
        wetR = mid - side;

        outL = xL * dryGain + wetL * wetGain;
        outR = xR * dryGain + wetR * wetGain;
    }
};

class Pcm70RichChamberCore
{
    static const int kLines = 6;

    float fs = 48000.0f;
    float wetMax = 0.30f;
    float dryGain = 1.0f;
    float wetGain = 0.0f;
    float width = 0.72f;
    float inputGain = 0.13f;
    float predelay = 1200.0f;
    float lineDelay[kLines] = { 0.0f };
    float fbGain[kLines] = { 0.0f };
    float reflDelay[6] = { 0.0f };
    float reflGain[6] = { 0.0f };
    float phase[kLines] = { 0.0f };
    float inc[kLines] = { 0.0f };
    float modDepth = 0.0f;

    RbDcBlock dcL;
    RbDcBlock dcR;
    RbOnePoleLp adcBand;
    RbOnePoleLp wetBandL;
    RbOnePoleLp wetBandR;
    RbOnePoleLp lineDamp[kLines];
    RbDelay<131072> predelayLine;
    RbDelay<65536> tank[kLines];
    RbAllpass<8192> inputDiff[5];
    RbAllpass<8192> outputDiffL[2];
    RbAllpass<8192> outputDiffR[2];

    static int samplesForMs(float sampleRate, float ms)
    {
        return (int)(sampleRate * ms * 0.001f + 0.5f);
    }

    void configureStaticDelays()
    {
        const float diffMs[5] = { 4.1f, 6.3f, 9.7f, 14.9f, 21.1f };
        for (int i = 0; i < 5; ++i)
            inputDiff[i].set((float)samplesForMs(fs, diffMs[i]), 0.58f);

        outputDiffL[0].set((float)samplesForMs(fs, 5.9f), 0.48f);
        outputDiffL[1].set((float)samplesForMs(fs, 12.7f), 0.43f);
        outputDiffR[0].set((float)samplesForMs(fs, 6.7f), 0.49f);
        outputDiffR[1].set((float)samplesForMs(fs, 13.9f), 0.42f);

        const float rates[kLines] = { 0.083f, 0.101f, 0.127f, 0.149f, 0.173f, 0.197f };
        for (int i = 0; i < kLines; ++i) {
            phase[i] = 0.53f * (float)i;
            inc[i] = 6.28318530718f * rates[i] / fs;
        }
    }

public:
    void setWetMax(float w) { wetMax = rbClip01(w); }

    void setSampleRate(float sampleRate)
    {
        fs = sampleRate > 1000.0f ? sampleRate : 48000.0f;
        configureStaticDelays();
        clear();
    }

    void clear()
    {
        dcL.clear();
        dcR.clear();
        adcBand.clear();
        wetBandL.clear();
        wetBandR.clear();
        predelayLine.clear();
        for (int i = 0; i < kLines; ++i) {
            tank[i].clear();
            lineDamp[i].clear();
        }
        for (int i = 0; i < 5; ++i)
            inputDiff[i].clear();
        for (int i = 0; i < 2; ++i) {
            outputDiffL[i].clear();
            outputDiffR[i].clear();
        }
    }

    void setParams(float time, float tone, float depth, float mix)
    {
        time = rbClip01(time);
        tone = rbClip01(tone);
        depth = rbClip01(depth);
        mix = rbClip01(mix);

        const float sizeMeters = 5.6f + std::pow(depth, 1.12f) * 54.0f;   // up to ~60 m (real PCM70 chamber)
        const float sizeNorm = (sizeMeters - 5.6f) / 54.0f;
        const float delayScale = 0.62f + sizeNorm * 1.55f;
        const float hc = 170.0f + std::pow(tone, 1.85f) * 14830.0f;
        const float diffusion = 0.36f + depth * 0.44f;
        const float definition = 0.28f + depth * 0.62f;

        adcBand.setCutoff(fs, 15250.0f);
        wetBandL.setCutoff(fs, hc);
        wetBandR.setCutoff(fs, hc * 0.97f);

        predelay = (4.0f + depth * 42.0f + 10.0f * time) * fs * 0.001f;

        const float baseMs[kLines] = { 18.1f, 23.9f, 31.7f, 39.1f, 48.7f, 61.3f };
        const float t60Low = 0.42f + std::pow(time, 1.42f) * 1.55f;   // chamber = short/dense (~0.42-2.0 s)
        const float t60Mid = 0.30f + std::pow(time, 1.35f) * 1.25f;   // RT_low > RT_mid (real ordering)
        const float highScale = 0.34f + tone * 0.96f;
        const float t60 = 0.58f * t60Mid + 0.42f * t60Low;

        for (int i = 0; i < kLines; ++i) {
            lineDelay[i] = (float)samplesForMs(fs, baseMs[i] * delayScale);
            const float delaySec = lineDelay[i] / fs;
            fbGain[i] = rbClamp(std::pow(10.0f, -3.0f * delaySec / t60), 0.34f, 0.985f);
            const float dampHz = rbClamp(hc * (0.70f + highScale * 0.30f), 120.0f, fs * 0.42f);   // even/low-color chamber tail
            lineDamp[i].setCutoff(fs, dampHz);
        }

        const float diffMs[5] = { 4.1f, 6.3f, 9.7f, 14.9f, 21.1f };
        for (int i = 0; i < 5; ++i)
            inputDiff[i].set((float)samplesForMs(fs, diffMs[i] * (0.82f + 0.42f * sizeNorm)),
                             rbClamp(diffusion + 0.018f * (float)i, 0.34f, 0.78f));

        const float reflMs[6] = { 8.0f, 15.7f, 26.4f, 11.1f, 21.8f, 35.5f };
        const float reflPol[6] = { 1.0f, -0.72f, 0.58f, -0.91f, 0.69f, -0.52f };
        for (int i = 0; i < 6; ++i) {
            reflDelay[i] = predelay + (float)samplesForMs(fs, reflMs[i] * (0.72f + 1.10f * sizeNorm));
            reflGain[i] = reflPol[i] * (0.11f + 0.18f * definition) * (1.0f - 0.055f * (float)(i & 3));
        }

        width = 0.48f + 0.43f * depth;
        inputGain = 0.105f + 0.040f * definition;
        modDepth = (0.8f + 13.0f * std::pow(depth, 1.3f)) * (fs / 48000.0f);   // PCM70 CHORUSING (OM p.3-5)

        const float m = mix * wetMax;
        const float a = m * 1.57079632679f;
        dryGain = std::cos(a);
        wetGain = std::sin(a) * 1.15f;
    }

    inline void process(float xL, float xR, float& outL, float& outR)
    {
        xL = dcL.process(xL);
        xR = dcR.process(xR);

        float x = 0.5f * (xL + xR);
        x = adcBand.process(x);
        x = rbSoftLimit(x * 1.10f) * 0.91f;

        predelayLine.write(x);
        float pd = predelayLine.read(predelay);

        for (int i = 0; i < 5; ++i)
            pd = inputDiff[i].process(pd);

        float y[kLines];
        float v[kLines];
        float sum = 0.0f;
        for (int i = 0; i < kLines; ++i) {
            phase[i] += inc[i];
            if (phase[i] > 6.28318530718f)
                phase[i] -= 6.28318530718f;
            // CHORUSING: randomized (incommensurate) delay modulation so the tail
            // doesn't sound metallic — the PCM70's signature (OM p.3-5).
            const float wander = (std::sin(phase[i]) + 0.6f * std::sin(phase[i] * 1.63f + 0.9f * (float)i)) * modDepth;
            y[i] = tank[i].read(lineDelay[i] + wander);
            v[i] = lineDamp[i].process(y[i]);
            sum += v[i];
        }

        const float signs[kLines] = { 1.00f, -0.86f, 0.72f, -0.62f, 0.51f, -0.43f };
        for (int i = 0; i < kLines; ++i) {
            const float householder = (2.0f / (float)kLines) * sum - v[i];
            tank[i].write(rbLoopLimit(inputGain * signs[i] * pd + fbGain[i] * householder));
        }

        float earlyL = reflGain[0] * predelayLine.read(reflDelay[0])
                     + reflGain[1] * predelayLine.read(reflDelay[1])
                     + reflGain[2] * predelayLine.read(reflDelay[2]);
        float earlyR = reflGain[3] * predelayLine.read(reflDelay[3])
                     + reflGain[4] * predelayLine.read(reflDelay[4])
                     + reflGain[5] * predelayLine.read(reflDelay[5]);

        float wetL = 0.33f * y[0] - 0.24f * y[1] + 0.30f * y[3] - 0.20f * y[5] + earlyL;
        float wetR = -0.22f * y[0] + 0.31f * y[2] - 0.25f * y[4] + 0.29f * y[5] + earlyR;

        wetL = outputDiffL[1].process(outputDiffL[0].process(wetL));
        wetR = outputDiffR[1].process(outputDiffR[0].process(wetR));
        wetL = wetBandL.process(wetL);
        wetR = wetBandR.process(wetR);

        const float mid = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR) * width;
        wetL = mid + side;
        wetR = mid - side;

        outL = xL * dryGain + wetL * wetGain;
        outR = xR * dryGain + wetR * wetGain;
    }
};

#endif // STUDIO_REVERB_MODELS_HPP
