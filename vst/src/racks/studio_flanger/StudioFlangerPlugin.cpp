/*
 * StudioFlanger - Boss RBF-10 Micro Rack style flanger for Rack_StudioFlanger.
 *
 * Reference: racks/RBF-10_service_notes.pdf.  The RBF-10 is a 1985 analog rack
 * flanger around M5218L/IR9022 op-amp conditioning, MN3102 clock driver,
 * MN3204 512-stage BBD, polarity-selectable feedback, and D+E / D-E stereo
 * outputs.  the game exposes five rack controls, so we keep the public
 * parameter set but map it onto the closest real controls:
 *   Rate  -> RATE 250K, 100 ms..16 s LFO span
 *   Depth -> DEPTH 50K modulation depth
 *   Regen -> F.BACK LEVEL 50K center-click feedback amount
 *   Tone  -> internal post-BBD/filter brightness trim
 *   Mix   -> BALANCE 50K direct/effect wet balance
 */
#include "DistrhoPlugin.hpp"
#include "StudioFlangerParams.h"
#include "../../pedals/_shared/FlangerComponents.h"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static rbflanger::FlangerVoicing rbf10Voicing()
{
    rbflanger::FlangerVoicing v;
    v.bbd = rbflanger::mn3204Spec();
    v.opamp = rbshared::m5218Spec();

    // Service notes: delay time 0.4 ms - 3.2 ms, MN3204 clock adjusted around
    // 80 kHz.  Keep the modeled sweep inside that range so the comb notches
    // stay in the tight rack-flanger zone instead of becoming chorus.
    v.minDelayMs = 0.40f;
    v.maxDelayMs = 3.20f;
    v.flangeRangeMaxMs = 3.20f;
    v.minRateHz = 0.0625f; // 16 s
    v.maxRateHz = 10.0f;   // 100 ms

    v.inputHpHz = 24.0f;
    v.inputLpHz = 11800.0f;
    v.bbdLpHz = 6800.0f;
    v.outputLpHz = 7600.0f;
    v.colorHpHz = 1750.0f;
    v.delaySlewHz = 120.0f;

    v.feedbackMax = 0.78f;
    v.feedbackSign = -1.0f; // rear switch in NOR for the game-facing model
    v.wetSign = -1.0f;
    v.dryLevel = 0.91f;
    v.wetLevel = 0.72f;
    v.dryDucking = 0.16f;
    v.wetMixMin = 0.05f;
    v.wetMixScale = 0.95f;
    v.lfoTriangle = 0.78f;
    v.depthBase = 0.02f;
    v.depthScale = 0.66f;

    v.driveMinDb = -1.0f;
    v.driveMaxDb = 1.4f;
    v.outputMinDb = -0.8f;
    v.outputMaxDb = 0.6f;
    v.compander = 0.40f;
    return v;
}

static inline float clamp01(float v)
{
    return rbmod::clamp01(v);
}

static inline float balanceWet(float mix)
{
    // RBF-10 BALANCE is direct at CCW and effect at CW.  Keep a small wet leak
    // at low values so Rack_StudioFlanger presets never collapse to hard bypass.
    return 0.08f + 0.92f * rbmod::smoothstep(mix);
}

static inline float feedbackAmount(float regen)
{
    // Hardware has a center-click feedback level.  the game's Regen is a
    // unipolar amount, so preserve 0..1 preset behavior and cap below runaway.
    return rbmod::smoothstep(regen) * 0.92f;
}

static inline float componentRateControl(float rackRate)
{
    // Existing RS mapping writes normalized values as if this control were
    // linear 0.1..6 Hz.  AnalogBbdFlanger expects a log/audio-taper control,
    // so invert that curve here and keep old presets at the intended speed.
    const float hz = 0.10f + 5.90f * rbmod::clamp01(rackRate);
    const float t = std::log(hz / 0.0625f) / std::log(10.0f / 0.0625f);
    return std::pow(rbmod::clamp01(t), 1.0f / 1.7f);
}

static inline float manualFromTone(float tone)
{
    // The rack has a MANUAL knob but the game's rack surface gives us Tone.
    // Use Tone as the manual/filter trim: darker settings sit at slightly longer
    // delay, brighter settings at shorter delay for tighter high notches.
    return rbmod::clamp(0.28f + 0.54f * rbmod::smoothstep(tone), 0.0f, 1.0f);
}

static inline float driveFromTone(float tone)
{
    return 0.30f + 0.18f * rbmod::smoothstep(tone);
}

static inline float outputFromBalance(float mix)
{
    return 0.52f - 0.08f * rbmod::smoothstep(mix);
}

} // namespace

class StudioFlangerPlugin : public Plugin
{
    rbflanger::AnalogBbdFlanger left;
    rbflanger::AnalogBbdFlanger right;
    float fParams[kParamCount];

    void applyAll()
    {
        const float manual = manualFromTone(fParams[kTone]);
        const float wet = balanceWet(fParams[kMix]);
        const float fb = feedbackAmount(fParams[kRegen]);
        const float drive = driveFromTone(fParams[kTone]);
        const float out = outputFromBalance(fParams[kMix]);

        const float rate = componentRateControl(fParams[kRate]);

        left.setControls(manual, fParams[kDepth], rate, fb, wet, drive, out, false);
        right.setControls(manual, fParams[kDepth], rate, fb, wet, drive, out, false);
    }

public:
    StudioFlangerPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            fParams[i] = kStudioFlangerDef[i];

        const rbflanger::FlangerVoicing voice = rbf10Voicing();
        left.setVoicing(voice);
        right.setVoicing(voice);
        left.setPhaseOffset(0.00f);
        right.setPhaseOffset(0.25f); // rack D+E/D-E stereo spread
        left.setSampleRate(48000.0f);
        right.setSampleRate(48000.0f);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "StudioFlanger"; }
    const char* getDescription() const override { return "Boss RBF-10 MN3204 BBD rack flanger"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'F', 'l', '1'); }

    void initParameter(uint32_t i, Parameter& p) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        p.hints = kParameterIsAutomatable;
        p.name = kStudioFlangerNames[i];
        p.symbol = kStudioFlangerSymbols[i];
        p.ranges.min = kStudioFlangerMin[i];
        p.ranges.max = kStudioFlangerMax[i];
        p.ranges.def = kStudioFlangerDef[i];
    }

    float getParameterValue(uint32_t i) const override
    {
        return (i < (uint32_t)kParamCount) ? fParams[i] : 0.0f;
    }

    void setParameterValue(uint32_t i, float v) override
    {
        if (i >= (uint32_t)kParamCount)
            return;
        fParams[i] = clamp01(v);
        applyAll();
    }

    void sampleRateChanged(double r) override
    {
        left.setSampleRate((float)r);
        right.setSampleRate((float)r);
        applyAll();
    }

    void run(const float** in, float** out, uint32_t frames) override
    {
        float* oL = out[0];
        float* oR = out[1];

        for (uint32_t i = 0; i < frames; ++i)
        {
            const rbmod::StereoInputPair feed = rbmod::stereoPedalFeeds(in[0][i], in[1][i]);
            oL[i] = left.process(feed.left);
            oR[i] = right.process(feed.right);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioFlangerPlugin)
};

Plugin* createPlugin()
{
    return new StudioFlangerPlugin();
}

END_NAMESPACE_DISTRHO
