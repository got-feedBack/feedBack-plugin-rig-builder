/*
 * DynamicsCompression - Dyna Comp-style compressor for Rocksmith's
 * Pedal_Compression. The references are MXR Dynacomp / Dyna Comp CA3080 OTA
 * schematics. Rocksmith exposes Comp, Attack, and Release, so this model keeps
 * the OTA-style squeeze and sustain while mapping those three controls.
 */
#include "DistrhoPlugin.hpp"
#include "DynamicsCompressionParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float dbToAmp(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float ampToDb(float amp)
{
    return 20.0f * std::log10(amp + 1.0e-9f);
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

} // namespace

class DynamicsCompressionCore
{
    float sampleRate = 48000.0f;
    float comp = kDynamicsCompressionDef[kComp];
    float attack = kDynamicsCompressionDef[kAttack];
    float release = kDynamicsCompressionDef[kRelease];

    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    float toneY = 0.0f;
    float env = 0.0f;
    float gainSmooth = 1.0f;

    float hpA = 0.0f;
    float toneA = 0.0f;
    float attackA = 0.0f;
    float releaseA = 0.0f;
    float gainA = 0.0f;

    void updateFilters()
    {
        const float dt = 1.0f / sampleRate;

        const float hpHz = 24.0f;
        const float hpRc = 1.0f / (2.0f * 3.14159265359f * hpHz);
        hpA = hpRc / (hpRc + dt);

        // Dyna Comp-style pedals are not hi-fi bright after heavy squeeze.
        const float toneHz = 7600.0f - 1800.0f * comp;
        toneA = 1.0f - std::exp(-2.0f * 3.14159265359f * toneHz / sampleRate);

        // RS Attack is stored 0..100-ish; slower values preserve more pick.
        const float attackMs = 1.2f + 78.0f * attack * attack;
        // RS Release values reach about 540; mapping code normalizes by 600.
        const float releaseMs = 20.0f + 580.0f * release;
        attackA = 1.0f - std::exp(-1.0f / (0.001f * attackMs * sampleRate));
        releaseA = 1.0f - std::exp(-1.0f / (0.001f * releaseMs * sampleRate));

        gainA = 1.0f - std::exp(-1.0f / (0.006f * sampleRate));
    }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        return y;
    }

    float lowPass(float x)
    {
        toneY += toneA * (x - toneY);
        return toneY;
    }

    void updateEnvelope(float x)
    {
        const float detector = std::sqrt(x * x + 1.0e-8f);
        const float a = detector > env ? attackA : releaseA;
        env += a * (detector - env);
    }

    float gainComputer()
    {
        const float amount = comp;
        const float thresholdDb = -12.0f - 34.0f * amount;
        const float ratio = 1.35f + 9.5f * amount;
        const float kneeDb = 5.0f + 7.0f * (1.0f - amount);
        const float envDb = ampToDb(env);
        const float over = envDb - thresholdDb;

        float reductionDb = 0.0f;
        if (over > -0.5f * kneeDb)
        {
            const float compressed = over < 0.5f * kneeDb
                ? ((over + 0.5f * kneeDb) * (over + 0.5f * kneeDb)) / (2.0f * kneeDb)
                : over;
            reductionDb = compressed * (1.0f - 1.0f / ratio);
        }

        const float target = dbToAmp(-reductionDb);
        gainSmooth += gainA * (target - gainSmooth);
        return gainSmooth;
    }

public:
    void reset()
    {
        hpX1 = hpY1 = toneY = env = 0.0f;
        gainSmooth = 1.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setComp(float v)
    {
        comp = clamp01(v);
        updateFilters();
    }

    void setAttack(float v)
    {
        attack = clamp01(v);
        updateFilters();
    }

    void setRelease(float v)
    {
        release = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        float x = highPass(in);

        // Input transistor and OTA input rounding. Keep it subtle: this is
        // compression color, not distortion.
        const float inputDrive = 1.05f + 0.55f * comp;
        x = 0.88f * x + 0.12f * softClip(x * inputDrive);

        updateEnvelope(x);
        const float gr = gainComputer();

        const float wet = x * gr;
        const float dryKeep = 0.04f + 0.10f * attack;
        float y = wet * (1.0f - dryKeep) + x * dryKeep;

        // Sustain makeup follows the Dyna Comp idea but is trimmed so the
        // pedal does not behave like a pure boost.
        const float makeupDb = 0.8f + 6.2f * comp;
        y *= dbToAmp(makeupDb) * (0.62f + 0.08f * (1.0f - comp));

        y = softClip(y * (0.94f + 0.08f * comp)) * 0.98f;
        y = lowPass(y);
        return y;
    }
};

class DynamicsCompressionPlugin : public Plugin
{
    DynamicsCompressionCore left;
    DynamicsCompressionCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setComp(params[kComp]);
        right.setComp(params[kComp]);
        left.setAttack(params[kAttack]);
        right.setAttack(params[kAttack]);
        left.setRelease(params[kRelease]);
        right.setRelease(params[kRelease]);
    }

public:
    DynamicsCompressionPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDynamicsCompressionDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "DynamicsCompression"; }
    const char* getDescription() const override { return "Dyna Comp-style pedal compressor"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 1); }
    int64_t getUniqueId() const override { return d_cconst('D', 'c', 'm', 'p'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDynamicsCompressionNames[index];
        parameter.symbol = kDynamicsCompressionSymbols[index];
        parameter.ranges.min = kDynamicsCompressionMin[index];
        parameter.ranges.max = kDynamicsCompressionMax[index];
        parameter.ranges.def = kDynamicsCompressionDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicsCompressionPlugin)
};

Plugin* createPlugin()
{
    return new DynamicsCompressionPlugin();
}

END_NAMESPACE_DISTRHO
