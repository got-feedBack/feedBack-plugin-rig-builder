/*
 * Electric B600F — Acoustic B600H (600 W solid-state bass head), COMPONENT-LEVEL.
 *
 * From the Acoustic B450/B600H schematic + front panel (all solid-state):
 *   • INPUT  — Passive / Active jacks + Mute
 *   • PREAMP — Gain (op-amp gain, soft-clips = the Clip LED) and Volume (master)
 *   • NOTCH  — a sweepable band-reject filter (Frequency + On/Off)
 *   • EQ     — a fixed 6-band tone EQ: 40/120/350/800/2k/5k Hz, +/-15 dB
 *   • POWER  — 600 W Class-D (clean; the only nonlinearity is the input soft-clip)
 *
 * Pure solid-state: clean op-amp gain + RBJ EQ/notch + a soft ceiling. White-
 * boxed band frequencies from the panel.
 */
#include "DistrhoPlugin.hpp"
#include "ElectricParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
static inline float softClip(float x) { return std::tanh(x); }

// ── RBJ biquad — 6-band EQ peaks + sweepable notch ───────────────────────────
class Biquad {
    float b0=1, b1=0, b2=0, a1=0, a2=0, z1=0, z2=0;
public:
    void reset() { z1 = z2 = 0.f; }
    inline float process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void setPeak(float fc, float dB, float Q, float fs) {
        if (fc > fs*0.49f) fc = fs*0.49f;
        const float A = std::pow(10.f, dB / 40.f);
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha / A;
        b0 = (1 + alpha * A) / a0; b1 = (-2 * cw) / a0; b2 = (1 - alpha * A) / a0;
        a1 = (-2 * cw) / a0; a2 = (1 - alpha / A) / a0;
    }
    void setNotch(float fc, float Q, float fs) {
        if (fc > fs*0.49f) fc = fs*0.49f;
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha;
        b0 = 1.f / a0; b1 = (-2 * cw) / a0; b2 = 1.f / a0;
        a1 = (-2 * cw) / a0; a2 = (1 - alpha) / a0;
    }
    void setBypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

class ElectricChannel {
    float fs = 48000.f;
    Biquad notch, eq[kNumEq];
    float gain=1, volume=1; bool notchOn=false, mute=false;
public:
    void setSampleRate(float s){ fs=(s>0.f)?s:48000.f; }
    void reset(){ notch.reset(); for(int i=0;i<kNumEq;++i) eq[i].reset(); }

    void setParams(const float* p) {
        const float pad = (p[kActive] > 0.5f) ? 0.5f : 1.0f;
        gain = (0.5f + p[kGain] * 3.5f) * pad;
        notchOn = p[kNotchOn] > 0.5f;
        const float nfc = 50.f * std::pow(60.0f, p[kNotchFreq]);     // 50 Hz .. 3 kHz
        if (notchOn) notch.setNotch(nfc, 3.0f, fs); else notch.setBypass();
        for (int i=0;i<kNumEq;++i)
            eq[i].setPeak(kEqFreqs[i], (p[kFirstEq+i]-0.5f)*30.f, 0.9f, fs);   // +/-15 dB
        volume = p[kVolume] / 0.7f;
        mute = p[kMute] > 0.5f;
    }

    inline float process(float x) {
        if (mute) return 0.f;
        float s = x * gain;                             // clean op-amp gain (Class-D, hi-fi)
        s = notch.process(s);                           // sweepable notch
        for (int i=0;i<kNumEq;++i) s = eq[i].process(s); // 6-band EQ
        return s * volume;                              // Volume (master) into Class-D
    }
};

static constexpr float kElectricMakeup = 8.50f;   // tuned offline (~-14 dBFS @ noon)
static constexpr float kElectricLvl    = 0.249f;

class ElectricPlugin : public Plugin {
    ElectricChannel L, R;
    float fParams[kParamCount];
    void recalc(){ L.setParams(fParams); R.setParams(fParams); }
public:
    ElectricPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kElectricDef[i];
        const float sr=(float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "ElectricB600F"; }
    const char* getDescription() const override { return "Acoustic B600H 600W solid-state bass head — component-level model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'E', 'B'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kActive) p.hints |= kParameterIsBoolean;
        p.name = kElectricNames[i]; p.symbol = kElectricSymbols[i];
        p.ranges.min = kElectricMin[i]; p.ranges.max = kElectricMax[i]; p.ranges.def = kElectricDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i]=v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0]; const float* iR=in[1]; float* oL=out[0]; float* oR=out[1];
        for (uint32_t i=0;i<frames;++i){ oL[i]=rbAmpLvl(kElectricLvl*softClip(kElectricMakeup*L.process(iL[i]))*0.98f); oR[i]=rbAmpLvl(kElectricLvl*softClip(kElectricMakeup*R.process(iR[i]))*0.98f); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElectricPlugin)
};

Plugin* createPlugin() { return new ElectricPlugin(); }

END_NAMESPACE_DISTRHO
