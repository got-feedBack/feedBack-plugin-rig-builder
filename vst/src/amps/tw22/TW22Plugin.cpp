/*
 * TW22 - Fender Super-Sonic 22 / 6V6-style amp for Rocksmith's Amp_TW22.
 *
 * Local reference:
 *   amps/Fender SuperSonic 22 (TW22)/Fender-Super-Sonic-22-Schematic.pdf
 *
 * The schematic has a Vintage/Fat path, a Burn path with Gain 1 and Gain 2,
 * a Fender-style Bass/Mid/Treble stack, reverb, and a pair of 6V6 output
 * tubes. Rocksmith exposes only Gain, Bass, Mid, and Treble, so Gain blends
 * from clean Vintage/Fat behavior into the cascaded Burn path.
 */
#include "DistrhoPlugin.hpp"
#include "TW22Params.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    return std::fmax(20.0f, std::fmin(hz, nyquist));
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float smoothstepRange(float edge0, float edge1, float x)
{
    return smoothstep((x - edge0) / (edge1 - edge0));
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

static inline float asymTube(float x, float drive, float bias)
{
    const float pushed = x * drive + bias;
    const float y = std::tanh(pushed);
    const float correction = std::tanh(bias);
    return (y - correction) / (1.0f - 0.32f * std::fabs(correction));
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
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setPeaking(float sr, float hz, float q, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a,
            1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }

    void setHighShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);

        set(a * ((a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
            a * ((a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha,
            2.0f * ((a - 1.0f) - (a + 1.0f) * c),
            (a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha);
    }

    void setLowShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);

        set(a * ((a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha),
            2.0f * a * ((a - 1.0f) - (a + 1.0f) * c),
            a * ((a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha,
            -2.0f * ((a - 1.0f) + (a + 1.0f) * c),
            (a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
};

class DcBlock
{
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void reset()
    {
        x1 = y1 = 0.0f;
    }

    float process(float x)
    {
        const float y = x - x1 + 0.995f * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

} // namespace

class TW22Core
{
    float sampleRate = 48000.0f;
    float gain = kTW22Def[kGain];
    float bass = kTW22Def[kBass];
    float mid = kTW22Def[kMid];
    float treble = kTW22Def[kTreble];

    Biquad inputHp;
    Biquad inputBright;
    Biquad vintageFat;
    Biquad burnTighten;
    Biquad interstageLowPass;
    Biquad bassShelf;
    Biquad lowMidPunch;
    Biquad midTone;
    Biquad trebleShelf;
    Biquad powerPresence;
    Biquad speakerHp;
    Biquad speakerThump;
    Biquad speakerBody;
    Biquad speakerBite;
    Biquad speakerFizzNotch;
    Biquad speakerLp;
    DcBlock dcBlock;

    float sag = 0.0f;

    static float eqDb(float normalized, float rangeDb)
    {
        return (clamp01(normalized) - 0.5f) * 2.0f * rangeDb;
    }

    void updateFilters()
    {
        const float g = smoothstep(gain);
        const float burn = smoothstepRange(0.34f, 0.78f, gain);

        inputHp.setHighPass(sampleRate, 54.0f + 76.0f * g + 70.0f * burn, 0.72f);
        inputBright.setHighShelf(sampleRate, 2500.0f + 900.0f * treble, 0.70f,
                                 -1.4f + 5.4f * treble - 1.4f * burn);
        vintageFat.setPeaking(sampleRate, 190.0f + 70.0f * bass, 0.76f,
                              1.5f + 3.6f * bass - 1.6f * burn);

        burnTighten.setHighPass(sampleRate, 92.0f + 175.0f * burn + 72.0f * g, 0.70f);
        interstageLowPass.setLowPass(sampleRate, 9300.0f - 3400.0f * burn + 1200.0f * treble, 0.64f);

        bassShelf.setLowShelf(sampleRate, 112.0f + 22.0f * bass, 0.74f, eqDb(bass, 10.5f));
        lowMidPunch.setPeaking(sampleRate, 310.0f + 115.0f * bass, 0.72f,
                               -1.8f + 4.9f * bass + 1.1f * burn);
        midTone.setPeaking(sampleRate, 710.0f + 360.0f * mid, 0.64f,
                           -7.0f + 13.5f * mid - 1.6f * burn);
        trebleShelf.setHighShelf(sampleRate, 2050.0f + 1500.0f * treble, 0.70f,
                                 eqDb(treble, 10.5f) - 1.5f * burn);
        powerPresence.setHighShelf(sampleRate, 2900.0f + 950.0f * treble, 0.78f,
                                   -2.8f + 6.5f * treble - 2.0f * burn);

        speakerHp.setHighPass(sampleRate, 78.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 155.0f, 0.86f, 0.8f + 2.2f * bass);
        speakerBody.setPeaking(sampleRate, 420.0f, 0.78f, 0.9f + 1.6f * mid - 1.1f * burn);
        speakerBite.setPeaking(sampleRate, 2450.0f + 480.0f * treble, 0.76f,
                               1.7f + 2.2f * treble + 0.8f * burn);
        speakerFizzNotch.setPeaking(sampleRate, 4680.0f + 380.0f * treble, 0.95f,
                                    -3.2f - 3.2f * burn);
        speakerLp.setLowPass(sampleRate, 6100.0f + 2200.0f * treble - 1050.0f * burn, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset();
        inputBright.reset();
        vintageFat.reset();
        burnTighten.reset();
        interstageLowPass.reset();
        bassShelf.reset();
        lowMidPunch.reset();
        midTone.reset();
        trebleShelf.reset();
        powerPresence.reset();
        speakerHp.reset();
        speakerThump.reset();
        speakerBody.reset();
        speakerBite.reset();
        speakerFizzNotch.reset();
        speakerLp.reset();
        dcBlock.reset();
        sag = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setGain(float v)
    {
        gain = clamp01(v);
        updateFilters();
    }

    void setBass(float v)
    {
        bass = clamp01(v);
        updateFilters();
    }

    void setMid(float v)
    {
        mid = clamp01(v);
        updateFilters();
    }

    void setTreble(float v)
    {
        treble = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float g = smoothstep(gain);
        const float burn = smoothstepRange(0.34f, 0.78f, gain);

        float x = inputHp.process(in);
        x = inputBright.process(x);
        x = vintageFat.process(x);
        x = softClip(x * (1.06f + 0.16f * burn)) * (0.93f - 0.08f * burn);

        const float vintageDrive = 1.05f + 2.7f * gain + 1.7f * g;
        float vintage = asymTube(x, vintageDrive, 0.012f + 0.018f * gain);
        vintage = vintage * (0.42f + 0.58f * gain) + x * (0.58f * (1.0f - gain));

        float burnPath = burnTighten.process(x);
        const float gain1 = 1.55f + 4.8f * gain + 5.8f * g;
        burnPath = asymTube(burnPath, gain1, 0.026f + 0.032f * gain);
        burnPath = interstageLowPass.process(burnPath);
        const float gain2 = 1.35f + 3.7f * gain + 6.4f * burn;
        burnPath = asymTube(burnPath, gain2, -0.018f - 0.026f * gain);

        float y = vintage * (1.0f - burn) + burnPath * burn;
        y = bassShelf.process(y);
        y = lowMidPunch.process(y);
        y = midTone.process(y);
        y = trebleShelf.process(y);

        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0038f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.105f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.62f + 1.25f * gain));

        const float powerDrive = (1.12f + 2.2f * gain + 3.4f * burn) * sagDrop;
        y = asymTube(y, powerDrive, 0.008f + 0.014f * (treble - bass));
        y = 0.84f * y + 0.16f * softClip(y * (1.7f + 1.4f * gain));

        y = powerPresence.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerBody.process(y);
        y = speakerBite.process(y);
        y = speakerFizzNotch.process(y);
        y = speakerLp.process(y);

        const float toneEnergy = 1.0f
            + 0.014f * std::fabs((bass - 0.5f) * 21.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 27.0f)
            + 0.014f * std::fabs((treble - 0.5f) * 21.0f);
        const float level = (0.78f + 0.14f * (1.0f - gain)) /
            ((1.0f + 0.20f * gain + 0.22f * burn) * toneEnergy);
        return softClip(y * level) * 0.98f;
    }
};

class TW22Plugin : public Plugin
{
    TW22Core left;
    TW22Core right;
    float params[kParamCount];

    void applyAll()
    {
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setBass(params[kBass]);
        right.setBass(params[kBass]);
        left.setMid(params[kMid]);
        right.setMid(params[kMid]);
        left.setTreble(params[kTreble]);
        right.setTreble(params[kTreble]);
    }

public:
    TW22Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTW22Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "TW22"; }
    const char* getDescription() const override { return "Fender Super-Sonic 22 style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('T', 'w', '2', '2'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kTW22Names[index];
        parameter.symbol = kTW22Symbols[index];
        parameter.ranges.min = kTW22Min[index];
        parameter.ranges.max = kTW22Max[index];
        parameter.ranges.def = kTW22Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TW22Plugin)
};

Plugin* createPlugin()
{
    return new TW22Plugin();
}

END_NAMESPACE_DISTRHO
