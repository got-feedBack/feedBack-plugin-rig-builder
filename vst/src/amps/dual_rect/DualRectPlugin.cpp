/*
 * DualRect - Mesa-Boogie Dual Rectifier Red-channel style amp for Rocksmith's
 * Amp_CA100.
 *
 * Local reference:
 *   amps/Dual Rectifier (Cali_100)/boogie_dualrectifier.pdf
 *
 * The schematic shows cascaded 12AX7 gain stages, switched Red/Orange gain
 * paths, Red/Orange tone stacks, a presence/feedback network, and a 6L6/EL34
 * power section. Rocksmith exposes one Gain plus Bass/Mid/Treble/Pres; the
 * curated NAM set for Amp_CA100 is Mesa Dual Rectifier Red G2/G5/G8, so Gain
 * morphs the Red channel through those gain ranges.
 */
#include "DistrhoPlugin.hpp"
#include "DualRectParams.h"
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

class DualRectCore
{
    float sampleRate = 48000.0f;
    float gain = kDualRectDef[kGain];
    float bass = kDualRectDef[kBass];
    float mid = kDualRectDef[kMid];
    float treble = kDualRectDef[kTreble];
    float pres = kDualRectDef[kPres];

    Biquad inputHp;
    Biquad inputLp;
    Biquad inputBright;
    Biquad redTight;
    Biquad redBody;
    Biquad cascadeHp;
    Biquad cascadeLp;
    Biquad gainBright;
    Biquad toneBass;
    Biquad toneMid;
    Biquad toneTreble;
    Biquad modernScoop;
    Biquad modernEdge;
    Biquad phaseHp;
    Biquad phaseLp;
    Biquad presenceShelf;
    Biquad feedbackLow;
    Biquad speakerHp;
    Biquad speakerThump;
    Biquad speakerLowMid;
    Biquad speakerBite;
    Biquad speakerFizzNotch;
    Biquad speakerLp;
    DcBlock dcBlock;

    float sag = 0.0f;

    void updateFilters()
    {
        const float g = smoothstep(gain);
        const float redG5 = smoothstepRange(0.30f, 0.70f, gain);
        const float redG8 = smoothstepRange(0.64f, 0.98f, gain);
        const float modern = smoothstepRange(0.56f, 0.94f, gain) * (0.72f + 0.28f * pres);

        inputHp.setHighPass(sampleRate, 58.0f + 70.0f * redG8 + 34.0f * (1.0f - bass), 0.70f);
        inputLp.setLowPass(sampleRate, 15000.0f - 3300.0f * redG8 + 1200.0f * treble, 0.64f);
        inputBright.setHighShelf(sampleRate, 980.0f + 1220.0f * treble, 0.70f,
                                 -1.5f + 4.2f * treble + 1.8f * pres + 1.1f * redG5);

        redTight.setLowShelf(sampleRate, 128.0f + 24.0f * bass, 0.74f,
                             -5.2f * redG8 - 1.8f * redG5 + 3.0f * bass);
        redBody.setPeaking(sampleRate, 520.0f + 260.0f * mid, 0.78f,
                           -1.4f + 4.4f * mid + 1.3f * redG5 - 2.4f * modern);
        cascadeHp.setHighPass(sampleRate, 78.0f + 92.0f * redG8 + 32.0f * (1.0f - bass), 0.71f);
        cascadeLp.setLowPass(sampleRate, 9300.0f + 1100.0f * treble - 2100.0f * redG8, 0.64f);
        gainBright.setPeaking(sampleRate, 1650.0f + 560.0f * treble, 0.82f,
                              0.6f + 2.2f * treble + 1.8f * redG8 + 0.9f * pres);

        toneBass.setLowShelf(sampleRate, 112.0f + 40.0f * bass, 0.72f,
                             eqDb(bass, 7.6f) - 1.9f * redG8);
        toneMid.setPeaking(sampleRate, 610.0f + 290.0f * mid, 0.72f,
                           eqDb(mid, 7.8f) - 2.7f * modern + 1.0f * redG5);
        toneTreble.setHighShelf(sampleRate, 1950.0f + 1050.0f * treble, 0.74f,
                                eqDb(treble, 7.6f) + 0.9f * redG8);
        modernScoop.setPeaking(sampleRate, 820.0f + 130.0f * treble, 0.98f,
                               -4.4f * modern * (1.0f - 0.45f * mid));
        modernEdge.setPeaking(sampleRate, 2800.0f + 600.0f * treble, 0.84f,
                              0.8f + 2.8f * modern + 1.5f * pres);

        phaseHp.setHighPass(sampleRate, 72.0f + 36.0f * redG8, 0.72f);
        phaseLp.setLowPass(sampleRate, 7900.0f + 1400.0f * treble + 850.0f * pres
                                      - 1500.0f * redG8, 0.65f);
        presenceShelf.setHighShelf(sampleRate, 2500.0f + 900.0f * pres, 0.78f,
                                   -4.8f + 9.4f * pres + 1.2f * treble + 0.8f * modern);
        feedbackLow.setLowShelf(sampleRate, 115.0f + 28.0f * bass, 0.74f,
                                1.8f * bass + 2.5f * modern - 1.2f * pres);

        speakerHp.setHighPass(sampleRate, 78.0f + 12.0f * redG8, 0.72f);
        speakerThump.setPeaking(sampleRate, 128.0f, 0.88f,
                                0.6f + 2.4f * bass + 1.6f * modern);
        speakerLowMid.setPeaking(sampleRate, 390.0f + 150.0f * mid, 0.78f,
                                 0.3f + 2.2f * mid - 1.5f * redG8);
        speakerBite.setPeaking(sampleRate, 3000.0f + 620.0f * treble, 0.78f,
                               1.1f + 2.6f * treble + 1.8f * pres + 1.1f * redG8);
        speakerFizzNotch.setPeaking(sampleRate, 5200.0f + 430.0f * pres, 1.12f,
                                    -3.9f - 3.6f * redG8);
        speakerLp.setLowPass(sampleRate, 6300.0f + 2050.0f * treble + 850.0f * pres
                                         - 1250.0f * redG8, 0.66f);
        (void)g;
    }

public:
    void reset()
    {
        inputHp.reset();
        inputLp.reset();
        inputBright.reset();
        redTight.reset();
        redBody.reset();
        cascadeHp.reset();
        cascadeLp.reset();
        gainBright.reset();
        toneBass.reset();
        toneMid.reset();
        toneTreble.reset();
        modernScoop.reset();
        modernEdge.reset();
        phaseHp.reset();
        phaseLp.reset();
        presenceShelf.reset();
        feedbackLow.reset();
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
        const float redG5 = smoothstepRange(0.30f, 0.70f, gain);
        const float redG8 = smoothstepRange(0.64f, 0.98f, gain);
        const float modern = smoothstepRange(0.56f, 0.94f, gain) * (0.72f + 0.28f * pres);

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = inputBright.process(x);
        x = redTight.process(x);
        x = redBody.process(x);
        x = softClip(x * (1.04f + 0.18f * g));

        float y = asymTube(x, 1.20f + 3.40f * gain + 1.85f * redG5, 0.010f + 0.010f * gain);
        y = gainBright.process(y);
        y = cascadeHp.process(y);
        y = asymTube(y, 1.38f + 3.90f * gain + 2.35f * redG8, -0.010f - 0.012f * redG8);
        y = cascadeLp.process(y);

        const float extraStage = smoothstepRange(0.48f, 0.90f, gain);
        float z = asymTube(y, 1.15f + 3.35f * gain + 2.95f * redG8, 0.012f + 0.010f * pres);
        z = 0.74f * z + 0.26f * softClip(z * (2.0f + 1.7f * redG8));
        y = y * (1.0f - 0.66f * extraStage) + z * (0.66f * extraStage);

        y = toneBass.process(y);
        y = toneMid.process(y);
        y = toneTreble.process(y);
        y = modernScoop.process(y);
        y = modernEdge.process(y);

        y = phaseHp.process(y);
        y = phaseLp.process(y);

        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0040f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.105f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagAmount = 0.22f + 0.62f * (1.0f - modern) + 0.22f * bass;
        const float sagDrop = 1.0f / (1.0f + sag * sagAmount * (0.65f + 0.75f * gain));

        const float powerDrive = (0.90f + 1.35f * gain + 1.70f * redG8) * sagDrop;
        y = asymTube(y, powerDrive, 0.004f + 0.014f * (pres - bass));
        y = 0.84f * y + 0.16f * softClip(y * (1.65f + 1.10f * modern));
        y *= 0.99f - 0.06f * sag;

        y = presenceShelf.process(y);
        y = feedbackLow.process(y);
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
            + 0.011f * std::fabs((pres - 0.5f) * 16.0f);
        const float level = (0.72f + 0.10f * (1.0f - gain)) /
            ((1.0f + 0.32f * gain + 0.58f * redG8 + 0.18f * modern) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class DualRectPlugin : public Plugin
{
    DualRectCore left;
    DualRectCore right;
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
    DualRectPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDualRectDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "DualRect"; }
    const char* getDescription() const override { return "Mesa-Boogie Dual Rectifier Red channel style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', 'R', 'C', 'T'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDualRectNames[index];
        parameter.symbol = kDualRectSymbols[index];
        parameter.ranges.min = kDualRectMin[index];
        parameter.ranges.max = kDualRectMax[index];
        parameter.ranges.def = kDualRectDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DualRectPlugin)
};

Plugin* createPlugin()
{
    return new DualRectPlugin();
}

END_NAMESPACE_DISTRHO
