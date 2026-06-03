/*
 * StudioChorus — Boss RCE-10 Chorus Ensemble model for Rack_StudioChorus.
 *
 * A lush stereo chorus: a modulated delay line per channel with TWO LFO voices
 * (offset in phase) for the "ensemble" thickness, the left/right LFOs spread
 * apart by the Stereo control, and a high-pass + low-pass shaping the wet voice
 * (LoFilter / HiFilter). Rate is taken in Hz to match the Rocksmith data.
 *   Rate     -> 0.1 .. 6 Hz LFO
 *   Depth    -> 0 .. ~7 ms modulation swing
 *   Delay    -> 5 .. 28 ms base delay
 *   LoFilter -> wet high-pass 25 Hz .. 500 Hz
 *   HiFilter -> wet low-pass 1.5 kHz .. 14 kHz
 *   Stereo   -> L/R LFO phase spread (0 .. 180°)
 *   Mix      -> wet/dry blend
 */
#include "DistrhoPlugin.hpp"
#include "StudioChorusParams.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

static const int kBufLen = 6000;   // ~60 ms @ 96 kHz

class StereoChorus {
    float fs = 48000.f;
    float bufL[kBufLen], bufR[kBufLen];
    int   wL = 0, wR = 0;
    float lfoPhase = 0.f, lfoInc = 0.001f;
    float baseS = 600.f, depthS = 200.f, stereoOff = 1.5f, mix = 0.2f;
    // wet filters (HP then LP) per channel
    float hpL = 0.f, hpR = 0.f, lpL = 0.f, lpR = 0.f, cHP = 0.02f, cLP = 0.3f;

    inline float readTap(const float* buf, int w, float delaySamp) {
        float rp = (float)w - delaySamp;
        while (rp < 0.f) rp += (float)kBufLen;
        int i0 = (int)rp; float fr = rp - (float)i0;
        int i1 = i0 + 1; if (i1 >= kBufLen) i1 -= kBufLen;
        return buf[i0] + fr * (buf[i1] - buf[i0]);
    }
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; reset(); }
    void reset() { std::memset(bufL,0,sizeof(bufL)); std::memset(bufR,0,sizeof(bufR)); wL=wR=0; lfoPhase=0.f; hpL=hpR=lpL=lpR=0.f; }
    void setParams(float rate, float depth, float mixP, float lo, float hi, float stereo, float delay) {
        lfoInc    = 6.2831853f * (0.1f + rate * 5.9f) / fs;        // 0.1 .. 6 Hz
        const float baseMs  = 5.0f + delay * 23.0f;                 // 5 .. 28 ms
        const float depthMs = depth * 7.0f;                         // 0 .. 7 ms
        baseS  = baseMs  * 0.001f * fs;
        depthS = depthMs * 0.001f * fs;
        if (depthS > baseS - 1.0f) depthS = baseS - 1.0f;           // keep delay positive
        stereoOff = stereo * 3.14159265f;                           // 0 .. 180°
        mix = mixP;
        cHP = onePoleCoef(25.0f  * std::pow(20.0f,   lo), fs);      // 25 .. 500 Hz
        cLP = onePoleCoef(1500.0f * std::pow(9.333f, hi), fs);      // 1.5k .. 14k Hz
    }
    inline void process(float xL, float xR, float& outL, float& outR) {
        bufL[wL] = xL; bufR[wR] = xR;

        lfoPhase += lfoInc; if (lfoPhase > 6.2831853f) lfoPhase -= 6.2831853f;
        // two ensemble voices per channel (phase offset 2.1 rad), L/R spread by Stereo
        const float l1 = std::sin(lfoPhase),            l2 = std::sin(lfoPhase + 2.1f);
        const float r1 = std::sin(lfoPhase + stereoOff), r2 = std::sin(lfoPhase + stereoOff + 2.1f);
        float wetL = 0.5f * (readTap(bufL, wL, baseS + depthS * l1) + readTap(bufL, wL, baseS + depthS * l2));
        float wetR = 0.5f * (readTap(bufR, wR, baseS + depthS * r1) + readTap(bufR, wR, baseS + depthS * r2));

        // shape the wet: high-pass (remove mud) then low-pass (tame top)
        hpL += cHP * (wetL - hpL); wetL -= hpL;  lpL += cLP * (wetL - lpL); wetL = lpL;
        hpR += cHP * (wetR - hpR); wetR -= hpR;  lpR += cLP * (wetR - lpR); wetR = lpR;

        if (++wL >= kBufLen) wL = 0;
        if (++wR >= kBufLen) wR = 0;

        outL = xL * (1.0f - mix) + wetL * mix * 2.0f;
        outR = xR * (1.0f - mix) + wetR * mix * 2.0f;
    }
};

class StudioChorusPlugin : public Plugin {
    StereoChorus ch;
    float fParams[kParamCount];
    void recalc() {
        ch.setParams(fParams[kRate], fParams[kDepth], fParams[kMix], fParams[kLoFilter],
                     fParams[kHiFilter], fParams[kStereo], fParams[kDelay]);
    }
public:
    StudioChorusPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kStudioChorusDef[i];
        ch.setSampleRate((float)getSampleRate()); recalc();
    }
protected:
    const char* getLabel()       const override { return "StudioChorus"; }
    const char* getDescription() const override { return "RCE-10 stereo chorus ensemble"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'S', 'c'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kStudioChorusNames[i]; p.symbol = kStudioChorusSymbols[i];
        p.ranges.min = kStudioChorusMin[i]; p.ranges.max = kStudioChorusMax[i]; p.ranges.def = kStudioChorusDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { ch.setSampleRate((float)r); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) ch.process(iL[i], iR[i], oL[i], oR[i]);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioChorusPlugin)
};

Plugin* createPlugin() { return new StudioChorusPlugin(); }

END_NAMESPACE_DISTRHO
