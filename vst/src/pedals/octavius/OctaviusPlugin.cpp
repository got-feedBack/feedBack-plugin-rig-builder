/*
 * Octavius - Boss OC-2 style octave-down pedal for the game's Pedal_Octavius.
 *
 * Local reference: pedals/octavius.pdf. The real OC-2 uses flip-flop frequency
 * dividers, but a literal divider model sounds buzzy/synthetic and its
 * threshold pitch-detection glitches on real guitar (octave jumps, warble) —
 * "distorted and terrible". So this reuses the BassEmulator's smooth
 * time-domain pitch shifter (a delay read head running at half / quarter speed
 * with a windowed crossfade splice = exactly -12 / -24 semitones, no square
 * waves, no detector glitches). the game exposes Tone and Mix:
 *   - Tone : octave brightness + the OCT1(-1)/OCT2(-2) balance (low = darker,
 *            more sub; high = clearer single octave).
 *   - Mix  : dry / octave blend.
 */
#include "DistrhoPlugin.hpp"
#include "OctaviusParams.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    hz = std::fmax(10.0f, std::fmin(hz, sr * 0.45f));
    return 1.0f - std::exp(-2.0f * kPi * hz / sr);
}

// Smooth time-domain octave-down shifter (same method as BassEmulator). `adv`
// is the per-sample delay growth = (1 - ratio): 0.5 -> -12 st, 0.75 -> -24 st.
struct OctShifter
{
    static constexpr int kN = 32768;
    float buf[kN] {};
    int   writeIndex = 0;
    float phase = 0.0f;
    float window = 2400.0f;
    float splice = 256.0f;
    float splicePos = 256.0f;

    void reset()
    {
        std::memset(buf, 0, sizeof(buf));
        writeIndex = 0;
        phase = 0.0f;
        splicePos = splice;
    }

    void setWindow(float sr, float ms)
    {
        window = sr * ms * 0.001f;
        window = std::fmax(512.0f, std::fmin(window, (float)kN * 0.45f));
        splice = std::fmax(96.0f, std::fmin(sr * 0.0045f, window * 0.22f));
        while (phase >= window) phase -= window;
        if (splicePos > splice) splicePos = splice;
    }

    float readDelay(float delay) const
    {
        float rp = (float)writeIndex - delay;
        while (rp < 0.0f) rp += (float)kN;
        while (rp >= (float)kN) rp -= (float)kN;
        const int i0 = (int)rp;
        const int i1 = (i0 + 1) % kN;
        const float frac = rp - (float)i0;
        return buf[i0] + frac * (buf[i1] - buf[i0]);
    }

    float process(float in, float adv)
    {
        buf[writeIndex] = in;
        phase += adv;
        while (phase >= window)
        {
            phase -= window;
            splicePos = 0.0f;
        }
        const float newVoice = readDelay(phase + 2.0f);
        float shifted = newVoice;
        if (splicePos < splice)
        {
            const float oldVoice = readDelay(phase + window + 2.0f);
            const float t = splicePos / splice;
            const float xfade = 0.5f - 0.5f * std::cos(kPi * t);
            shifted = oldVoice * (1.0f - xfade) + newVoice * xfade;
            splicePos += 1.0f;
        }
        if (++writeIndex >= kN) writeIndex = 0;
        return shifted;
    }
};

} // namespace

class OctaviusCore
{
    float sampleRate = 48000.0f;
    float tone = kOctaviusDef[kTone];
    float mix = kOctaviusDef[kMix];

    OctShifter oct1;   // -12 st
    OctShifter oct2;   // -24 st

    // octave brightness (2-pole low-pass for a smooth, fizz-free voice)
    float oTone1 = 0.0f, oTone2 = 0.0f, toneA = 0.0f;
    // sub-octave shaping
    float subY = 0.0f, subA = 0.0f;
    // dry path softening + output high-pass
    float dryY = 0.0f, dryA = 0.0f;
    float hpX1 = 0.0f, hpY1 = 0.0f, hpA = 0.0f;

    void updateFilters()
    {
        oct1.setWindow(sampleRate, 36.0f);
        oct2.setWindow(sampleRate, 50.0f);

        const float t = smoothstep(tone);
        toneA = onePoleCoeffHz(520.0f + 3400.0f * t, sampleRate);   // octave brightness
        subA  = onePoleCoeffHz(150.0f + 360.0f * t, sampleRate);    // -2 oct kept dark/round
        dryA  = onePoleCoeffHz(8200.0f, sampleRate);

        const float dt = 1.0f / sampleRate;
        const float hpRc = 1.0f / (2.0f * kPi * 38.0f);
        hpA = hpRc / (hpRc + dt);
    }

    float lowPass(float x, float& z, float a) { z += a * (x - z); return z; }

    float highPass(float x)
    {
        const float y = hpA * (hpY1 + x - hpX1);
        hpX1 = x; hpY1 = y;
        return y;
    }

public:
    void reset()
    {
        oct1.reset(); oct2.reset();
        oTone1 = oTone2 = subY = dryY = hpX1 = hpY1 = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setTone(float v) { tone = clamp01(v); updateFilters(); }
    void setMix(float v)  { mix = clamp01(v); }

    float process(float in)
    {
        const float dry = lowPass(in, dryY, dryA);

        // Smooth pitch-shifted octaves (no dividers, no detector).
        const float o1 = oct1.process(in, 0.5f);    // -12 st
        const float o2raw = oct2.process(in, 0.75f); // -24 st
        const float o2 = lowPass(o2raw, subY, subA);  // keep the sub dark/round

        // OC-2 voice: clearer single -1 octave on high Tone, more -2 sub on
        // low Tone (the classic dark synth-bass character) — without the buzz.
        const float t = smoothstep(tone);
        const float oct1Level = 0.80f + 0.34f * t;
        const float oct2Level = 0.46f * (1.0f - 0.55f * t);
        float octave = o1 * oct1Level + o2 * oct2Level;

        // brightness: 2-pole low-pass so the octave is round, never fizzy.
        oTone1 += toneA * (octave - oTone1);
        oTone2 += toneA * (oTone1 - oTone2);
        octave = oTone2;

        // Mix: dry / octave blend (equal-ish power; Mix=0 -> dry only).
        const float m = mix <= 0.0001f ? 0.0f : clamp01(0.06f + 1.00f * mix);
        const float dryLevel = std::cos(m * 0.5f * kPi);
        const float wetLevel = std::sin(m * 0.5f * kPi) * 1.35f;

        float out = dry * dryLevel + octave * wetLevel;
        return highPass(out);
    }
};

class OctaviusPlugin : public Plugin
{
    OctaviusCore left;
    OctaviusCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setTone(params[kTone]);
        right.setTone(params[kTone]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
    }

public:
    OctaviusPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kOctaviusDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Octavius"; }
    const char* getDescription() const override { return "OC-2 style octave-down pedal"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 1); }
    int64_t getUniqueId() const override { return d_cconst('O', 'c', 'v', 's'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kOctaviusNames[index];
        parameter.symbol = kOctaviusSymbols[index];
        parameter.ranges.min = kOctaviusMin[index];
        parameter.ranges.max = kOctaviusMax[index];
        parameter.ranges.def = kOctaviusDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctaviusPlugin)
};

Plugin* createPlugin()
{
    return new OctaviusPlugin();
}

END_NAMESPACE_DISTRHO
