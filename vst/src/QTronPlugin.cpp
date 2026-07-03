/*
 * AutoSweep — envelope filter / auto-wah VST3 (DPF).
 * DSP core ported from the user's ESP32 "QTron" sketch; sample-rate-driven,
 * stereo, with continuous Attack + Release envelope times.
 */
#include "DistrhoPlugin.hpp"
#include "QTronParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

enum FilterMode { LP_MODE, BP_MODE, HP_MODE };

// ----------------------------------------------------------------------------
class QTron {
    float env_current, env_smooth, filter_freq;
    float y1_, y2_, x1, x2;
    FilterMode mode;
    float range_mod, peak_amount, mix_level, gain_level, boost_level;
    float attack_time, release_time, attack_coef, release_coef;
    float sampling_freq;
    static constexpr float two_pi = 6.28318530718f;
    const float freq_ranges[3][2] = { {70.f,1200.f}, {150.f,1500.f}, {300.f,2500.f} };

    void updateCoefs() {
        attack_coef  = 1.0f - expf(-1.0f / (attack_time  * sampling_freq));
        release_coef = 1.0f - expf(-1.0f / (release_time * sampling_freq));
    }
public:
    QTron() { sampling_freq = 48000.0f; reset(); }
    void reset() {
        env_current = env_smooth = 0.0f; filter_freq = 800.0f;
        y1_ = y2_ = x1 = x2 = 0.0f;
        mode = BP_MODE; range_mod = 0.9f; peak_amount = 4.0f;
        mix_level = 0.5f; gain_level = 12.0f; boost_level = 2.0f;
        attack_time = 0.004f; release_time = 0.04f; updateCoefs();
    }
    void setSampleRate(float sr) { sampling_freq = (sr > 0.0f) ? sr : 48000.0f; updateCoefs(); }
    void setMode(FilterMode m) { mode = m; }
    // Continuous envelope times (log): Attack 1..300 ms, Release 5..1000 ms.
    void setAttack(float v)  { attack_time  = 0.001f * powf(300.0f, v); updateCoefs(); }
    void setRelease(float v) { release_time = 0.005f * powf(200.0f, v); updateCoefs(); }
    void setRange(float v) { int i = (v < 0.10f) ? 0 : (v < 0.80f) ? 1 : 2; range_mod = (float)i / 2.0f; }
    void setPeak(float v) {
        switch (mode) {
            case LP_MODE: peak_amount = 0.5f + v * 5.0f;  break;
            case BP_MODE: peak_amount = 1.0f + v * 18.0f; break;
            case HP_MODE: peak_amount = 0.2f + v * 2.0f;  break;
        }
        if (peak_amount < 0.05f) peak_amount = 0.05f;
    }
    void setMix(float v)   { mix_level = v; }
    void setGain(float v)  { gain_level = 1.0f + v * 14.0f; }
    void setBoost(float v) { boost_level = 1.0f + v * 2.0f; }

    float processSample(float input) {
        const float input_abs = fabsf(input) * gain_level;
        if (input_abs > env_current) env_current += attack_coef  * (input_abs - env_current);
        else                          env_current += release_coef * (input_abs - env_current);
        env_smooth = 0.8f * env_smooth + 0.2f * env_current;

        const int ri = (int)(range_mod * 2.9f);
        const float fmin = freq_ranges[ri][0], fmax = freq_ranges[ri][1];
        filter_freq = fmin + (fmax - fmin) * env_smooth * env_smooth;

        const float omega = two_pi * filter_freq / sampling_freq;
        const float so = sinf(omega), co = cosf(omega);
        float alpha = so / (4.0f * peak_amount);
        float a0, a1, a2, b0, b1, b2;
        switch (mode) {
            case LP_MODE: alpha *= 1.0f; b0 = (1.0f - co) * 0.5f; b1 = 1.0f - co;  b2 = b0; break;
            case BP_MODE: alpha *= 1.8f; b0 = so / 2.0f;          b1 = 0.0f;       b2 = -so / 2.0f; break;
            case HP_MODE: alpha *= 0.6f; b0 = (1.0f + co) * 0.5f; b1 = -(1.0f + co); b2 = b0; break;
            default:      b0 = b1 = b2 = 0.0f; break;
        }
        a0 = 1.0f + alpha; a1 = -2.0f * co; a2 = 1.0f - alpha;
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

        const float out = b0 * input + b1 * x1 + b2 * x2 - a1 * y1_ - a2 * y2_;
        x2 = x1; x1 = input; y2_ = y1_; y1_ = out;
        const float dryLevel = 1.0f - 0.56f * mix_level;
        const float wetLevel = mix_level * boost_level * (0.58f + 0.14f * (1.0f - mix_level));
        return out * wetLevel + input * dryLevel;
    }
};

// ----------------------------------------------------------------------------
class QTronPlugin : public Plugin {
    QTron qL, qR;
    float fParams[kParamCount];

    void applyAll() {
        const FilterMode m = (fParams[kMode] < 0.5f) ? LP_MODE : (fParams[kMode] < 1.5f) ? BP_MODE : HP_MODE;
        QTron* qs[2] = { &qL, &qR };
        for (QTron* q : qs) {
            q->setMode(m);
            q->setAttack(fParams[kAttack]);   q->setRelease(fParams[kRelease]);
            q->setRange(fParams[kRange]);      q->setPeak(fParams[kPeak]);
            q->setMix(fParams[kMix]);          q->setGain(fParams[kGain]);
            q->setBoost(fParams[kBoost]);
        }
    }
public:
    QTronPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kQTronDef[i];
        const float sr = (float)getSampleRate();
        qL.setSampleRate(sr); qR.setSampleRate(sr);
        applyAll();
    }
protected:
    const char* getLabel()       const override { return "AutoSweep"; }
    const char* getDescription() const override { return "Envelope filter / auto-wah"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 1, 1); }
    int64_t     getUniqueId()    const override { return d_cconst('A', 'S', 'w', 'p'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == kMode) p.hints |= kParameterIsInteger;
        p.name   = kQTronNames[i];
        p.symbol = kQTronSymbols[i];
        p.ranges.min = kQTronMin[i];
        p.ranges.max = kQTronMax[i];
        p.ranges.def = kQTronDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.0f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; applyAll(); } }
    void  sampleRateChanged(double r) override { qL.setSampleRate((float)r); qR.setSampleRate((float)r); applyAll(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0];      float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = qL.processSample(iL[i]); oR[i] = qR.processSample(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QTronPlugin)
};

Plugin* createPlugin() { return new QTronPlugin(); }

END_NAMESPACE_DISTRHO
