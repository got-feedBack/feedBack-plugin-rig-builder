/*
 * TW26 - Fender Tweed Deluxe / 6V6-style amp for Rocksmith's Amp_TW26.
 *
 * Local reference:
 *   amps/Fender Deluxe (TW26)/Fender-57-Deluxe-Schematic.pdf
 *
 * The 5E3 schematic has four inputs, two 1M channel volumes, one 1M Tone
 * control with 500 pF / 0.0047 uF shaping, a cathodyne phase inverter, and
 * cathode-biased 6V6 output tubes. Rocksmith exposes Gain, Bass, Mid,
 * Treble, and Pres, so the extra tone knobs become voicing controls around
 * the single real Tone circuit rather than a blackface-style tone stack.
 */
#include "DistrhoPlugin.hpp"
#include "TW26Params.h"
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

class TW26Core
{
    float sampleRate = 48000.0f;
    float gain = kTW26Def[kGain];
    float bass = kTW26Def[kBass];
    float mid = kTW26Def[kMid];
    float treble = kTW26Def[kTreble];
    float pres = kTW26Def[kPres];

    Biquad inputHp;
    Biquad pickupLoad;
    Biquad brightBleed;
    Biquad channelBody;
    Biquad couplingHp;
    Biquad toneBass;
    Biquad toneBody;
    Biquad toneBite;
    Biquad phaseLowPass;
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
        const float pushed = smoothstepRange(0.30f, 0.86f, gain);
        const float hot = smoothstepRange(0.62f, 0.98f, gain);
        const float tone = smoothstep(treble);

        // The 5E3's large coupling caps make the amp feel loose. Raise the
        // effective high-pass only as Gain rises so clean settings stay full.
        inputHp.setHighPass(sampleRate,
                            34.0f + 44.0f * g + 42.0f * hot - 18.0f * bass,
                            0.68f);
        pickupLoad.setLowPass(sampleRate,
                              11800.0f - 2100.0f * (1.0f - tone) - 1700.0f * hot,
                              0.63f);
        brightBleed.setHighShelf(sampleRate, 1200.0f + 1050.0f * tone, 0.72f,
                                 -2.8f + 6.6f * tone + 2.2f * pres - 1.0f * hot);
        channelBody.setPeaking(sampleRate, 180.0f + 80.0f * bass, 0.72f,
                               1.0f + 4.2f * bass - 2.6f * hot);

        couplingHp.setHighPass(sampleRate,
                               48.0f + 72.0f * hot + 52.0f * (1.0f - bass),
                               0.70f);
        toneBass.setLowShelf(sampleRate, 118.0f + 34.0f * bass, 0.70f,
                             eqDb(bass, 8.0f) - 2.4f * hot * bass);
        toneBody.setPeaking(sampleRate, 560.0f + 260.0f * mid, 0.60f,
                            -2.5f + 8.5f * mid + 1.3f * pushed);
        toneBite.setHighShelf(sampleRate, 1650.0f + 1700.0f * tone, 0.70f,
                              eqDb(treble, 8.5f) + 3.2f * pres - 1.4f * hot);
        phaseLowPass.setLowPass(sampleRate,
                                5200.0f + 2600.0f * tone + 1800.0f * pres - 1600.0f * hot,
                                0.64f);
        powerPresence.setHighShelf(sampleRate, 2350.0f + 1100.0f * pres, 0.76f,
                                   -4.2f + 8.0f * pres + 0.7f * tone);

        speakerHp.setHighPass(sampleRate, 78.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 145.0f, 0.84f,
                                1.5f + 2.7f * bass - 1.4f * hot);
        speakerBody.setPeaking(sampleRate, 390.0f + 80.0f * mid, 0.76f,
                               1.8f + 2.2f * mid);
        speakerBite.setPeaking(sampleRate, 2300.0f + 520.0f * tone, 0.74f,
                               1.2f + 2.2f * tone + 1.8f * pres);
        speakerFizzNotch.setPeaking(sampleRate, 4520.0f + 420.0f * pres, 0.95f,
                                    -3.6f - 3.0f * hot);
        speakerLp.setLowPass(sampleRate,
                             5450.0f + 2050.0f * tone + 1200.0f * pres - 1150.0f * hot,
                             0.66f);
    }

public:
    void reset()
    {
        inputHp.reset();
        pickupLoad.reset();
        brightBleed.reset();
        channelBody.reset();
        couplingHp.reset();
        toneBass.reset();
        toneBody.reset();
        toneBite.reset();
        phaseLowPass.reset();
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

    void setPres(float v)
    {
        pres = clamp01(v);
        updateFilters();
    }

    float process(float in)
    {
        const float pushed = smoothstepRange(0.30f, 0.86f, gain);
        const float hot = smoothstepRange(0.62f, 0.98f, gain);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = brightBleed.process(x);
        x = channelBody.process(x);
        x = softClip(x * (1.02f + 0.10f * hot)) * (0.96f - 0.06f * hot);

        const float normalDrive = 0.95f + 3.6f * gain + 4.4f * pushed + 2.2f * hot;
        float normal = asymTube(x, normalDrive, 0.018f + 0.026f * gain);

        // Approximate the unused channel-volume loading that makes a 5E3 feel
        // wider and less linear than a single-volume preamp.
        const float micDrive = 0.78f + 2.4f * gain + 2.0f * pushed;
        float mic = asymTube(x * (0.72f + 0.18f * mid), micDrive,
                             -0.012f - 0.018f * gain);
        const float channelMix = 0.18f + 0.18f * mid + 0.08f * bass;

        float y = normal * (1.0f - channelMix) + mic * channelMix;
        const float cleanLeak = 0.42f * (1.0f - smoothstepRange(0.22f, 0.70f, gain));
        y = y * (1.0f - cleanLeak) + x * cleanLeak;

        y = couplingHp.process(y);
        const float secondDrive = 0.85f + 2.35f * gain + 3.15f * pushed + 1.25f * hot;
        y = asymTube(y, secondDrive, -0.010f + 0.012f * (treble - 0.5f));

        y = toneBass.process(y);
        y = toneBody.process(y);
        y = toneBite.process(y);
        y = phaseLowPass.process(y);

        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0065f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.180f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.95f + 1.95f * gain + 0.90f * hot));

        const float powerDrive = (0.98f + 2.3f * gain + 3.7f * pushed + 1.0f * hot) * sagDrop;
        y = asymTube(y, powerDrive,
                     0.012f + 0.020f * (treble - bass) + 0.008f * pres);
        const float powerBlend = 0.22f - 0.08f * (1.0f - hot);
        y = (1.0f - powerBlend) * y + powerBlend * softClip(y * (1.9f + 2.2f * pushed));
        y *= 0.96f - 0.18f * sag;

        y = powerPresence.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerBody.process(y);
        y = speakerBite.process(y);
        y = speakerFizzNotch.process(y);
        y = speakerLp.process(y);

        const float toneEnergy = 1.0f
            + 0.013f * std::fabs((bass - 0.5f) * 16.0f)
            + 0.014f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.014f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.010f * std::fabs((pres - 0.5f) * 16.0f);
        const float level = (0.70f + 0.16f * (1.0f - gain)) /
            ((1.0f + 0.42f * gain + 0.42f * pushed + 0.25f * hot) * toneEnergy);
        return softClip(y * level) * 0.96f;
    }
};

class TW26Plugin : public Plugin
{
    TW26Core left;
    TW26Core right;
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
    }

public:
    TW26Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTW26Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "TW26"; }
    const char* getDescription() const override { return "Fender 57 Deluxe / 5E3 style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('T', 'w', '2', '6'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kTW26Names[index];
        parameter.symbol = kTW26Symbols[index];
        parameter.ranges.min = kTW26Min[index];
        parameter.ranges.max = kTW26Max[index];
        parameter.ranges.def = kTW26Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TW26Plugin)
};

Plugin* createPlugin()
{
    return new TW26Plugin();
}

END_NAMESPACE_DISTRHO
