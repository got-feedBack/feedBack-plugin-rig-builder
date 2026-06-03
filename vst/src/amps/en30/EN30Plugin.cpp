/*
 * EN30 - Vox AC30 Top Boost / EL84-style amp for Rocksmith's Amp_EN30.
 *
 * Local references:
 *   amps/vox ac30 (en30)/ac30-60-02-iss5.pdf
 *   amps/vox ac30 (en30)/Vox_ac30cc2_ac30cc2x_2005_sm.pdf
 *   amps/vox ac30 (en30)/Vox_ac30c2.pdf
 *
 * The AC30 Top Boost reference has a brilliant input, ECC83 preamp stages,
 * a passive Bass/Treble network, a cut control before the EL84 output stage,
 * and a bright 2x12 Celestion output. Rocksmith exposes Gain, Bass, Mid,
 * Treble, Pres, and Bright, so Mid is modeled as the missing body/scoop
 * control and Pres is the inverse of the cut/presence behavior.
 */
#include "DistrhoPlugin.hpp"
#include "EN30Params.h"
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
    return (y - correction) / (1.0f - 0.35f * std::fabs(correction));
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

class EN30Core
{
    float sampleRate = 48000.0f;
    float gain = kEN30Def[kGain];
    float bass = kEN30Def[kBass];
    float mid = kEN30Def[kMid];
    float treble = kEN30Def[kTreble];
    float pres = kEN30Def[kPres];
    float bright = kEN30Def[kBright];

    Biquad inputHp;
    Biquad brightShelf;
    Biquad preChime;
    Biquad preTighten;
    Biquad interstageLowPass;
    Biquad bassShelf;
    Biquad lowMidBody;
    Biquad midScoop;
    Biquad trebleShelf;
    Biquad cutLowPass;
    Biquad powerPresence;
    Biquad speakerHp;
    Biquad speakerBody;
    Biquad speakerChime;
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
        const float b = bright >= 0.5f ? 1.0f : 0.0f;

        inputHp.setHighPass(sampleRate, 48.0f + 92.0f * g + 28.0f * b, 0.72f);
        brightShelf.setHighShelf(sampleRate, 1450.0f + 500.0f * treble, 0.70f,
                                 b ? (3.0f + 4.2f * treble) : -1.2f);
        preChime.setPeaking(sampleRate, 1220.0f + 720.0f * b, 0.78f,
                            1.2f + 3.7f * b + 1.9f * treble);
        preTighten.setHighPass(sampleRate, 58.0f + 135.0f * g, 0.70f);
        interstageLowPass.setLowPass(sampleRate, 8200.0f - 2600.0f * g + 1250.0f * pres, 0.66f);

        bassShelf.setLowShelf(sampleRate, 118.0f + 22.0f * bass, 0.74f, eqDb(bass, 9.5f));
        lowMidBody.setPeaking(sampleRate, 285.0f + 115.0f * bass, 0.72f,
                              -2.3f + 4.6f * bass - 1.3f * g);
        // AC30 has no Mid pot. Rocksmith Mid controls how much of the Top
        // Boost scoop is filled back in without changing the exposed knobs.
        midScoop.setPeaking(sampleRate, 720.0f + 420.0f * mid, 0.62f,
                            -6.7f + 12.2f * mid);
        trebleShelf.setHighShelf(sampleRate, 1850.0f + 1650.0f * treble, 0.70f,
                                 eqDb(treble, 11.5f) + 1.3f * b);

        // AC30 tone cut is inverted from a normal presence knob: more cut
        // darkens the power amp. Rocksmith Pres higher means more presence.
        cutLowPass.setLowPass(sampleRate, 3300.0f + 6100.0f * pres + 1100.0f * treble, 0.62f);
        powerPresence.setHighShelf(sampleRate, 2600.0f + 1200.0f * pres, 0.78f,
                                   -5.5f + 10.5f * pres + 1.5f * b);

        speakerHp.setHighPass(sampleRate, 76.0f, 0.72f);
        speakerBody.setPeaking(sampleRate, 360.0f, 0.82f, 1.3f + 1.8f * bass);
        speakerChime.setPeaking(sampleRate, 2250.0f + 520.0f * treble, 0.74f,
                                2.2f + 2.6f * treble + 1.2f * b);
        speakerFizzNotch.setPeaking(sampleRate, 4550.0f + 450.0f * pres, 0.92f,
                                    -3.5f - 2.7f * g);
        speakerLp.setLowPass(sampleRate, 5700.0f + 2300.0f * pres + 950.0f * b, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset();
        brightShelf.reset();
        preChime.reset();
        preTighten.reset();
        interstageLowPass.reset();
        bassShelf.reset();
        lowMidBody.reset();
        midScoop.reset();
        trebleShelf.reset();
        cutLowPass.reset();
        powerPresence.reset();
        speakerHp.reset();
        speakerBody.reset();
        speakerChime.reset();
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

    void setPres(float v)
    {
        pres = clamp01(v);
        updateFilters();
    }

    void setBright(float v)
    {
        bright = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float g = smoothstep(gain);
        const float hot = smoothstepRange(0.50f, 0.95f, gain);
        const float b = bright >= 0.5f ? 1.0f : 0.0f;

        float x = inputHp.process(in);
        x = brightShelf.process(x);
        x = preChime.process(x);
        // Treble boosters before an AC30 should hit the input stage into
        // compression, not runaway fuzz. This is nearly unity for normal DI
        // levels but caps the sharp upper-mid peaks a RangeBooster can send.
        x = softClip(x * (1.08f + 0.06f * hot)) * (0.92f - 0.06f * hot);

        const float firstDrive = 0.92f + 1.55f * gain + 3.20f * hot + 0.45f * b;
        float y = asymTube(x, firstDrive, 0.014f + 0.018f * gain);
        const float cleanLeak = 0.34f * (1.0f - smoothstepRange(0.32f, 0.85f, gain));
        y = y * (1.0f - cleanLeak) + x * cleanLeak;

        y = preTighten.process(y);
        y = interstageLowPass.process(y);

        const float secondDrive = 0.82f + 1.30f * gain + 2.55f * hot + 0.25f * b;
        y = asymTube(y, secondDrive, -0.010f - 0.016f * gain);

        y = bassShelf.process(y);
        y = lowMidBody.process(y);
        y = midScoop.process(y);
        y = trebleShelf.process(y);

        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0045f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.115f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.55f + 1.10f * gain));

        const float powerDrive = (1.00f + 1.65f * gain + 2.35f * hot) * sagDrop;
        y = asymTube(y, powerDrive, 0.010f + 0.018f * (treble - bass));
        y = 0.90f * y + 0.10f * softClip(y * (1.55f + 1.15f * gain));

        y = cutLowPass.process(y);
        y = powerPresence.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerBody.process(y);
        y = speakerChime.process(y);
        y = speakerFizzNotch.process(y);
        y = speakerLp.process(y);

        const float toneEnergy = 1.0f
            + 0.015f * std::fabs((bass - 0.5f) * 19.0f)
            + 0.014f * std::fabs((mid - 0.5f) * 24.0f)
            + 0.015f * std::fabs((treble - 0.5f) * 23.0f);
        const float level = (0.74f + 0.10f * (1.0f - gain)) /
            ((1.0f + 0.30f * gain + 0.28f * g) * toneEnergy);
        return softClip(y * level) * 0.98f;
    }
};

class EN30Plugin : public Plugin
{
    EN30Core left;
    EN30Core right;
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
        left.setPres(params[kPres]);
        right.setPres(params[kPres]);
        left.setBright(params[kBright]);
        right.setBright(params[kBright]);
    }

public:
    EN30Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kEN30Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "EN30"; }
    const char* getDescription() const override { return "AC30 Top Boost style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('E', 'n', '3', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kEN30Names[index];
        parameter.symbol = kEN30Symbols[index];
        parameter.ranges.min = kEN30Min[index];
        parameter.ranges.max = kEN30Max[index];
        parameter.ranges.def = kEN30Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EN30Plugin)
};

Plugin* createPlugin()
{
    return new EN30Plugin();
}

END_NAMESPACE_DISTRHO
