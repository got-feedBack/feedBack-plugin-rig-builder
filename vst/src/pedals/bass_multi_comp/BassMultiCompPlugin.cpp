/*
 * BassMultiComp - EBS MultiComp 2-style bass compressor.
 *
 * Real panel: Comp, Sens, Gain and Mode. Component references come from the
 * local Black Label MultiComp 2 schematic: BC850C input buffer, PMBFJ113 JFET
 * gain elements, TS922/TS925 audio filters, TL064 detector/control stages,
 * BAS28/BAT54 rectifiers and BC857B control buffers.
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
    float sens = kBassMultiCompDef[kSens];
    float gain = kBassMultiCompDef[kGain];
    float mode = kBassMultiCompDef[kMode];

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
    float controlMakeupGain = 1.0f;
    int modeIndex = 1;

    void update()
    {
        // P2A/P2B are a dual 50kB (linear) compression pot. Sens represents the
        // two detector-reference trims as one user-facing linear calibration.
        const float c = clamp01(comp);
        const float s = clamp01(sens);
        modeIndex = mode < 0.33f ? 0 : (mode < 0.66f ? 1 : 2);

        inputCap.setRC(sampleRate, 1000000.0f, 0.68e-6f);
        outputCap.setRC(sampleRate, 100000.0f, 10.0e-6f);

        // The HP/LP split is fixed by U4 and C24-C31. Sensitivity belongs to the
        // detector and must not retune the multiband crossover.
        const float crossover = 315.0f;
        splitLowA.setHz(sampleRate, crossover);
        splitLowB.setHz(sampleRate, crossover * 1.18f);
        highTone.setHz(sampleRate, 7800.0f - 1200.0f * c);
        tubeLow.setHz(sampleRate, 5200.0f);

        // Raising the detector reference makes the rectifier charge its timing
        // capacitor sooner. Model that as a faster effective attack so high
        // Sens does not merely lower the body while leaking the initial peak.
        const float sensAttackScale = 1.0f - 0.70f * s;
        lowDetector.setTimes(sampleRate, (7.0f - 4.0f * c) * sensAttackScale,
            240.0f - 160.0f * c);
        highDetector.setTimes(sampleRate, (3.2f - 1.2f * c) * sensAttackScale,
            130.0f - 80.0f * c);
        fullDetector.setTimes(sampleRate, (4.0f - 3.5f * c) * sensAttackScale,
            180.0f - 110.0f * c);

        lowThresholdDb = -11.0f - 21.0f * s - 4.0f * c;
        highThresholdDb = -13.0f - 19.0f * s - 3.0f * c;
        fullThresholdDb = -12.0f - 20.0f * s - 4.0f * c;
        lowRatio = 1.0f + 6.5f * c;
        highRatio = 1.0f + 5.4f * c;
        fullRatio = 1.0f + 6.8f * c;
        maxLowReduction = 12.0f * c;
        maxHighReduction = 10.0f * c;
        maxFullReduction = 10.0f * c;

        // The game keeps the real Gain pot pinned while automating Comp/Sens.
        // Compensate only the predictable control-dependent insertion loss so
        // clockwise rotation is heard as lower crest and greater sustain, not
        // as the whole instrument becoming quieter. This is static makeup and
        // cannot pump or chase the input envelope.
        const float compMakeupDb = -0.45f * c + 1.58f * c * c;
        const float sensMakeupDb = 2.0f * c * (0.80f * s + 1.40f * s * s);
        // Full-band Normal and TubeSim route the entire signal through one JFET,
        // so their insertion loss is greater than the recombined multiband path.
        const float modeMakeupDb = modeIndex == 0
            ? 5.70f * std::pow(c, 1.90f)
            : (modeIndex == 2 ? 4.30f * std::pow(c, 2.40f) : 0.0f);
        controlMakeupGain = dbToGain(std::fmin(10.0f,
            compMakeupDb + sensMakeupDb + modeMakeupDb));
        // Real panel Gain is an output/makeup control, not a mute. The previous
        // -18..+14 dB span made low settings drop below a usable compressor
        // level; keep the bottom audible and reserve the top for makeup.
        outputGain = dbToGain(-6.0f + 18.0f * audioTaper(gain));

        bas28Pair.setSpec(rbcomponents::diodeBAS28());
        bas28Pair.setSourceR(15000.0f - 6000.0f * s);
        bat54Pair.setSpec(rbcomponents::diodeBAT54());
        bat54Pair.setSourceR(12000.0f - 5200.0f * s);
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

    void setSens(float v)
    {
        sens = clamp01(v);
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

            const float lowDepth = 0.18f + 0.82f * c;
            const float highDepth = 0.14f + 0.68f * c;
            low = lowAmp.process(jfetAttenuator(low, lowGain, lowDepth), 2.0f);
            high = highAmp.process(jfetAttenuator(high, highGain, highDepth), 2.0f);
            high = highTone.process(high);
            y = low + high;
        }
        else
        {
            const float rect = std::fabs(bas28Pair.process(x * 2.20f));
            const float g = fullDetector.process(rect, fullThresholdDb, fullRatio, maxFullReduction);
            y = jfetAttenuator(x, g, 0.18f + 0.82f * c);

            if (modeIndex == 2)
            {
                const float warm = std::tanh(y * (1.35f + 0.85f * comp));
                y = 0.78f * y + 0.22f * warm;
                y = tubeLow.process(y);
            }
            else
            {
                y = highTone.process(y);
            }
        }

        y = outputCap.process(y * outputGain * controlMakeupGain);
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
        left.setSens(params[kSens]);
        right.setSens(params[kSens]);
        left.setGain(params[kGain]);
        right.setGain(params[kGain]);
        left.setMode(params[kMode]);
        right.setMode(params[kMode]);
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
    uint32_t getVersion() const override { return d_version(1, 3, 0); }
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
