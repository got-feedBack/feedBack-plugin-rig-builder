/*
 * BassOverdrive - Darkglass Microtubes B3K style model for Bass_Pedal_BassOverdrive.
 *
 * Schematic blocks modeled here: J201 input buffer color, TL072 gain/filter
 * stages, CD4049UBE CMOS inverter clipping, 1N4148 shunt clipping/protection,
 * and the real Blend/Drive/Level plus Attack/Grunt switch controls.
 */
#include "DistrhoPlugin.hpp"
#include "BassOverdriveParams.h"
#include "../_shared/opamp.hpp"
#include "../_shared/semiconductors.hpp"
#include "../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float quantize3(float v)
{
    return v < 0.25f ? 0.0f : (v < 0.75f ? 0.5f : 1.0f);
}

static inline float onePoleCoef(float fc, float fs)
{
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

static inline float audioTaper(float v)
{
    return std::pow(clamp01(v), 2.05f);
}

static inline float j201Buffer(float x)
{
    return 0.965f * x + 0.035f * std::tanh(2.4f * x);
}

class B3K
{
    float fs = 48000.0f;
    float zGrunt = 0.0f;
    float zClean = 0.0f;
    float zPost = 0.0f;
    float zShelf = 0.0f;
    float zOutDc = 0.0f;
    float railMemory = 0.0f;
    float cGrunt = 0.0f;
    float cClean = 0.0f;
    float cPost = 0.0f;
    float cShelf = 0.0f;
    float cOutDc = 0.0f;
    float blend = 0.58f;
    float driveGain = 35.0f;
    float levelGain = 0.85f;
    float gruntMode = 0.5f;
    float attackMode = 0.5f;
    float gruntBoost = 1.0f;
    float shelfGain = 1.0f;
    rbshared::OpAmpStage preamp;
    rbshared::OpAmpStage recovery;
    rbcomponents::AntiParallelDiodePair siliconClamp;

    float cmosInverter(float x, float bias, float rail) const
    {
        const float v = (x + bias) / (rail > 0.1f ? rail : 0.1f);
        return -rail * std::tanh(1.55f * v);
    }

    float cmosCascade(float x)
    {
        const rbcomponents::DiodeSpec supply = rbcomponents::diode1N5817();
        railMemory += 0.0009f * (std::fabs(x) - railMemory);
        const float rail = 1.02f - 0.12f * rbcomponents::rbClamp(railMemory / supply.maxAbsV, 0.0f, 1.0f);

        // Two CMOS stages (was three) with gentler inter-stage gain. Three near-
        // saturated tanh stages compounded into a permanent square wave (crest ~1.01
        // at ANY drive = fuzz, no overdrive range). Two softer stages + a lighter
        // silicon clamp let the Drive knob sweep clean -> OD -> heavy distortion.
        float s = cmosInverter(x, 0.040f, rail);
        s = cmosInverter(s * 1.08f, -0.026f, rail);
        return siliconClamp.process(s * 0.82f);
    }

public:
    B3K()
    {
        preamp.setSpec(rbshared::tl072Spec());
        recovery.setSpec(rbshared::tl072Spec());
        siliconClamp.setSpec(rbcomponents::diode1N4148());
        siliconClamp.setSourceR(3000.0f);
    }

    void setSampleRate(float s)
    {
        fs = s > 1000.0f ? s : 48000.0f;
        preamp.setSampleRate(fs);
        recovery.setSampleRate(fs);
        recalcFixed();
    }

    void recalcFixed()
    {
        cClean = onePoleCoef(9500.0f, fs);
        cOutDc = onePoleCoef(14.0f, fs);
    }

    void setParams(float blendP, float drive, float level, float attack, float grunt)
    {
        blend = clamp01(blendP);
        driveGain = 0.7f + 22.0f * audioTaper(drive);
        // Output trim −3 dB (user request 2026-07-02): the B3K read too hot in
        // the mix. 2.05 × 10^(−3/20) = 1.451, scaling the whole Level range down
        // uniformly so the knob response is unchanged, just 3 dB quieter.
        levelGain = 1.451f * audioTaper(level);
        attackMode = quantize3(attack);
        gruntMode = quantize3(grunt);

        const float gruntHz = (gruntMode < 0.25f) ? 260.0f : (gruntMode < 0.75f ? 125.0f : 48.0f);
        gruntBoost = (gruntMode < 0.25f) ? 0.78f : (gruntMode < 0.75f ? 1.0f : 1.32f);
        cGrunt = onePoleCoef(gruntHz, fs);

        const float postHz = (attackMode < 0.25f) ? 3200.0f : (attackMode < 0.75f ? 5200.0f : 8200.0f);
        shelfGain = (attackMode < 0.25f) ? 0.58f : (attackMode < 0.75f ? 1.0f : 1.72f);
        cPost = onePoleCoef(postHz, fs);
        cShelf = onePoleCoef(1500.0f, fs);
    }

    float process(float x)
    {
        const float buffered = j201Buffer(x);
        zClean += cClean * (buffered - zClean);
        const float clean = zClean;

        zGrunt += cGrunt * (buffered - zGrunt);
        float d = (buffered - zGrunt) * gruntBoost;
        d = preamp.process(d * driveGain, 8.0f + driveGain * 0.18f);
        d = cmosCascade(d);

        zPost += cPost * (d - zPost);
        d = zPost;
        zShelf += cShelf * (d - zShelf);
        d = zShelf + (d - zShelf) * shelfGain;
        d = recovery.process(d, 2.0f);

        float out = clean * (1.0f - blend) + d * blend * 0.95f;
        zOutDc += cOutDc * (out - zOutDc);
        return (out - zOutDc) * levelGain;
    }
};

class BassOverdrivePlugin : public Plugin
{
    B3K L, R;
    rbshared::Oversampler4x osL, osR;
    static constexpr int kOS = rbshared::Oversampler4x::OS;
    float fParams[kParamCount];

    void recalc()
    {
        L.setParams(fParams[kBlend], fParams[kDrive], fParams[kLevel], fParams[kAttack], fParams[kGrunt]);
        R.setParams(fParams[kBlend], fParams[kDrive], fParams[kLevel], fParams[kAttack], fParams[kGrunt]);
    }

public:
    BassOverdrivePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            fParams[i] = kBassOverdriveDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(kOS * sr);
        R.setSampleRate(kOS * sr);
        recalc();
    }

protected:
    const char* getLabel() const override { return "BassOverdrive"; }
    const char* getDescription() const override { return "Darkglass B3K CMOS bass overdrive"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'B', 'O', 'd'); }

    void initParameter(uint32_t i, Parameter& p) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        p.hints = kParameterIsAutomatable;
        p.name = kBassOverdriveNames[i];
        p.symbol = kBassOverdriveSymbols[i];
        p.ranges.min = kBassOverdriveMin[i];
        p.ranges.max = kBassOverdriveMax[i];
        p.ranges.def = kBassOverdriveDef[i];
    }

    float getParameterValue(uint32_t i) const override
    {
        return (i < (uint32_t)kParamCount) ? fParams[i] : 0.0f;
    }

    void setParameterValue(uint32_t i, float v) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        fParams[i] = (i == (uint32_t)kAttack || i == (uint32_t)kGrunt) ? quantize3(v) : clamp01(v);
        recalc();
    }

    void sampleRateChanged(double r) override
    {
        osL.reset();
        osR.reset();
        L.setSampleRate(kOS * (float)r);
        R.setSampleRate(kOS * (float)r);
        recalc();
    }

    void run(const float** in, float** out, uint32_t frames) override
    {
        const float* iL = in[0];
        const float* iR = in[1];
        float* oL = out[0];
        float* oR = out[1];
        float ubL[kOS];
        float ubR[kOS];
        for (uint32_t i = 0; i < frames; ++i)
        {
            osL.upsample(iL[i], ubL);
            osR.upsample(iR[i], ubR);
            for (int k = 0; k < kOS; ++k)
            {
                ubL[k] = L.process(ubL[k]);
                ubR[k] = R.process(ubR[k]);
            }
            oL[i] = osL.downsample(ubL);
            oR[i] = osR.downsample(ubR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassOverdrivePlugin)
};

Plugin* createPlugin() { return new BassOverdrivePlugin(); }

END_NAMESPACE_DISTRHO
