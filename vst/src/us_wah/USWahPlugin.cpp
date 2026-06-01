/*
 * USWah - Dunlop Cry Baby GCB-95 style wah for Rocksmith Pedal_USWah.
 *
 * The local Cry Baby schematic shows the classic JFET input buffer, inductor
 * resonant wah network, sweep pot, and output buffer. This model keeps the
 * Rocksmith four-knob contract while voicing the DSP as a guitar wah: narrower
 * than BassWah, brighter, less dry blend, and a stronger upper-mid peak.
 */
#include "DistrhoPlugin.hpp"
#include "USWahParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class CryBabyWah
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
    float hpA = 0.985f;

    bool autoSweep = true;
    float pedal = 0.65f;
    float sens = 0.70f;
    float speed = 0.55f;

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
        atk = msCoef(5.0f, fs);
        rel = msCoef(130.0f, fs);

        const float rateHz = 0.12f + speed * speed * 6.6f;
        lfoInc = 6.28318530718f * rateHz / fs;

        const float hpFc = 95.0f;
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
        float pick = env * (2.2f + sens * 2.8f);
        if (pick > 1.0f)
            pick = 1.0f;

        float pos;
        if (autoSweep)
        {
            lfoPhase += lfoInc;
            if (lfoPhase >= 6.28318530718f)
                lfoPhase -= 6.28318530718f;

            const float lfo01 = 0.5f - 0.5f * std::cos(lfoPhase);
            const float base = 0.05f + 0.22f * pedal;
            const float depth = 0.52f + 0.30f * sens;
            pos = base + depth * lfo01 + 0.14f * sens * pick;
        }
        else
        {
            pos = pedal + 0.18f * sens * pick;
        }
        pos = clamp01(pos);

        const float shaped = pos * pos * (3.0f - 2.0f * pos);
        float fc = 350.0f * std::pow(6.25f, shaped); // about 350 Hz .. 2.2 kHz
        const float nyq = fs * 0.43f;
        if (fc > nyq)
            fc = nyq;

        const float q = 4.4f + 2.6f * sens;
        const float g = std::tan(3.14159265359f * fc / fs);
        const float k = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float v3 = x - ic2;
        const float bp = a1 * ic1 + a2 * v3;
        const float lp = ic2 + a2 * ic1 + g * a2 * v3;
        ic1 = 2.0f * bp - ic1;
        ic2 = 2.0f * lp - ic2;

        const float wah = bp * k * (3.2f + 1.1f * sens);
        const float mixed = wah + x * 0.035f;

        hpY = hpA * (hpY + mixed - hpX);
        hpX = mixed;

        return hpY * 0.92f;
    }
};

class USWahPlugin : public Plugin
{
    CryBabyWah left;
    CryBabyWah right;
    float params[kParamCount];

    void recalc()
    {
        left.setParams(params[kAuto], params[kPedal], params[kSens], params[kSpeed]);
        right.setParams(params[kAuto], params[kPedal], params[kSens], params[kSpeed]);
    }

public:
    USWahPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kUSWahDef[i];
        const float sr = (float)getSampleRate();
        left.setSampleRate(sr);
        right.setSampleRate(sr);
        left.reset();
        right.reset();
        recalc();
    }

protected:
    const char* getLabel() const override { return "USWah"; }
    const char* getDescription() const override { return "Cry Baby GCB-95 style wah"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('U', 'S', 'W', 'h'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == kAuto)
            parameter.hints |= kParameterIsBoolean;
        parameter.name = kUSWahNames[index];
        parameter.symbol = kUSWahSymbols[index];
        parameter.ranges.min = kUSWahMin[index];
        parameter.ranges.max = kUSWahMax[index];
        parameter.ranges.def = kUSWahDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(USWahPlugin)
};

Plugin* createPlugin()
{
    return new USWahPlugin();
}

END_NAMESPACE_DISTRHO
