/*
 * BassMultiComp - EBS MultiComp 2-style bass compressor.
 *
 * Real panel: Comp/Limit, Gain and Mode. Component references come from the
 * local Black Label MultiComp 2 schematic: BC850C input buffer, PMBFJ113 JFET
 * gain elements, TS922/TS925 audio filters, TL064 detector/control stages,
 * BAS28/BAT54 rectifiers and BC857B control buffers. Low/high detector
 * reference trims are internal service controls, not front-panel knobs.
 */
#include "DistrhoPlugin.hpp"
#include "BassMultiCompParams.h"
#include "../../_shared/opamp.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265358979323846f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float dn(float v)
{
    return std::fabs(v) < 1.0e-15f ? 0.0f : v;
}

static inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float ampToDb(float amp)
{
    return 20.0f * std::log10(std::fmax(amp, 1.0e-8f));
}

static inline float coeffMs(float ms, float sr)
{
    return 1.0f - std::exp(-1.0f / std::fmax(1.0f, ms * 0.001f * sr));
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 1.65f);
}

class RcHighPass
{
    float a = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void setRC(float sr, float rOhm, float cFarad)
    {
        const float rc = rOhm * cFarad;
        const float dt = 1.0f / (sr > 1000.0f ? sr : 48000.0f);
        a = rc / (rc + dt);
    }

    void reset()
    {
        x1 = y1 = 0.0f;
    }

    float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = dn(y);
        return y1;
    }
};

class OnePole
{
    float a = 0.0f;
    float y = 0.0f;

public:
    void setHz(float sr, float hz)
    {
        a = 1.0f - std::exp(-2.0f * kPi * hz / (sr > 1000.0f ? sr : 48000.0f));
    }

    void reset()
    {
        y = 0.0f;
    }

    float process(float x)
    {
        y += a * (x - y);
        y = dn(y);
        return y;
    }
};

struct BandDetector
{
    float env = 0.0f;
    float gain = 1.0f;
    float attackA = 0.0f;
    float releaseA = 0.0f;
    float gainAttackA = 0.0f;
    float gainReleaseA = 0.0f;

    void setTimes(float sr, float attackMs, float releaseMs)
    {
        attackA = coeffMs(attackMs, sr);
        releaseA = coeffMs(releaseMs, sr);
        gainAttackA = coeffMs(attackMs * 1.25f + 1.0f, sr);
        gainReleaseA = coeffMs(releaseMs * 0.88f + 12.0f, sr);
    }

    void reset()
    {
        env = 0.0f;
        gain = 1.0f;
    }

    float process(float x, float thresholdDb, float ratio, float maxReductionDb)
    {
        const float rect = std::fabs(x);
        const float envA = rect > env ? attackA : releaseA;
        env += envA * (rect - env);
        env = dn(env);

        const float over = ampToDb(env) - thresholdDb;
        const float kneeDb = 5.0f;
        float reductionDb = 0.0f;
        if (over > -0.5f * kneeDb)
        {
            const float eff = over < 0.5f * kneeDb
                ? ((over + 0.5f * kneeDb) * (over + 0.5f * kneeDb)) / (2.0f * kneeDb)
                : over;
            reductionDb = std::fmin(maxReductionDb, eff * (1.0f - 1.0f / ratio));
        }

        const float wanted = dbToGain(-reductionDb);
        const float a = wanted < gain ? gainAttackA : gainReleaseA;
        gain += a * (wanted - gain);
        gain = std::fmax(0.035f, std::fmin(1.0f, gain));
        return gain;
    }
};

static inline float bc850Input(float x)
{
    return 0.90f * x + 0.10f * std::tanh(1.45f * x) / 1.45f;
}

static inline float jfetAttenuator(float x, float gain, float controlDepth)
{
    // Q2/Q3 are shunt PMBFJ113 voltage-controlled resistors. The old model put
    // Rds in series with 47k, so even a nominal 23 dB detector reduction became
    // less than 0.6 dB of audio reduction. Convert the control state into the
    // equivalent shunt resistance seen after R17/R25 instead.
    const float detectorGain = std::fmax(0.025f, std::fmin(1.0f, gain));
    const float targetGain = std::fmax(0.025f,
        1.0f - clamp01(controlDepth) * (1.0f - detectorGain));
    const float seriesR = 47000.0f;
    const float rds = targetGain >= 0.9995f
        ? 100000000.0f
        : std::fmax(100.0f, seriesR * targetGain / (1.0f - targetGain));
    const float divider = rds / (seriesR + rds);

    // A conducting JFET channel is mildly nonlinear, but it is an attenuator,
    // not the TubeSim distortion stage.
    const float channelDrive = 1.0f + 0.32f * (1.0f - divider);
    const float shaped = std::tanh(x * channelDrive) / channelDrive;
    return divider * (0.96f * x + 0.04f * shaped);
}

} // namespace

class BassMultiCompCore
{
    float sampleRate = 48000.0f;
    float comp = kBassMultiCompDef[kComp];
    float lowTrim = kBassMultiCompDef[kLowTrim];
    float gain = kBassMultiCompDef[kGain];
    float mode = kBassMultiCompDef[kMode];
    float highTrim = kBassMultiCompDef[kHighTrim];

    RcHighPass inputCap;
    RcHighPass outputCap;
    OnePole splitLowA;
    OnePole splitLowB;
    OnePole highTone;
    OnePole tubeLow;
    rbshared::OpAmpStage inputAmp;
    rbshared::OpAmpStage lowAmp;
    rbshared::OpAmpStage highAmp;
    rbcomponents::AntiParallelDiodePair bas28Pair;
    rbcomponents::AntiParallelDiodePair bat54Pair;
    BandDetector lowDetector;
    BandDetector highDetector;
    BandDetector fullDetector;

    float outputGain = 1.0f;
    float lowThresholdDb = -18.0f;
    float highThresholdDb = -20.0f;
    float fullThresholdDb = -18.0f;
    float lowRatio = 2.8f;
    float highRatio = 2.5f;
    float fullRatio = 3.0f;
    float maxLowReduction = 14.0f;
    float maxHighReduction = 12.0f;
    float maxFullReduction = 16.0f;
    int modeIndex = 1;

    void update()
    {
        // P2A/P2B are the dual 50kB Comp/Limit pot. The two 500k detector
        // reference trims are independent internal adjustments in the schematic.
        const float c = clamp01(comp);
        const float lowT = clamp01(lowTrim);
        const float highT = clamp01(highTrim);
        modeIndex = mode < 0.33f ? 0 : (mode < 0.66f ? 1 : 2);

        inputCap.setRC(sampleRate, 1000000.0f, 0.68e-6f);
        outputCap.setRC(sampleRate, 100000.0f, 10.0e-6f);

        // The HP/LP split is fixed by U4 and C24-C31. Sensitivity belongs to the
        // detector and must not retune the multiband crossover.
        const float crossover = 315.0f;
        splitLowA.setHz(sampleRate, crossover);
        splitLowB.setHz(sampleRate, crossover * 1.18f);
        highTone.setHz(sampleRate, 7800.0f);
        tubeLow.setHz(sampleRate, 5200.0f);

        // Comp/Limit changes compression ratio from 1:1 to 5:1. It does not
        // retune the crossover, detector references or rectifier impedance.
        // Linearly interpolate the actual gain-reduction slope rather than the
        // ratio number. A direct 1+4*c mapping spends most of the knob travel
        // near the limiting end and makes the upper half barely change.
        const float ratio = 1.0f / (1.0f - 0.80f * c);
        lowDetector.setTimes(sampleRate, 6.0f, 220.0f);
        highDetector.setTimes(sampleRate, 3.0f, 125.0f);
        fullDetector.setTimes(sampleRate, 4.0f, 170.0f);

        lowThresholdDb = -9.0f - 18.0f * lowT;
        highThresholdDb = -11.0f - 18.0f * highT;
        fullThresholdDb = -10.0f - 9.0f * (lowT + highT);
        lowRatio = highRatio = fullRatio = ratio;
        maxLowReduction = 18.0f;
        maxHighReduction = 16.0f;
        maxFullReduction = 18.0f;

        // Real panel Gain is an output/makeup control, not a mute. The previous
        // -18..+14 dB span made low settings drop below a usable compressor
        // level; keep the bottom audible and reserve the top for makeup.
        outputGain = dbToGain(-6.0f + 18.0f * audioTaper(gain));

        bas28Pair.setSpec(rbcomponents::diodeBAS28());
        bas28Pair.setSourceR(12000.0f);
        bat54Pair.setSpec(rbcomponents::diodeBAT54());
        bat54Pair.setSourceR(9400.0f);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        inputAmp.setSpec(rbshared::ts921Spec());
        lowAmp.setSpec(rbshared::ts925Spec());
        highAmp.setSpec(rbshared::ts925Spec());
        inputAmp.setSampleRate(sampleRate);
        lowAmp.setSampleRate(sampleRate);
        highAmp.setSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        inputCap.reset();
        outputCap.reset();
        splitLowA.reset();
        splitLowB.reset();
        highTone.reset();
        tubeLow.reset();
        inputAmp.reset();
        lowAmp.reset();
        highAmp.reset();
        bas28Pair.reset();
        bat54Pair.reset();
        lowDetector.reset();
        highDetector.reset();
        fullDetector.reset();
        update();
    }

    void setComp(float v)
    {
        comp = clamp01(v);
        update();
    }

    void setLowTrim(float v)
    {
        lowTrim = clamp01(v);
        update();
    }

    void setGain(float v)
    {
        gain = clamp01(v);
        update();
    }

    void setMode(float v)
    {
        mode = clamp01(v);
        update();
    }

    void setHighTrim(float v)
    {
        highTrim = clamp01(v);
        update();
    }

    float process(float in)
    {
        const float c = clamp01(comp);
        float x = inputCap.process(bc850Input(in));
        x = inputAmp.process(x, 1.35f);

        float y = x;
        if (modeIndex == 1)
        {
            float low = splitLowA.process(x);
            low = splitLowB.process(low);
            float high = x - low;

            const float lowRect = std::fabs(bat54Pair.process(low * 2.15f));
            const float highRect = std::fabs(bas28Pair.process(high * 2.30f));
            const float lowGain = lowDetector.process(lowRect, lowThresholdDb, lowRatio, maxLowReduction);
            const float highGain = highDetector.process(highRect, highThresholdDb, highRatio, maxHighReduction);

            low = lowAmp.process(jfetAttenuator(low, lowGain, 1.0f), 2.0f);
            high = highAmp.process(jfetAttenuator(high, highGain, 1.0f), 2.0f);
            high = highTone.process(high);
            y = low + high;
        }
        else
        {
            const float rect = std::fabs(bas28Pair.process(x * 2.20f));
            const float g = fullDetector.process(rect, fullThresholdDb, fullRatio, maxFullReduction);
            y = jfetAttenuator(x, g, 1.0f);

            if (modeIndex == 2)
            {
                // TubeSim adds a low-passed, asymmetric harmonic residual. The
                // dry fundamental stays intact, so the mode warms the bass
                // without turning into a dark low-pass filter.
                const float drive = 4.0f + 3.0f * c;
                const float bias = 0.055f;
                const float warm = (std::tanh((y + bias) * drive)
                    - std::tanh(bias * drive)) / drive;
                const float harmonics = tubeLow.process(warm - y);
                y += 0.24f * harmonics;
            }
            else
            {
                y = highTone.process(y);
            }
        }

        y = outputCap.process(y * outputGain);
        return std::tanh(y * 1.04f) * 0.98f;
    }
};

class BassMultiCompPlugin : public Plugin
{
    BassMultiCompCore left;
    BassMultiCompCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setComp(params[kComp]);
        right.setComp(params[kComp]);
        left.setLowTrim(params[kLowTrim]);
        right.setLowTrim(params[kLowTrim]);
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setMode(params[kMode]);
        right.setMode(params[kMode]);
        left.setHighTrim(params[kHighTrim]);
        right.setHighTrim(params[kHighTrim]);
    }

public:
    BassMultiCompPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kBassMultiCompDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "BassMultiComp"; }
    const char* getDescription() const override { return "EBS MultiComp 2-style bass compressor"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 4, 0); }
    int64_t getUniqueId() const override { return d_cconst('B', 'm', 'c', 'p'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kBassMultiCompNames[index];
        parameter.symbol = kBassMultiCompSymbols[index];
        parameter.ranges.min = kBassMultiCompMin[index];
        parameter.ranges.max = kBassMultiCompMax[index];
        parameter.ranges.def = kBassMultiCompDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassMultiCompPlugin)
};

Plugin* createPlugin()
{
    return new BassMultiCompPlugin();
}

END_NAMESPACE_DISTRHO
