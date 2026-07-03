/*
 * ModernWah - Morley Bad Horsie / optical contour wah for the game
 * Pedal_ModernWah.
 *
 * The local schematic is a Morley switchless contour wah: optical sweep,
 * buffered op-amp stages, and a contour resonance network. This model is
 * wider and cleaner than USWah/UKWah, with a faster optical-style envelope and
 * Sens controlling both envelope push and peak sharpness.
 */
#include "DistrhoPlugin.hpp"
#include "ModernWahParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class OpticalWah
{
    float fs = 48000.0f;
    float ic1 = 0.0f;
    float ic2 = 0.0f;
    float env = 0.0f;
    float atk = 0.0f;
    float rel = 0.0f;
    float lfoPhase = 0.0f;
    float lfoInc = 0.001f;
    float hpY = 0.0f;
    float hpX = 0.0f;
    float hpA = 0.987f;

    bool autoSweep = true;
    float pedal = 0.50f;
    float sens = 0.68f;
    float speed = 0.68f;

    static inline float clamp01(float v)
    {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    static inline float msCoef(float ms, float sampleRate)
    {
        return std::exp(-1.0f / (0.001f * ms * sampleRate));
    }

    void updateDerived()
    {
        atk = msCoef(3.0f, fs);
        rel = msCoef(95.0f, fs);

        // the game uses many high Speed values (77-100) on Modern Wah.
        // The previous 0.16..7.96 Hz range made those presets chatter like a
        // fast LFO; keep the optical auto sweep responsive but cap it lower.
        const float rateHz = 0.10f + std::pow(speed, 1.45f) * 3.45f;
        lfoInc = 6.28318530718f * rateHz / fs;

        const float hpFc = 85.0f;
        const float rc = 1.0f / (6.28318530718f * hpFc);
        const float dt = 1.0f / fs;
        hpA = rc / (rc + dt);
    }

public:
    void setSampleRate(float sampleRate)
    {
        fs = sampleRate > 0.0f ? sampleRate : 48000.0f;
        updateDerived();
    }

    void reset()
    {
        ic1 = 0.0f;
        ic2 = 0.0f;
        env = 0.0f;
        lfoPhase = 0.0f;
        hpY = 0.0f;
        hpX = 0.0f;
    }

    void setParams(float autoP, float pedalP, float sensP, float speedP)
    {
        autoSweep = autoP > 0.5f;
        pedal = clamp01(pedalP);
        sens = clamp01(sensP);
        speed = clamp01(speedP);
        updateDerived();
    }

    inline float process(float x)
    {
        const float level = std::fabs(x);
        const float envCoef = level > env ? atk : rel;
        env = envCoef * env + (1.0f - envCoef) * level;
        float pick = env * (2.6f + sens * 3.0f);
        if (pick > 1.0f)
            pick = 1.0f;

        float pos;
        if (autoSweep)
        {
            lfoPhase += lfoInc;
            if (lfoPhase >= 6.28318530718f)
                lfoPhase -= 6.28318530718f;

            const float lfo01 = 0.5f + 0.5f * std::sin(lfoPhase);
            const float base = 0.02f + 0.20f * pedal;
            const float depth = 0.56f + 0.34f * sens;
            pos = base + depth * lfo01 + 0.18f * sens * pick;
        }
        else
        {
            pos = pedal + 0.22f * sens * pick;
        }
        pos = clamp01(pos);

        const float shaped = pos * (1.15f - 0.15f * pos);
        float fc = 310.0f * std::pow(10.2f, shaped); // about 310 Hz .. 3.16 kHz
        const float nyq = fs * 0.43f;
        if (fc > nyq)
            fc = nyq;

        const float q = 3.0f + 4.1f * sens;
        const float g = std::tan(3.14159265359f * fc / fs);
        const float k = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float v3 = x - ic2;
        const float bp = a1 * ic1 + a2 * v3;
        const float lp = ic2 + a2 * ic1 + g * a2 * v3;
        ic1 = 2.0f * bp - ic1;
        ic2 = 2.0f * lp - ic2;

        const float lowAssist = lp * 0.08f * (1.0f - sens);
        const float wah = bp * k * (3.1f + 1.45f * sens);
        const float mixed = wah + lowAssist + x * 0.025f;

        hpY = hpA * (hpY + mixed - hpX);
        hpX = mixed;

        return hpY * 0.90f;
    }
};

class ModernWahPlugin : public Plugin
{
    OpticalWah left;
    OpticalWah right;
    float params[kParamCount];

    void recalc()
    {
        left.setParams(params[kAuto], params[kPedal], params[kSens], params[kSpeed]);
        right.setParams(params[kAuto], params[kPedal], params[kSens], params[kSpeed]);
    }

public:
    ModernWahPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kModernWahDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(sr);
        right.setSampleRate(sr);
        left.reset();
        right.reset();
        recalc();
    }

protected:
    const char* getLabel() const override { return "ModernWah"; }
    const char* getDescription() const override { return "Optical contour wah"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'd', 'W', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == kAuto)
            parameter.hints |= kParameterIsBoolean;
        parameter.name = kModernWahNames[index];
        parameter.symbol = kModernWahSymbols[index];
        parameter.ranges.min = kModernWahMin[index];
        parameter.ranges.max = kModernWahMax[index];
        parameter.ranges.def = kModernWahDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index < (uint32_t)kParamCount)
        {
            params[index] = value;
            recalc();
        }
    }

    void sampleRateChanged(double sampleRate) override
    {
        left.setSampleRate((float)sampleRate);
        right.setSampleRate((float)sampleRate);
        left.reset();
        right.reset();
        recalc();
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModernWahPlugin)
};

Plugin* createPlugin()
{
    return new ModernWahPlugin();
}

END_NAMESPACE_DISTRHO
