/*
 * StudioFlanger — stereo flanger rack for Rack_StudioFlanger.
 *
 * Short modulated delay (0.5 .. 6 ms) per channel swept by an LFO, with
 * regeneration feedback (Regen) for the metallic jet, a low-pass in the loop
 * (Tone), and a wet/dry Mix. L/R LFOs are offset for stereo width.
 *   Rate  -> 0.1 .. 6 Hz   Depth -> sweep amount
 *   Regen -> feedback       Tone -> wet/feedback low-pass
 */
#include "DistrhoPlugin.hpp"
#include "StudioFlangerParams.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

static const int kFlBuf = 1024;   // ~10 ms @ 96 kHz

class FlangerCh {
    float fs = 48000.f;
    float buf[kFlBuf]; int w = 0;
    float lpZ = 0.f, cLP = 0.4f, fbState = 0.f;
    float baseS = 48.f, depthS = 200.f, regen = 0.5f, mix = 0.4f;
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; std::memset(buf,0,sizeof(buf)); w=0; lpZ=0.f; fbState=0.f; }
    void setParams(float depth, float regenP, float tone, float mixP) {
        baseS  = 0.6f * 0.001f * fs;                 // ~0.6 ms base
        depthS = depth * 5.0f * 0.001f * fs;         // up to 5 ms sweep
        regen  = regenP * 0.9f;
        cLP    = onePoleCoef(800.0f * std::pow(2.0f, tone * 4.0f), fs);  // 800 .. ~12.8k
        mix    = mixP;
    }
    inline float process(float x, float lfo01) {
        const float dly = baseS + depthS * lfo01;
        float rp = (float)w - dly;
        while (rp < 0.f) rp += (float)kFlBuf;
        int i0 = (int)rp; float fr = rp - (float)i0; int i1 = i0+1; if (i1>=kFlBuf) i1-=kFlBuf;
        float wet = buf[i0] + fr * (buf[i1] - buf[i0]);
        lpZ += cLP * (wet - lpZ); wet = lpZ;            // Tone low-pass
        buf[w] = x + wet * regen;                        // write input + feedback
        if (++w >= kFlBuf) w = 0;
        return x * (1.0f - 0.5f * mix) + wet * mix;
    }
};

class StudioFlangerPlugin : public Plugin {
    FlangerCh L, R;
    float lfoPhase = 0.f, lfoInc = 0.f;
    float fParams[kParamCount];
    void recalc() {
        lfoInc = 6.2831853f * (0.1f + fParams[kRate] * 5.9f) / (float)getSampleRate();
        L.setParams(fParams[kDepth], fParams[kRegen], fParams[kTone], fParams[kMix]);
        R.setParams(fParams[kDepth], fParams[kRegen], fParams[kTone], fParams[kMix]);
    }
public:
    StudioFlangerPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kStudioFlangerDef[i];
        L.setSampleRate((float)getSampleRate()); R.setSampleRate((float)getSampleRate()); recalc();
    }
protected:
    const char* getLabel()       const override { return "StudioFlanger"; }
    const char* getDescription() const override { return "Stereo flanger"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R','F','l','1'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kStudioFlangerNames[i]; p.symbol = kStudioFlangerSymbols[i];
        p.ranges.min = kStudioFlangerMin[i]; p.ranges.max = kStudioFlangerMax[i]; p.ranges.def = kStudioFlangerDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double) override { L.setSampleRate((float)getSampleRate()); R.setSampleRate((float)getSampleRate()); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) {
            lfoPhase += lfoInc; if (lfoPhase > 6.2831853f) lfoPhase -= 6.2831853f;
            const float lL = 0.5f + 0.5f * std::sin(lfoPhase);
            const float lR = 0.5f + 0.5f * std::sin(lfoPhase + 1.5708f);   // 90° stereo
            oL[i] = L.process(iL[i], lL); oR[i] = R.process(iR[i], lR);
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioFlangerPlugin)
};

Plugin* createPlugin() { return new StudioFlangerPlugin(); }

END_NAMESPACE_DISTRHO
