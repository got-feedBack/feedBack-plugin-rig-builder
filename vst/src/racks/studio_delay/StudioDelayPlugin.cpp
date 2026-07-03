/*
 * StudioDelay — stereo delay rack (independent L/R times) for Rack_StudioDelay.
 * Each channel has its own delay line + a shared Feedback and a low-pass Filter
 * in the loop so repeats darken. Time smoothed to avoid zipper.
 *   TimeL/TimeR -> 0 .. 700 ms      Feedback -> 0 .. 0.95
 *   Filter      -> repeat low-pass  Mix -> wet/dry
 */
#include "DistrhoPlugin.hpp"
#include "StudioDelayParams.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}
static const int kMaxDelay = 68000;   // ~0.7 s @ 96 kHz

class DLine {
    float fs = 48000.f;
    float buf[kMaxDelay]; int w = 0;
    float lpZ = 0.f, cLP = 0.3f, smooth = 9600.f, target = 9600.f, fb = 0.4f, mix = 0.3f;
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; std::memset(buf,0,sizeof(buf)); w=0; lpZ=0.f; }
    void setParams(float timeP, float fbP, float filt, float mixP) {
        const float ms = 5.0f + timeP * 695.0f;
        target = ms * 0.001f * fs; const float md=(float)(kMaxDelay-4); if(target>md)target=md;
        fb = fbP * 0.95f; mix = mixP;
        cLP = onePoleCoef(600.0f * std::pow(2.0f, filt * 3.6f), fs);
    }
    inline float process(float x) {
        smooth += 0.0007f * (target - smooth);
        float rp = (float)w - smooth; while (rp < 0.f) rp += (float)kMaxDelay;
        int i0=(int)rp; float fr=rp-(float)i0; int i1=i0+1; if(i1>=kMaxDelay)i1-=kMaxDelay;
        float wet = buf[i0] + fr*(buf[i1]-buf[i0]);
        lpZ += cLP*(wet-lpZ); wet=lpZ;
        float wn = x + wet*fb; wn = std::tanh(wn*0.8f)*1.25f;
        buf[w]=wn; if(++w>=kMaxDelay)w=0;
        return x*(1.0f-0.3f*mix) + wet*mix;
    }
};

class StudioDelayPlugin : public Plugin {
    DLine L, R;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kTimeL], fParams[kFeedback], fParams[kFilter], fParams[kMix]);
        R.setParams(fParams[kTimeR], fParams[kFeedback], fParams[kFilter], fParams[kMix]);
    }
public:
    StudioDelayPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kStudioDelayDef[i];
        L.setSampleRate((float)getSampleRate()); R.setSampleRate((float)getSampleRate()); recalc();
    }
protected:
    const char* getLabel()       const override { return "StudioDelay"; }
    const char* getDescription() const override { return "Stereo delay"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1,0,0); }
    int64_t     getUniqueId()    const override { return d_cconst('R','D','l','1'); }
    void initParameter(uint32_t i, Parameter& p) override {
        if (i>=(uint32_t)kParamCount) return;
        p.hints=kParameterIsAutomatable;
        p.name=kStudioDelayNames[i]; p.symbol=kStudioDelaySymbols[i];
        p.ranges.min=kStudioDelayMin[i]; p.ranges.max=kStudioDelayMax[i]; p.ranges.def=kStudioDelayDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void  setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fParams[i]=v;recalc();} }
    void  sampleRateChanged(double) override { L.setSampleRate((float)getSampleRate()); R.setSampleRate((float)getSampleRate()); recalc(); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0]; const float* iR=in[1]; float* oL=out[0]; float* oR=out[1];
        for (uint32_t i=0;i<frames;++i){ oL[i]=L.process(iL[i]); oR[i]=R.process(iR[i]); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioDelayPlugin)
};
Plugin* createPlugin() { return new StudioDelayPlugin(); }
END_NAMESPACE_DISTRHO
