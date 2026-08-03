/*
 * BobFilter — Moog MF-101 Lowpass Filter (envelope filter), rebuilt.
 *
 * The game's "Bob Filter" gear has an envelope-filter panel (Sens / Attack /
 * Release / Mix / Filter), i.e. the Moog envelope filter: an envelope follower
 * sweeping a 4-pole transistor-ladder lowpass with resonance. The previous
 * model here was the MF-105 MuRF (a pattern-SEQUENCED static filter bank) —
 * a different pedal that steps a rhythm instead of tracking the pick, which
 * is why it never sounded like an envelope filter in game tones.
 *
 * Architecture (MF-101 signal path):
 *   Drive -> full-wave detector (Follow Rate, bipolar Amount)
 *         -> 4-stage tanh transistor ladder (LM3046-style, 4x oversampled)
 *            with resonance feedback up to the edge of self-oscillation
 *         -> 2-pole / 4-pole output tap (the real panel switch)
 *         -> Mix -> Output.
 */
#include "DistrhoPlugin.hpp"
#include "BobFilterParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float dn(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.65f);
}

static inline float onePoleCoeffMs(float ms, float sr)
{
    const float samples = std::fmax(1.0f, ms * 0.001f * sr);
    return 1.0f - std::exp(-1.0f / samples);
}

static inline float softRail(float x)
{
    // TL072 output swing on the MF-101's +/-5 V rails. This is deliberately
    // gentler than a final tanh waveshaper: most of the nonlinearity belongs in
    // the input pair and the LM3046 ladder cells.
    const float a = std::fabs(x);
    if (a <= 0.78f)
        return x;
    const float over = (a - 0.78f) / 0.21f;
    const float y = 0.78f + 0.21f * std::tanh(over);
    return std::copysign(y, x);
}

static inline float resonanceLevelTrimDb(float resonance)
{
    // Measured insertion loss of the MF-101 feedback/output network. The upper
    // segment rises sharply as P4 approaches self-oscillation.
    const float r = clamp01(resonance);
    if (r <= 0.40f)
        return -5.30f + (r / 0.40f) * 3.55f;
    if (r <= 0.70f)
        return -1.75f + ((r - 0.40f) / 0.30f) * 1.75f;
    if (r <= 0.80f)
        return ((r - 0.70f) / 0.10f) * 2.00f;
    return 2.00f + ((r - 0.80f) / 0.20f) * 2.00f;
}

// Four differential-pair cells corresponding to the three LM3046 arrays in
// the supplied schematic. Oversampling is performed by the caller; the state
// update is the Huovilainen nonlinear one-pole form, including saturation at
// every differential pair rather than a generic clipper after the filter.
class LadderFilter
{
    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;

public:
    void reset() { s1 = s2 = s3 = s4 = 0.0f; }

    // g = 1-exp(-2*pi*fc/sr) per oversampled step. k approaches the ladder's
    // self-oscillation boundary near the upper end of the Resonance pot.
    // Returns via out2 (2-pole tap) and out4 (4-pole tap).
    inline void process(float x, float g, float k, float& out2, float& out4)
    {
        const float in = x - k * s4;
        s1 += g * (std::tanh(in) - std::tanh(s1));
        s2 += g * (std::tanh(s1) - std::tanh(s2));
        s3 += g * (std::tanh(s2) - std::tanh(s3));
        s4 += g * (std::tanh(s3) - std::tanh(s4));
        s1 = dn(s1); s2 = dn(s2); s3 = dn(s3); s4 = dn(s4);
        out2 = s2;
        out4 = s4;
    }
};

} // namespace

class BobFilterCore
{
    float sampleRate = 48000.0f;
    float drive = kBobFilterDef[kDrive];
    float output = kBobFilterDef[kOutput];
    float cutoff = kBobFilterDef[kCutoff];
    float resonance = kBobFilterDef[kResonance];
    float envAmount = kBobFilterDef[kEnvelope];
    float followRate = kBobFilterDef[kFollowRate];
    float mix = kBobFilterDef[kMix];
    float poles = kBobFilterDef[kPoles];

    LadderFilter ladder;
    float env = 0.0f;
    float atkA = 0.0f, relA = 0.0f, relStrongA = 0.0f;
    float dcIn = 0.0f;
    float previousLadderIn = 0.0f;
    float twoPoleDryLp = 0.0f;
    float resonancePairLp = 0.0f;
    float fcSmooth = 1000.0f;
    float fcA = 0.0f;

    void updateCoeffs()
    {
        // The MF-101S Follow Rate moves both detector time constants. Slow
        // holds the CV between notes; Fast gives the short, vocal pick quack.
        const float r = clamp01(followRate);
        const float atkMs = 24.0f * std::pow(0.015f, r);
        const float relMs = 620.0f * std::pow(0.0065f, r);
        atkA = onePoleCoeffMs(atkMs, sampleRate);
        relA = onePoleCoeffMs(relMs, sampleRate);
        relStrongA = onePoleCoeffMs(relMs * (1.0f + 6.0f * r), sampleRate);
        fcA = onePoleCoeffMs(1.5f, sampleRate);
    }

public:
    void reset()
    {
        ladder.reset();
        env = dcIn = previousLadderIn = twoPoleDryLp = resonancePairLp = 0.0f;
        fcSmooth = 1000.0f;
        updateCoeffs();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setDrive(float v)     { drive = clamp01(v); }
    void setOutput(float v)    { output = clamp01(v); }
    void setCutoff(float v)    { cutoff = clamp01(v); }
    void setResonance(float v) { resonance = clamp01(v); }
    void setEnvelope(float v)  { envAmount = clamp01(v); }
    void setFollowRate(float v){ followRate = clamp01(v); updateCoeffs(); }
    void setMix(float v)       { mix = clamp01(v); }
    void setPoles(float v)     { poles = clamp01(v); }

    float process(float in)
    {
        // Input coupling capacitor and 1 M input load, about 7 Hz.
        const float dcA = 1.0f - std::exp(-2.0f * kPi * 7.0f / sampleRate);
        dcIn += dcA * (in - dcIn);
        float x = in - dcIn;

        // DRIVE: TL072 non-inverting stage, 1 + P2(50 kA)/R8(1.5 k).
        const float dg = 1.0f + 33.0f * audioTaper(drive);
        const float driven = softRail(x * dg * 0.29f) / 0.29f;

        // The schematic sends the post-Drive signal through C25 into the
        // precision full-wave rectifier (U3/D3-D6), then C4 and the rate network
        // form the control envelope. A small diode knee keeps noise from opening
        // the filter between notes.
        const float detector = std::fmax(0.0f, std::fabs(driven) * 0.29f - 0.010f);
        const float positiveAmount = std::fmax(0.0f, 2.0f * envAmount - 1.0f);
        const float releaseStrength = clamp01(env / 0.45f) * positiveAmount;
        const float dynamicReleaseA = relA + releaseStrength * (relStrongA - relA);
        env += (detector > env ? atkA : dynamicReleaseA) * (detector - env);
        env = dn(env);
        const float detectorCv = env / (env + 0.35f);
        // D7/Q3 and the exponential current-control input do not turn the
        // rectified voltage into a linear 0..1 control. The measured response
        // keeps weak notes substantially more closed than strong attacks.
        const float e01 = std::pow(detectorCv, 1.80f);

        // P6 is the real 20 Hz..12 kHz log Cutoff pot. Amount is bipolar:
        // -10 closes on the pick, 0 is static, +10 opens on the pick.
        const float base = 20.0f * std::pow(600.0f, clamp01(cutoff));
        const float signedAmount = 2.0f * envAmount - 1.0f;
        // The CV summing network is asymmetric around zero: positive Amount
        // spans the full opening sweep, while negative Amount closes by about
        // three octaves over the guitar-level detector range.
        const float lowCutoffCvCompression = signedAmount > 0.0f
            ? (0.35f + 0.65f * std::pow(clamp01(base / 1000.0f), 0.70f))
            : 1.0f;
        const float sweepOctaves = (signedAmount >= 0.0f ? 23.0f : 10.2f)
                                 * lowCutoffCvCompression;
        const float oct = sweepOctaves * signedAmount * e01;
        // The panel calibration marks the electrical CV setpoint. The
        // resonant peak of the loaded four-pole audio path sits lower.
        float fc = 0.72f * base * std::pow(2.0f, oct);
        fc *= std::pow(2.0f, 10.0f * std::fmax(0.0f, resonance - 0.70f));
        const float fcMax = std::fmin(12000.0f, sampleRate * 0.42f);
        if (fc > fcMax) fc = fcMax;
        if (fc < 20.0f) fc = 20.0f;
        fcSmooth += fcA * (fc - fcSmooth);

        // P4/Q5 return the 4-pole output to the ladder input. The useful range
        // is close to linear; the last quarter approaches self-oscillation.
        const float resonanceAboveSeven = std::fmax(0.0f, resonance - 0.70f);
        const float k = std::fmin(4.15f,
            4.05f * std::pow(clamp01(resonance), 1.08f)
            + 8.0f * resonanceAboveSeven);

        // Four-times oversampled ladder with linear interpolation into the
        // nonlinear cells. Scaling corresponds to the LM3046 differential-pair
        // voltage range; no generic post-filter distortion is added.
        static constexpr int kOS = 4;
        const float g = 1.0f - std::exp(-2.0f * kPi * fcSmooth / (kOS * sampleRate));
        const float ladderIn = driven * 0.22f;
        float o2 = 0.0f, o4 = 0.0f;
        for (int os = 1; os <= kOS; ++os)
        {
            const float t = static_cast<float>(os) / static_cast<float>(kOS);
            ladder.process(previousLadderIn + t * (ladderIn - previousLadderIn),
                           g, k, o2, o4);
        }
        previousLadderIn = ladderIn;

        const float wet4 = o4 / 0.22f;
        const float wet2Base = o2 / 0.22f;
        // The schematic's 2-pole output is an active summing tap, not simply
        // the second integrator exposed raw. Its dry feed cancels at DC against
        // the resonant 4-pole return and restores the broad upper response seen
        // in the supplied 2-pole references.
        const float twoPoleHpA = 1.0f - std::exp(
            -2.0f * kPi * std::fmax(300.0f, base * 2.0f) / sampleRate);
        twoPoleDryLp += twoPoleHpA * (driven - twoPoleDryLp);
        const float twoPoleHighFeed = driven - twoPoleDryLp;
        const float wet2 = 0.62f * (0.65f * wet2Base + 2.30f * twoPoleHighFeed);
        float wet = poles >= 0.5f ? wet4 : wet2;
        // Above 7 the Q5 feedback pair enters its nonlinear region. Preserve
        // the ladder fundamental and return only the high-passed differential-
        // pair residue; this produces the upper-mid rasp/self-oscillation edge
        // in the Resonance=8 reference without brightening lower settings.
        const float resonancePairAmount = clamp01((resonance - 0.70f) / 0.10f);
        if (resonancePairAmount > 0.0f)
        {
            const float saturated = std::tanh(3.0f * wet) / 3.0f;
            const float residue = saturated - wet;
            const float pairA = 1.0f - std::exp(-2.0f * kPi * 700.0f / sampleRate);
            resonancePairLp += pairA * (residue - resonancePairLp);
            wet -= 3.0f * resonancePairAmount * (residue - resonancePairLp);
        }

        // P5 mixes the post-Drive dry path with the selected ladder tap. P3 is
        // the output level; softRail represents the final TL072 headroom only.
        float y = driven * (1.0f - mix) + wet * mix;
        y *= 0.10f + 1.25f * audioTaper(output);
        const float cutoffDistance = std::log2(std::fmax(20.0f, base) / 1000.0f);
        const float dynamicMidMakeup = 1.45f * std::fmax(0.0f, signedAmount)
                                     * std::exp(-0.25f * cutoffDistance * cutoffDistance);
        const float lowCutoffWeight = clamp01(
            std::log2(1000.0f / std::fmax(20.0f, base)) / std::log2(10.0f));
        const float highCutoffWeight = clamp01(
            std::log2(std::fmax(1000.0f, base) / 1000.0f) / std::log2(12.0f));
        const float strongAttack = std::fmax(0.0f, e01 - 0.30f);
        const float envelopeCurve = 20.0f * (e01 - 0.30f)
                                  + 40.0f * strongAttack * strongAttack;
        const float envelopeCurveWeight = signedAmount >= 0.0f
            ? (0.45f + 0.55f * lowCutoffWeight + 0.65f * resonancePairAmount)
              * (1.0f - highCutoffWeight)
            : 0.15f;
        const float envelopeLevelDb = signedAmount * envelopeCurveWeight * envelopeCurve;
        y *= std::pow(10.0f,
                      (resonanceLevelTrimDb(resonance) + dynamicMidMakeup
                       + envelopeLevelDb - 0.60f * highCutoffWeight) / 20.0f);
        const float lowCutoffEnvelopeGain = std::pow(0.06f + 0.94f * e01, 1.30f)
                                           * std::pow(10.0f, 4.0f / 20.0f);
        y *= 1.0f + lowCutoffWeight * (lowCutoffEnvelopeGain - 1.0f);
        if (signedAmount < 0.0f)
            y *= std::pow(10.0f, -3.1f * (-signedAmount) / 20.0f);
        return softRail(y);
    }
};

class BobFilterPlugin : public Plugin
{
    BobFilterCore left;
    BobFilterCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setDrive(params[kDrive]);
        right.setDrive(params[kDrive]);
        left.setOutput(params[kOutput]);
        right.setOutput(params[kOutput]);
        left.setCutoff(params[kCutoff]);
        right.setCutoff(params[kCutoff]);
        left.setResonance(params[kResonance]);
        right.setResonance(params[kResonance]);
        left.setEnvelope(params[kEnvelope]);
        right.setEnvelope(params[kEnvelope]);
        left.setFollowRate(params[kFollowRate]);
        right.setFollowRate(params[kFollowRate]);
        left.setMix(params[kMix]);
        right.setMix(params[kMix]);
        left.setPoles(params[kPoles]);
        right.setPoles(params[kPoles]);
    }

public:
    BobFilterPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBobFilterDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BobFilter"; }
    const char* getDescription() const override { return "Moog MF-101 style envelope filter"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(3, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'f', 'l', 't'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == static_cast<uint32_t>(kPoles))
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        parameter.name = kBobFilterNames[index];
        parameter.symbol = kBobFilterSymbols[index];
        parameter.ranges.min = kBobFilterMin[index];
        parameter.ranges.max = kBobFilterMax[index];
        parameter.ranges.def = kBobFilterDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BobFilterPlugin)
};

Plugin* createPlugin()
{
    return new BobFilterPlugin();
}

END_NAMESPACE_DISTRHO
