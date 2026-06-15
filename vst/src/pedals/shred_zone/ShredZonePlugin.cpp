/*
 * ShredZone - Boss MT-2 Metal Zone style distortion for the game's
 * Pedal_ShredZone.
 *
 * Local reference: pedals/shred zone.pdf. The MT-2 uses dual-gain high
 * saturation and a very active EQ. the game exposes Gain, Bass, Mid, and
 * Treble, so Level and mid-frequency are compensated/fixed internally.
 */
#include "DistrhoPlugin.hpp"
#include "ShredZoneParams.h"
#include "../_shared/automakeup.hpp"
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

static inline float softClip(float x)
{
    return std::tanh(x);
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

} // namespace

class ShredZoneCore
{
    float sampleRate = 48000.0f;
    float gain = kShredZoneDef[kGain];
    float bass = kShredZoneDef[kBass];
    float mid = kShredZoneDef[kMid];
    float treble = kShredZoneDef[kTreble];

    Biquad inputHp;
    Biquad preLowTight;
    Biquad preMidPush;
    Biquad clipRollOff;
    Biquad bassShelf;
    Biquad midEq;
    Biquad biteEq;
    Biquad trebleShelf;
    Biquad outputLp;

    static float eqDb(float normalized, float rangeDb)
    {
        return (clamp01(normalized) - 0.5f) * 2.0f * rangeDb;
    }

    void updateFilters()
    {
        const float g = smoothstep(gain);
        const float bassDb = eqDb(bass, 14.0f);
        const float midDb = eqDb(mid, 16.0f);
        const float trebleDb = eqDb(treble, 15.0f);

        inputHp.setHighPass(sampleRate, 76.0f + 120.0f * gain, 0.70f);
        preLowTight.setHighPass(sampleRate, 118.0f + 190.0f * g, 0.74f);
        preMidPush.setPeaking(sampleRate, 980.0f + 420.0f * gain, 0.82f,
                              3.0f + 5.0f * g);
        clipRollOff.setLowPass(sampleRate, 6400.0f - 2300.0f * g + 900.0f * treble, 0.68f);

        bassShelf.setLowShelf(sampleRate, 120.0f, 0.78f, bassDb);
        // the game exposes Mid but not MT-2 Mid Freq. Move the center a bit
        // with the control so cuts scoop lower and boosts focus upper mids.
        midEq.setPeaking(sampleRate, 720.0f + 720.0f * mid, 0.72f, midDb);
        biteEq.setPeaking(sampleRate, 2300.0f + 900.0f * treble, 0.68f,
                          1.6f + 3.8f * treble + 1.2f * g);
        trebleShelf.setHighShelf(sampleRate, 2600.0f + 1500.0f * treble, 0.74f, trebleDb);
        outputLp.setLowPass(sampleRate, 3600.0f + 7800.0f * treble, 0.62f);
    }

public:
    void reset()
    {
        inputHp.reset();
        preLowTight.reset();
        preMidPush.reset();
        clipRollOff.reset();
        bassShelf.reset();
        midEq.reset();
        biteEq.reset();
        trebleShelf.reset();
        outputLp.reset();
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
        float x = inputHp.process(in);
        x = preLowTight.process(x);
        x = preMidPush.process(x);

        // Dual-gain MT-2 cue: a compressed first stage feeding a harder second
        // stage. Gain=16 stays crunchy; Gain=94/100 is intentionally saturated.
        const float firstDrive = 1.6f + 6.4f * gain + 15.0f * g;
        float y = softClip(x * firstDrive) * (0.72f + 0.28f * gain);

        const float secondDrive = 1.25f + 4.8f * gain + 12.5f * g;
        y = softClip((y + 0.06f * x) * secondDrive);
        y = 0.84f * y + 0.16f * softClip(y * (2.2f + 2.8f * gain));
        y = clipRollOff.process(y);

        y = bassShelf.process(y);
        y = midEq.process(y);
        y = biteEq.process(y);
        y = trebleShelf.process(y);
        y = outputLp.process(y);

        // No the game Level knob. Keep level controlled despite very large
        // active-EQ boosts, but retain enough sustain for the high-gain presets.
        const float eqEnergy = 1.0f
            + 0.018f * std::fabs((bass - 0.5f) * 28.0f)
            + 0.016f * std::fabs((mid - 0.5f) * 32.0f)
            + 0.016f * std::fabs((treble - 0.5f) * 30.0f);
        const float level = 0.50f / ((1.0f + 0.35f * gain + 0.32f * g) * eqEnergy);  // -1.9 dB: ran a touch hot vs the other distortions
        return softClip(y * level) * 0.98f;
    }
};

class ShredZonePlugin : public Plugin
{
    ShredZoneCore left;
    ShredZoneCore right;
    RBAutoMakeup makeupL;
    RBAutoMakeup makeupR;
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
    ShredZonePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kShredZoneDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        makeupL.setSampleRate((float)getSampleRate());
        makeupR.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "ShredZone"; }
    const char* getDescription() const override { return "MT-2 style high-gain shred distortion"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'h', 'Z', 'n'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kShredZoneNames[index];
        parameter.symbol = kShredZoneSymbols[index];
        parameter.ranges.min = kShredZoneMin[index];
        parameter.ranges.max = kShredZoneMax[index];
        parameter.ranges.def = kShredZoneDef[index];
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
        makeupL.snap();
        makeupR.snap();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
        makeupL.setSampleRate((float)newSampleRate);
        makeupR.setSampleRate((float)newSampleRate);
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
            // Auto makeup-gain: match output loudness to the dry input so the
            // drive's controls change only the amount of clip, not the level.
            outL[i] = makeupL.process(inL[i], left.process(inL[i]));
            outR[i] = makeupR.process(inR[i], right.process(inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShredZonePlugin)
};

Plugin* createPlugin()
{
    return new ShredZonePlugin();
}

END_NAMESPACE_DISTRHO
