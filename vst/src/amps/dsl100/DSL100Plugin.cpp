/*
 * DSL100 - Marshall DSL100H / JCM2000-style amp for Rocksmith's
 * Amp_MarshallDSL100H.
 *
 * Local references:
 *   amps/Marshall DSL100/JCM2-60-02 (2003) iss9.pdf
 *   amps/Marshall DSL100/JCM2-61-00 (2001) iss5.pdf
 *   amps/Marshall DSL100/JCM2-62-02 (2001) iss3.pdf
 *   amps/Marshall DSL100/DSL50-100 manual (2004).pdf
 */
#include "DistrhoPlugin.hpp"
#include "DSL100Params.h"
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
    return std::fmax(20.0f, std::fmin(hz, sr * 0.45f));
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
    return (y - correction) / (1.0f - 0.30f * std::fabs(correction));
}

static inline float eqDb(float v, float rangeDb)
{
    return (clamp01(v) - 0.5f) * 2.0f * rangeDb;
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
        const float inv = 1.0f / na0;
        b0 = nb0 * inv;
        b1 = nb1 * inv;
        b2 = nb2 * inv;
        a1 = na1 * inv;
        a2 = na2 * inv;
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

class DSL100Core
{
    float sampleRate = 48000.0f;
    float gain = kDSL100Def[kGain];
    float bass = kDSL100Def[kBass];
    float mid = kDSL100Def[kMid];
    float treble = kDSL100Def[kTreble];
    float pres = kDSL100Def[kPres];
    float res = kDSL100Def[kRes];

    Biquad inputHp, inputLp, brightShelf;
    Biquad cleanBody, crunchBody, ultraTight, ultraBite;
    Biquad interHp, interLp;
    Biquad toneBass, toneMid, toneTreble, toneShiftMid, toneShiftBite;
    Biquad phaseHp, phaseLp, presenceShelf, resonanceShelf, resonancePeak;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizzNotch, speakerLp;
    DcBlock dcBlock;
    float sag = 0.0f;

    void updateFilters()
    {
        const float crunch = smoothstepRange(0.30f, 0.72f, gain);
        const float ultra = smoothstepRange(0.58f, 0.96f, gain);
        const float toneShift = smoothstepRange(0.68f, 0.98f, gain) * (1.0f - mid) * 0.92f;
        const float deep = smoothstep(res);

        inputHp.setHighPass(sampleRate, 54.0f + 58.0f * ultra + 28.0f * (1.0f - bass), 0.70f);
        inputLp.setLowPass(sampleRate, 14800.0f - 3100.0f * ultra + 1100.0f * treble, 0.64f);
        brightShelf.setHighShelf(sampleRate, 1050.0f + 1150.0f * treble, 0.70f,
                                 -1.8f + 5.0f * treble + 1.6f * pres + 1.0f * crunch);
        cleanBody.setPeaking(sampleRate, 410.0f + 130.0f * mid, 0.76f,
                             -0.8f + 2.6f * mid + 1.4f * bass);
        crunchBody.setPeaking(sampleRate, 760.0f + 220.0f * mid, 0.82f,
                              -1.6f + 4.8f * mid + 1.9f * crunch);
        ultraTight.setLowShelf(sampleRate, 145.0f + 30.0f * bass, 0.76f,
                               -3.8f * ultra + 3.2f * bass + 1.2f * deep);
        ultraBite.setPeaking(sampleRate, 1850.0f + 620.0f * treble, 0.82f,
                             0.4f + 3.4f * treble + 2.2f * ultra + 1.0f * pres);
        interHp.setHighPass(sampleRate, 70.0f + 86.0f * ultra + 34.0f * (1.0f - bass), 0.71f);
        interLp.setLowPass(sampleRate, 9600.0f + 1200.0f * treble - 1800.0f * ultra, 0.64f);

        toneBass.setLowShelf(sampleRate, 118.0f + 42.0f * bass, 0.72f,
                             eqDb(bass, 7.0f) - 1.6f * ultra + 2.2f * deep);
        toneMid.setPeaking(sampleRate, 610.0f + 310.0f * mid, 0.70f + 0.28f * toneShift,
                           eqDb(mid, 7.4f) + 1.2f * crunch - 5.8f * toneShift);
        toneTreble.setHighShelf(sampleRate, 1900.0f + 1050.0f * treble, 0.74f,
                                eqDb(treble, 7.2f) + 1.0f * ultra);
        toneShiftMid.setPeaking(sampleRate, 820.0f + 180.0f * treble, 1.08f, -7.0f * toneShift);
        toneShiftBite.setPeaking(sampleRate, 2550.0f + 530.0f * treble, 0.82f,
                                 2.4f * toneShift + 0.6f * ultra);

        phaseHp.setHighPass(sampleRate, 76.0f + 32.0f * ultra, 0.72f);
        phaseLp.setLowPass(sampleRate, 7900.0f + 1400.0f * treble + 700.0f * pres - 1450.0f * ultra, 0.65f);
        presenceShelf.setHighShelf(sampleRate, 2700.0f + 850.0f * pres, 0.78f,
                                   -4.2f + 8.7f * pres + 1.3f * treble);
        resonanceShelf.setLowShelf(sampleRate, 95.0f + 38.0f * res, 0.78f,
                                   -2.2f + 7.4f * deep + 1.8f * ultra);
        resonancePeak.setPeaking(sampleRate, 118.0f + 28.0f * res, 0.92f,
                                 0.4f + 4.8f * deep + 1.4f * bass);

        speakerHp.setHighPass(sampleRate, 76.0f + 10.0f * ultra, 0.72f);
        speakerThump.setPeaking(sampleRate, 125.0f + 20.0f * res, 0.88f,
                                0.8f + 2.3f * bass + 2.2f * deep);
        speakerLowMid.setPeaking(sampleRate, 415.0f + 155.0f * mid, 0.76f,
                                 0.5f + 2.5f * mid - 2.2f * toneShift);
        speakerBite.setPeaking(sampleRate, 2850.0f + 620.0f * treble, 0.78f,
                               1.2f + 2.4f * treble + 1.9f * pres + 0.8f * ultra);
        speakerFizzNotch.setPeaking(sampleRate, 5050.0f + 460.0f * pres, 1.10f,
                                    -3.5f - 3.7f * ultra - 0.8f * toneShift);
        speakerLp.setLowPass(sampleRate, 6400.0f + 2050.0f * treble + 850.0f * pres - 1200.0f * ultra, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset(); brightShelf.reset();
        cleanBody.reset(); crunchBody.reset(); ultraTight.reset(); ultraBite.reset();
        interHp.reset(); interLp.reset();
        toneBass.reset(); toneMid.reset(); toneTreble.reset(); toneShiftMid.reset(); toneShiftBite.reset();
        phaseHp.reset(); phaseLp.reset(); presenceShelf.reset(); resonanceShelf.reset(); resonancePeak.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset();
        speakerFizzNotch.reset(); speakerLp.reset(); dcBlock.reset();
        sag = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setGain(float v) { gain = clamp01(v); updateFilters(); }
    void setBass(float v) { bass = clamp01(v); updateFilters(); }
    void setMid(float v) { mid = clamp01(v); updateFilters(); }
    void setTreble(float v) { treble = clamp01(v); updateFilters(); }
    void setPres(float v) { pres = clamp01(v); updateFilters(); }
    void setRes(float v) { res = clamp01(v); updateFilters(); }

    float process(float in)
    {
        const float cleanW = 1.0f - smoothstepRange(0.22f, 0.50f, gain);
        const float ultraW = smoothstepRange(0.58f, 0.96f, gain);
        float crunchW = 1.0f - cleanW - ultraW;
        if (crunchW < 0.0f)
            crunchW = 0.0f;
        const float sum = cleanW + crunchW + ultraW + 1.0e-6f;
        const float cleanMix = cleanW / sum;
        const float crunchMix = crunchW / sum;
        const float ultraMix = ultraW / sum;

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = brightShelf.process(x);
        x = softClip(x * (1.05f + 0.18f * gain + 0.15f * ultraMix));

        float clean = cleanBody.process(x);
        clean = 0.58f * clean + 0.42f * asymTube(clean, 0.86f + 1.05f * gain, 0.004f);

        float crunch = crunchBody.process(x);
        crunch = asymTube(crunch, 1.28f + 3.05f * gain, 0.012f + 0.010f * gain);
        crunch = 0.78f * crunch + 0.22f * softClip(crunch * (1.7f + 1.0f * gain));

        float ultra = ultraTight.process(x);
        ultra = ultraBite.process(ultra);
        ultra = asymTube(ultra, 1.85f + 5.10f * gain, 0.018f + 0.012f * pres);
        ultra = asymTube(ultra, 1.25f + 3.90f * gain, -0.014f - 0.008f * gain);
        ultra = 0.70f * ultra + 0.30f * softClip(ultra * (2.0f + 1.9f * gain));

        float y = clean * cleanMix + crunch * crunchMix + ultra * ultraMix;
        y = interHp.process(y);
        y = interLp.process(y);

        const float extraCascade = smoothstepRange(0.48f, 0.90f, gain);
        const float cascaded = asymTube(y, 1.02f + 2.15f * gain + 2.15f * ultraMix,
                                        -0.006f - 0.010f * ultraMix);
        y = y * (1.0f - 0.56f * extraCascade) + cascaded * (0.56f * extraCascade);

        y = toneBass.process(y);
        y = toneMid.process(y);
        y = toneTreble.process(y);
        y = toneShiftMid.process(y);
        y = toneShiftBite.process(y);
        y = phaseHp.process(y);
        y = phaseLp.process(y);

        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0045f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.125f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.44f + 1.08f * gain + 0.82f * ultraMix));

        const float powerDrive = (0.96f + 1.55f * gain + 1.90f * ultraMix) * sagDrop;
        y = asymTube(y, powerDrive, 0.004f + 0.014f * (pres - bass) + 0.008f * res);
        y = 0.82f * y + 0.18f * softClip(y * (1.8f + 1.25f * ultraMix));
        y *= 0.98f - 0.07f * sag;

        y = presenceShelf.process(y);
        y = resonanceShelf.process(y);
        y = resonancePeak.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizzNotch.process(y);
        y = speakerLp.process(y);

        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.011f * std::fabs((pres - 0.5f) * 16.0f)
            + 0.010f * std::fabs((res - 0.5f) * 16.0f);
        const float level = (0.74f + 0.12f * (1.0f - gain)) /
            ((1.0f + 0.32f * gain + 0.64f * ultraMix) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class DSL100Plugin : public Plugin
{
    DSL100Core left;
    DSL100Core right;
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
        left.setRes(params[kRes]);
        right.setRes(params[kRes]);
    }

public:
    DSL100Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDSL100Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "DSL100"; }
    const char* getDescription() const override { return "Marshall DSL100H / JCM2000 style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', '1', '0', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDSL100Names[index];
        parameter.symbol = kDSL100Symbols[index];
        parameter.ranges.min = kDSL100Min[index];
        parameter.ranges.max = kDSL100Max[index];
        parameter.ranges.def = kDSL100Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSL100Plugin)
};

Plugin* createPlugin()
{
    return new DSL100Plugin();
}

END_NAMESPACE_DISTRHO
