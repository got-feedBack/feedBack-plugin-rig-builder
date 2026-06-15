/*
 * BitCruncher - ADC0804-style lo-fi converter for the game Pedal_BitCruncher.
 *
 * Local reference: pedals/bit crusher.pdf (Beverly BitCrusher). The schematic
 * uses op-amp conditioning into an 8-bit ADC and resistor-ladder output. The
 * the game pedal adds envelope controls, so Sens/Attack/Release modulate the
 * downsample/bit-depth amount while FilterType selects the color path.
 */
#include "DistrhoPlugin.hpp"
#include "BitCruncherParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float onePoleCoeff(float ms, float sr)
{
    ms = ms < 0.05f ? 0.05f : ms;
    return 1.0f - std::exp(-1.0f / (ms * 0.001f * sr));
}

static inline float quantizeSigned(float x, float bits)
{
    bits = clampf(bits, 2.25f, 8.0f);
    const float steps = std::pow(2.0f, bits - 1.0f) - 1.0f;
    if (steps <= 1.0f)
        return x;
    x = clampf(x, -1.0f, 1.0f);
    return std::round(x * steps) / steps;
}

class Biquad
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f)
            na0 = 1.0f;
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        a1 = na1 * invA0;
        a2 = na2 * invA0;
    }

    static float safeHz(float hz, float sr)
    {
        const float nyquist = sr * 0.45f;
        if (hz < 20.0f)
            return 20.0f;
        return hz > nyquist ? nyquist : hz;
    }

public:
    void reset()
    {
        z1 = z2 = 0.0f;
    }

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setHighPass(float sr, float hz, float q)
    {
        hz = safeHz(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = safeHz(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setBandPass(float sr, float hz, float q)
    {
        hz = safeHz(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(alpha, 0.0f, -alpha, 1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }
};

} // namespace

class BitCruncherCore
{
    float sampleRate = 48000.0f;
    float attack = kBitCruncherDef[kAttack];
    float filterType = kBitCruncherDef[kFilterType];
    float mix = kBitCruncherDef[kMix];
    float release = kBitCruncherDef[kRelease];
    float sens = kBitCruncherDef[kSens];

    Biquad inputHp;
    Biquad colorLow;
    Biquad colorBand;
    Biquad colorHigh;
    Biquad outputHp;

    float env = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    float hold = 0.0f;
    float holdPhase = 1.0f;

    void update()
    {
        const float s = smoothstep(sens);
        const float f = clamp01(filterType);
        const int mode = (int)std::floor(f * 8.0f + 0.5f);

        const float attackMs = 1.5f + 120.0f * attack * attack;
        const float releaseMs = 8.0f + 720.0f * release * release;
        attackCoeff = onePoleCoeff(attackMs, sampleRate);
        releaseCoeff = onePoleCoeff(releaseMs, sampleRate);

        inputHp.setHighPass(sampleRate, 28.0f, 0.70f);
        outputHp.setHighPass(sampleRate, 18.0f, 0.70f);

        if (mode <= 3)
        {
            colorLow.setLowPass(sampleRate, 1050.0f + 1250.0f * s, 0.62f);
            colorBand.setBandPass(sampleRate, 1150.0f + 1300.0f * s, 1.10f);
            colorHigh.setHighPass(sampleRate, 540.0f + 620.0f * s, 0.72f);
        }
        else if (mode <= 5)
        {
            colorLow.setLowPass(sampleRate, 3150.0f + 3000.0f * s, 0.68f);
            colorBand.setBandPass(sampleRate, 1550.0f + 3450.0f * s, 1.42f);
            colorHigh.setHighPass(sampleRate, 170.0f + 520.0f * s, 0.72f);
        }
        else
        {
            colorLow.setLowPass(sampleRate, 6100.0f + 6200.0f * s, 0.72f);
            colorBand.setBandPass(sampleRate, 2300.0f + 4900.0f * s, 1.05f);
            colorHigh.setHighPass(sampleRate, 260.0f + 900.0f * s, 0.68f);
        }
    }

    float baseSampleRateForMode() const
    {
        const int mode = (int)std::floor(clamp01(filterType) * 8.0f + 0.5f);
        if (mode <= 3)
            return 5200.0f;
        if (mode <= 5)
            return 9200.0f;
        return 15500.0f;
    }

public:
    void reset()
    {
        env = 0.0f;
        hold = 0.0f;
        holdPhase = 1.0f;
        inputHp.reset();
        colorLow.reset();
        colorBand.reset();
        colorHigh.reset();
        outputHp.reset();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        update();
        reset();
    }

    void setAttack(float v)
    {
        attack = clamp01(v);
        update();
    }

    void setFilterType(float v)
    {
        filterType = clamp01(v);
        update();
    }

    void setMix(float v)
    {
        mix = clamp01(v);
    }

    void setRelease(float v)
    {
        release = clamp01(v);
        update();
    }

    void setSens(float v)
    {
        sens = clamp01(v);
        update();
    }

    float process(float in)
    {
        const float dry = in;
        const float m = smoothstep(mix);
        if (m < 0.0001f)
            return dry;

        const float s = smoothstep(sens);
        const float target = clamp01(std::fabs(in) * (2.4f + 6.4f * s));
        const float coeff = target > env ? attackCoeff : releaseCoeff;
        env += coeff * (target - env);
        const float e = smoothstep(env);

        float x = inputHp.process(in);
        x = std::tanh(x * (1.0f + 0.32f * s));

        const float amount = clamp01(0.18f + 0.44f * s + 0.10f * m + e * (0.18f + 0.24f * s));
        float sampleHz = baseSampleRateForMode() * (1.0f - 0.74f * amount);
        sampleHz = clampf(sampleHz, 650.0f, sampleRate * 0.42f);

        holdPhase += sampleHz / sampleRate;
        if (holdPhase >= 1.0f)
        {
            holdPhase -= std::floor(holdPhase);
            hold = x;
        }

        const float bits = 8.0f - 5.25f * amount;
        float wet = quantizeSigned(hold, bits);

        const int mode = (int)std::floor(clamp01(filterType) * 8.0f + 0.5f);
        const float low = colorLow.process(wet);
        const float band = colorBand.process(wet);
        const float high = colorHigh.process(wet);

        if (mode <= 3)
            wet = low * 0.90f + band * 0.22f;
        else if (mode <= 5)
            wet = low * 0.52f + band * 0.54f;
        else
            wet = low * 0.72f + high * 0.28f;

        wet = quantizeSigned(wet, bits + 0.45f);

        const float dryLevel = 1.0f - 0.72f * m;
        const float wetLevel = m * (0.70f + 0.12f * amount);
        float out = dry * dryLevel + wet * wetLevel;
        out = outputHp.process(out);
        out *= 0.93f - 0.05f * m;
        return clampf(out, -1.0f, 1.0f);
    }
};

class BitCruncherPlugin : public Plugin
{
    BitCruncherCore left;
    BitCruncherCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setAttack(params[kAttack]);
        right.setAttack(params[kAttack]);
        left.setFilterType(params[kFilterType]);
        right.setFilterType(params[kFilterType]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setRelease(params[kRelease]);
        right.setRelease(params[kRelease]);
        left.setSens(params[kSens]);
        right.setSens(params[kSens]);
    }

public:
    BitCruncherPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBitCruncherDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BitCruncher"; }
    const char* getDescription() const override { return "ADC0804 style bit crusher"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 't', 'C', 'r'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBitCruncherNames[index];
        parameter.symbol = kBitCruncherSymbols[index];
        parameter.ranges.min = kBitCruncherMin[index];
        parameter.ranges.max = kBitCruncherMax[index];
        parameter.ranges.def = kBitCruncherDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BitCruncherPlugin)
};

Plugin* createPlugin()
{
    return new BitCruncherPlugin();
}

END_NAMESPACE_DISTRHO
