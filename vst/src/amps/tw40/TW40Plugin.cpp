/*
 * TW40 - Fender Bassman 5F6-A / 5881-style amp for Rocksmith's Amp_TW40.
 *
 * Local reference:
 *   amps/Fender Bassman Tweed (TW40)/Fender_bassman_5f6a.pdf
 *
 * The schematic has normal/bright inputs, a 12AY7 first stage, a 12AX7
 * recovery/cathode-follower tone-stack driver, a Bass/Middle/Treble FMV
 * stack, a long-tail phase inverter, a presence circuit in the feedback loop,
 * and fixed-bias 5881 output tubes into a 4x10 cabinet. Rocksmith exposes a
 * single Gain plus Bass/Mid/Treble/Pres, so Gain drives the preamp and power
 * section while the tone knobs keep the real 5F6-A control names.
 */
#include "DistrhoPlugin.hpp"
#include "TW40Params.h"
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

static inline float tonePot(float v)
{
    v = clamp01(v);
    if (v < 0.001f)
        return 0.001f;
    if (v > 0.999f)
        return 0.999f;
    return v;
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

class BassmanToneStack
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float b3 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
    float x1 = 0.0f;
    float x2 = 0.0f;
    float x3 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;
    float y3 = 0.0f;
    float sampleRate = 48000.0f;

public:
    void reset()
    {
        x1 = x2 = x3 = y1 = y2 = y3 = 0.0f;
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
    }

    void update(float treble, float mid, float bass)
    {
        const float t = tonePot(treble);
        const float m = tonePot(mid);
        const float l = tonePot(bass);

        const float R1 = 250.0e3f;
        const float R2 = 1.0e6f;
        const float R3 = 25.0e3f;
        const float R4 = 56.0e3f;
        const float C1 = 250.0e-12f;
        const float C2 = 20.0e-9f;
        const float C3 = 20.0e-9f;

        const float ab0 = 0.0f;
        const float ab1 = t*C1*R1 + m*C3*R3 + l*(C1*R2 + C2*R2) + (C1*R3 + C2*R3);
        const float ab2 = t*(C1*C2*R1*R4 + C1*C3*R1*R4)
                        - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                        + m*(C1*C3*R1*R3 + C1*C3*R3*R3 + C2*C3*R3*R3)
                        + l*(C1*C2*R1*R2 + C1*C2*R2*R4 + C1*C3*R2*R4)
                        + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                        + (C1*C2*R1*R3 + C1*C2*R3*R4 + C1*C3*R3*R4);
        const float ab3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                        - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                        + t*l*C1*C2*C3*R1*R2*R4;
        const float aa0 = 1.0f;
        const float aa1 = (C1*R1 + C1*R3 + C2*R3 + C2*R4 + C3*R4)
                        + m*C3*R3 + l*(C1*R2 + C2*R2);
        const float aa2 = m*(C1*C3*R1*R3 - C2*C3*R3*R4 + C1*C3*R3*R3 + C2*C3*R3*R3)
                        - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                        + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                        + l*(C1*C2*R2*R4 + C1*C2*R1*R2 + C1*C3*R2*R4 + C2*C3*R2*R4)
                        + (C1*C2*R1*R4 + C1*C3*R1*R4 + C1*C2*R3*R4
                           + C1*C2*R1*R3 + C1*C3*R3*R4 + C2*C3*R3*R4);
        const float aa3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                        - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + m*(C1*C2*C3*R3*R3*R4 + C1*C2*C3*R1*R3*R3
                             - C1*C2*C3*R1*R3*R4)
                        + l*(C1*C2*C3*R1*R2*R4) + C1*C2*C3*R1*R3*R4;

        const float c = 2.0f * sampleRate;
        const float c2 = c * c;
        const float c3 = c2 * c;
        const float nb0 = -ab0 - ab1*c - ab2*c2 - ab3*c3;
        const float nb1 = -3.0f*ab0 - ab1*c + ab2*c2 + 3.0f*ab3*c3;
        const float nb2 = -3.0f*ab0 + ab1*c + ab2*c2 - 3.0f*ab3*c3;
        const float nb3 = -ab0 + ab1*c - ab2*c2 + ab3*c3;
        const float na0 = -aa0 - aa1*c - aa2*c2 - aa3*c3;
        const float na1 = -3.0f*aa0 - aa1*c + aa2*c2 + 3.0f*aa3*c3;
        const float na2 = -3.0f*aa0 + aa1*c + aa2*c2 - 3.0f*aa3*c3;
        const float na3 = -aa0 + aa1*c - aa2*c2 + aa3*c3;

        if (std::fabs(na0) < 1.0e-30f)
        {
            b0 = 1.0f; b1 = b2 = b3 = a1 = a2 = a3 = 0.0f;
            return;
        }
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        b3 = nb3 * invA0;
        a1 = na1 * invA0;
        a2 = na2 * invA0;
        a3 = na3 * invA0;
    }

    float process(float x)
    {
        const float y = b0*x + b1*x1 + b2*x2 + b3*x3 - a1*y1 - a2*y2 - a3*y3;
        x3 = x2; x2 = x1; x1 = x;
        y3 = y2; y2 = y1; y1 = y;
        return y;
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

class TW40Core
{
    float sampleRate = 48000.0f;
    float gain = kTW40Def[kGain];
    float bass = kTW40Def[kBass];
    float mid = kTW40Def[kMid];
    float treble = kTW40Def[kTreble];
    float pres = kTW40Def[kPres];

    Biquad inputHp;
    Biquad pickupLoad;
    Biquad brightShelf;
    Biquad normalBody;
    Biquad brightBody;
    Biquad interstageHp;
    Biquad cathodeFollowerLp;
    BassmanToneStack toneStack;
    Biquad stackMakeupLow;
    Biquad stackMakeupBody;
    Biquad phaseLowPass;
    Biquad presenceShelf;
    Biquad speakerHp;
    Biquad speakerThump;
    Biquad speakerLowMid;
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
        const float pushed = smoothstepRange(0.42f, 0.92f, gain);
        const float bright = clamp01(0.35f * treble + 0.25f * pres + 0.40f * gain);

        inputHp.setHighPass(sampleRate, 42.0f + 52.0f * g + 26.0f * pushed, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12800.0f - 1800.0f * pushed + 900.0f * treble, 0.64f);
        brightShelf.setHighShelf(sampleRate, 1500.0f + 1150.0f * treble, 0.70f,
                                 -1.2f + 5.2f * bright + 1.7f * pres);
        normalBody.setPeaking(sampleRate, 190.0f + 55.0f * bass, 0.72f,
                              0.7f + 2.6f * bass - 1.2f * pushed);
        brightBody.setPeaking(sampleRate, 720.0f + 420.0f * mid, 0.82f,
                              -1.0f + 2.8f * mid + 0.9f * bright);

        interstageHp.setHighPass(sampleRate, 58.0f + 74.0f * pushed + 42.0f * (1.0f - bass), 0.70f);
        cathodeFollowerLp.setLowPass(sampleRate, 8800.0f + 1700.0f * treble - 1600.0f * pushed, 0.64f);
        toneStack.update(treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f + 30.0f * bass, 0.72f,
                                   eqDb(bass, 4.8f) - 1.4f * pushed);
        stackMakeupBody.setPeaking(sampleRate, 470.0f + 170.0f * mid, 0.66f,
                                   -1.2f + 4.8f * mid + 1.2f * pushed);
        phaseLowPass.setLowPass(sampleRate, 6900.0f + 1400.0f * treble + 1100.0f * pres
                                            - 900.0f * pushed, 0.64f);
        presenceShelf.setHighShelf(sampleRate, 2600.0f + 900.0f * pres, 0.78f,
                                   -4.0f + 8.6f * pres + 0.9f * treble);

        speakerHp.setHighPass(sampleRate, 74.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 122.0f, 0.84f, 0.9f + 2.4f * bass);
        speakerLowMid.setPeaking(sampleRate, 330.0f + 90.0f * mid, 0.78f,
                                 0.8f + 1.9f * mid - 0.7f * pushed);
        speakerBite.setPeaking(sampleRate, 2650.0f + 520.0f * treble, 0.76f,
                               1.2f + 2.0f * treble + 1.2f * pres);
        speakerFizzNotch.setPeaking(sampleRate, 4800.0f + 360.0f * pres, 0.96f,
                                    -3.0f - 2.4f * pushed);
        speakerLp.setLowPass(sampleRate, 6200.0f + 2100.0f * treble + 900.0f * pres
                                         - 900.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset();
        pickupLoad.reset();
        brightShelf.reset();
        normalBody.reset();
        brightBody.reset();
        interstageHp.reset();
        cathodeFollowerLp.reset();
        toneStack.reset();
        stackMakeupLow.reset();
        stackMakeupBody.reset();
        phaseLowPass.reset();
        presenceShelf.reset();
        speakerHp.reset();
        speakerThump.reset();
        speakerLowMid.reset();
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
        toneStack.setSampleRate(sampleRate);
        reset();
    }

    void setGain(float v) { gain = clamp01(v); updateFilters(); }
    void setBass(float v) { bass = clamp01(v); updateFilters(); }
    void setMid(float v) { mid = clamp01(v); updateFilters(); }
    void setTreble(float v) { treble = clamp01(v); updateFilters(); }
    void setPres(float v) { pres = clamp01(v); updateFilters(); }

    float process(float in)
    {
        const float g = smoothstep(gain);
        const float pushed = smoothstepRange(0.42f, 0.92f, gain);
        const float brightMix = clamp01(0.25f + 0.35f * treble + 0.18f * pres);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = brightShelf.process(x);
        x = softClip(x * (1.04f + 0.08f * pushed)) * (0.96f - 0.04f * pushed);

        float normal = normalBody.process(x);
        normal = asymTube(normal, 0.82f + 2.0f * gain + 2.0f * g, 0.010f + 0.014f * gain);
        float brightPath = brightBody.process(x);
        brightPath = asymTube(brightPath, 0.90f + 2.25f * gain + 2.3f * g,
                              0.012f + 0.016f * gain);

        float y = normal * (1.0f - brightMix) + brightPath * brightMix;
        const float cleanLeak = 0.36f * (1.0f - smoothstepRange(0.28f, 0.78f, gain));
        y = y * (1.0f - cleanLeak) + x * cleanLeak;

        y = interstageHp.process(y);
        y = asymTube(y, 0.82f + 1.60f * gain + 2.2f * pushed, -0.006f - 0.010f * gain);
        y = cathodeFollowerLp.process(y);

        y = toneStack.process(y) * 1.70f;
        y = stackMakeupLow.process(y);
        y = stackMakeupBody.process(y);
        y = phaseLowPass.process(y);

        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0060f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.150f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.36f + 0.86f * gain + 0.50f * pushed));

        const float powerDrive = (0.92f + 1.55f * gain + 2.25f * pushed) * sagDrop;
        y = asymTube(y, powerDrive, 0.006f + 0.014f * (treble - bass) + 0.010f * pres);
        y = 0.86f * y + 0.14f * softClip(y * (1.65f + 1.35f * pushed));
        y *= 0.98f - 0.08f * sag;

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizzNotch.process(y);
        y = speakerLp.process(y);

        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.010f * std::fabs((pres - 0.5f) * 16.0f);
        const float level = (0.72f + 0.14f * (1.0f - gain)) /
            ((1.0f + 0.28f * gain + 0.32f * pushed) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class TW40Plugin : public Plugin
{
    TW40Core left;
    TW40Core right;
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
    TW40Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTW40Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "TW40"; }
    const char* getDescription() const override { return "Fender Bassman 5F6-A style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('T', 'w', '4', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kTW40Names[index];
        parameter.symbol = kTW40Symbols[index];
        parameter.ranges.min = kTW40Min[index];
        parameter.ranges.max = kTW40Max[index];
        parameter.ranges.def = kTW40Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TW40Plugin)
};

Plugin* createPlugin()
{
    return new TW40Plugin();
}

END_NAMESPACE_DISTRHO
