/*
 * RingMod - Maestro/Oberheim RM-1A component-guided ring modulator.
 *
 * Signal path follows the local 26 March 1973 schematic: 50kA input Volume,
 * MC1458 input amplifier, LM741 Wien-bridge sine oscillator with 1N746 zener
 * amplitude stabilisation, MC1495 four-quadrant multiplier, 1N4148 detector +
 * 2N4360 P-channel JFET linear squelch, Modulation blend and MC1458 output.
 */
#include "DistrhoPlugin.hpp"
#include "RingModParams.h"
#include "../../_shared/analog_multiplier.hpp"
#include "../../_shared/opamp.hpp"
#include "../../_shared/oversampler.hpp"
#include "../../_shared/semiconductors.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 2.0f * kPi;
static constexpr float kVoltsPerUnit = 3.0f;

static inline float clamp01(float value)
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static inline float onePoleCoeff(float hz, float sampleRate)
{
    return 1.0f - std::exp(-kTwoPi * hz / sampleRate);
}

class DcBlocker
{
    float r = 0.999f;
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void set(float sampleRate, float hz)
    {
        r = std::exp(-kTwoPi * hz / sampleRate);
    }

    void reset()
    {
        x1 = y1 = 0.0f;
    }

    float process(float x)
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = std::fabs(y) < 1.0e-15f ? 0.0f : y;
        return y1;
    }
};

class OnePoleLowPass
{
    float a = 1.0f;
    float y = 0.0f;

public:
    void set(float sampleRate, float hz)
    {
        a = onePoleCoeff(hz, sampleRate);
    }

    void reset() { y = 0.0f; }

    float process(float x)
    {
        y += a * (x - y);
        if (std::fabs(y) < 1.0e-15f)
            y = 0.0f;
        return y;
    }
};

class RingModCore
{
    float sampleRate = 192000.0f;
    float pitchTarget = kRingModDef[kPitch];
    float modulationTarget = kRingModDef[kModulation];
    float volumeTarget = kRingModDef[kVolume];
    float pitch = kRingModDef[kPitch];
    float modulation = kRingModDef[kModulation];
    float volume = kRingModDef[kVolume];
    float modulateAmount = kRingModDef[kModulate];
    bool highRange = false;
    bool modulate = true;

    double phase = 0.0;
    float envelope = 0.0f;
    float squelch = 0.0f;
    float hardSquelch = 0.0f;
    float controlA = 0.0f;
    float envAttackA = 0.0f;
    float envReleaseA = 0.0f;
    float squelchAttackA = 0.0f;
    float squelchReleaseA = 0.0f;

    DcBlocker inputCoupling;
    OnePoleLowPass inputBandwidth;
    DcBlocker outputCoupling;
    OnePoleLowPass outputBandwidth;
    rbshared::LinearOpAmpStage inputAmp;
    rbshared::LinearOpAmpStage oscillatorAmp;
    rbshared::LinearOpAmpStage outputAmp;
    rbcomponents::Mc1495Multiplier multiplier;
    rbcomponents::JfetSpec squelchFet = rbcomponents::jfet2N4360();

    void updateCoefficients()
    {
        inputCoupling.set(sampleRate, 7.5f);
        inputBandwidth.set(sampleRate, 16800.0f); // 47k / 200p input network
        outputCoupling.set(sampleRate, 5.0f);
        outputBandwidth.set(sampleRate, 14500.0f);
        controlA = onePoleCoeff(18.0f, sampleRate);
        envAttackA = onePoleCoeff(78.0f, sampleRate);
        envReleaseA = onePoleCoeff(3.0f, sampleRate);
        squelchAttackA = onePoleCoeff(46.0f, sampleRate);
        squelchReleaseA = onePoleCoeff(0.8f, sampleRate);
    }

    float carrierFrequency() const
    {
        // Dual-gang 150kB Pitch pot with the schematic's 4.7k end stop. The
        // range switch parallels the second 15 nF timing capacitor in LOW.
        const float resistance = 4700.0f + 150000.0f * (1.0f - pitch);
        const float capacitance = highRange ? 15.0e-9f : 30.0e-9f;
        return 1.0f / (kTwoPi * resistance * capacitance);
    }

    float makeCarrier()
    {
        phase += static_cast<double>(carrierFrequency() / sampleRate);
        phase -= std::floor(phase);

        // The 741 Wien bridge is sine-only. Back-to-back 1N746 3.3 V zeners
        // plus the opposite diode's forward drop stabilise it near +/-4 V.
        const float sineVolts = 4.20f * std::sin(kTwoPi * static_cast<float>(phase));
        const float magnitude = std::fabs(sineVolts);
        const float limited = magnitude <= 3.60f
                            ? magnitude
                            : 3.60f + 0.40f * std::tanh((magnitude - 3.60f) / 0.40f);
        const float zenerVolts = sineVolts < 0.0f ? -limited : limited;
        return kVoltsPerUnit * oscillatorAmp.process(zenerVolts / kVoltsPerUnit, 3.0f);
    }

    float updateSquelch(float inputVolts)
    {
        // 1N4148 half-wave detector. The 5k trim is treated as calibrated to
        // suppress carrier bleed between notes without chopping normal decay.
        // The detector op-amp compensates most of the diode's forward drop;
        // applying the raw 0.54 V threshold here chopped quiet guitar notes.
        const float rectified = std::fmax(0.0f, std::fabs(inputVolts) - 0.08f);
        const float envA = rectified > envelope ? envAttackA : envReleaseA;
        envelope += envA * (rectified - envelope);

        const float target = clamp01((envelope - 0.002f) / 0.040f);
        const float gateA = target > squelch ? squelchAttackA : squelchReleaseA;
        squelch += gateA * (target - squelch);

        // Q1 is a P-channel shunt. RDS(on) is <=700 Ohm; when released, the
        // effective resistance rises toward one megohm against the 47k path.
        const float fetR = squelchFet.rdsOnMaxOhm
                         + squelch * squelch * (1000000.0f - squelchFet.rdsOnMaxOhm);
        const float divider = fetR / (47000.0f + fetR);
        hardSquelch = divider;
        return 0.15f + 0.85f * divider;
    }

public:
    RingModCore()
    {
        inputAmp.setSpec(rbshared::mc1458Spec());
        oscillatorAmp.setSpec(rbshared::lm741DualRailSpec());
        outputAmp.setSpec(rbshared::mc1458Spec());
        multiplier.setAxisResistors(18000.0f, 47000.0f);
        multiplier.setOutputScale(1.0f);
    }

    void setSampleRate(float newSampleRate)
    {
        sampleRate = newSampleRate > 4000.0f ? newSampleRate : 192000.0f;
        inputAmp.setSampleRate(sampleRate);
        oscillatorAmp.setSampleRate(sampleRate);
        outputAmp.setSampleRate(sampleRate);
        updateCoefficients();
        reset();
    }

    void reset()
    {
        phase = 0.0;
        envelope = 0.0f;
        squelch = 0.0f;
        hardSquelch = 0.0f;
        pitch = pitchTarget;
        modulation = modulationTarget;
        volume = volumeTarget;
        modulateAmount = modulate ? 1.0f : 0.0f;
        inputCoupling.reset();
        inputBandwidth.reset();
        outputCoupling.reset();
        outputBandwidth.reset();
        inputAmp.reset();
        oscillatorAmp.reset();
        outputAmp.reset();
    }

    void setParams(float newPitch, float newModulation, float newVolume,
                   bool newHighRange, bool newModulate)
    {
        pitchTarget = clamp01(newPitch);
        modulationTarget = clamp01(newModulation);
        volumeTarget = clamp01(newVolume);
        highRange = newHighRange;
        modulate = newModulate;
    }

    float process(float input)
    {
        pitch += controlA * (pitchTarget - pitch);
        modulation += controlA * (modulationTarget - modulation);
        volume += controlA * (volumeTarget - volume);
        modulateAmount += controlA * ((modulate ? 1.0f : 0.0f) - modulateAmount);

        // 50kA pot feeds the non-inverting MC1458 stage. The 47k/1k feedback
        // network gives 48x gain above its 20 uF cathode-equivalent corner.
        const float volumeTaper = volume * volume;
        float dry = inputCoupling.process(input);
        dry = inputBandwidth.process(dry);
        const float pre = inputAmp.process(dry * 0.35f * volumeTaper * 48.0f, 48.0f);
        const float signalVolts = kVoltsPerUnit * pre;

        const float carrierVolts = makeCarrier();
        const float multiplied = multiplier.process(carrierVolts, signalVolts);
        const float linearSquelch = updateSquelch(signalVolts);

        // Fixed null trims leave only a small, hardware-like residue. The JFET
        // squelch removes that residue and the multiplied tail between notes.
        const float signalPath = multiplied + 0.0040f * signalVolts;
        const float carrierResidue = 0.0015f * carrierVolts + 0.0010f;
        const float wetVolts = signalPath * linearSquelch
                             + carrierResidue * hardSquelch;

        // Modulation adds the multiplier return around a stable buffered dry
        // path. A replacement crossfade produced a level hole at noon and made
        // the guitar disappear as the effect increased.
        const float volumeReference = kRingModDef[kVolume] * kRingModDef[kVolume];
        dry *= volumeTaper / volumeReference;
        const float wet = 2.00f * wetVolts / kVoltsPerUnit;
        const float mix = modulation * modulateAmount;
        const float dryLevel = 1.0f - 0.12f * mix;
        const float wetLevel = 0.58f * mix;
        float output = dry * dryLevel + wet * wetLevel;
        output = outputCoupling.process(output);
        output = outputBandwidth.process(output);
        return outputAmp.process(output, 1.0f);
    }
};

} // namespace

class RingModPlugin : public Plugin
{
    static constexpr int kOS = rbshared::Oversampler4x::OS;
    RingModCore left;
    RingModCore right;
    rbshared::Oversampler4x osL;
    rbshared::Oversampler4x osR;
    float params[kParamCount];

    void applyAll()
    {
        const bool range = params[kPitchRange] >= 0.5f;
        const bool enabled = params[kModulate] >= 0.5f;
        left.setParams(params[kPitch], params[kModulation], params[kVolume], range, enabled);
        right.setParams(params[kPitch], params[kModulation], params[kVolume], range, enabled);
    }

public:
    RingModPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kRingModDef[i];
        const float rate = kOS * static_cast<float>(getSampleRate());
        left.setSampleRate(rate);
        right.setSampleRate(rate);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "RingMod"; }
    const char* getDescription() const override { return "Maestro/Oberheim RM-1A ring modulator"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'g', 'M', 'd'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= static_cast<uint32_t>(kParamCount))
            return;
        parameter.hints = kParameterIsAutomatable;
        if (index == static_cast<uint32_t>(kPitchRange)
            || index == static_cast<uint32_t>(kModulate))
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        parameter.name = kRingModNames[index];
        parameter.symbol = kRingModSymbols[index];
        parameter.ranges.min = kRingModMin[index];
        parameter.ranges.max = kRingModMax[index];
        parameter.ranges.def = kRingModDef[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < static_cast<uint32_t>(kParamCount) ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= static_cast<uint32_t>(kParamCount))
            return;
        params[index] = (index == static_cast<uint32_t>(kPitchRange)
                      || index == static_cast<uint32_t>(kModulate))
                      ? (value >= 0.5f ? 1.0f : 0.0f) : clamp01(value);
        applyAll();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        osL.reset();
        osR.reset();
        const float rate = kOS * static_cast<float>(newSampleRate);
        left.setSampleRate(rate);
        right.setSampleRate(rate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        float upL[kOS];
        float upR[kOS];
        for (uint32_t i = 0; i < frames; ++i)
        {
            osL.upsample(inputs[0][i], upL);
            osR.upsample(inputs[1][i], upR);
            for (int k = 0; k < kOS; ++k)
            {
                upL[k] = left.process(upL[k]);
                upR[k] = right.process(upR[k]);
            }
            outputs[0][i] = osL.downsample(upL);
            outputs[1][i] = osR.downsample(upR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RingModPlugin)
};

Plugin* createPlugin()
{
    return new RingModPlugin();
}

END_NAMESPACE_DISTRHO
