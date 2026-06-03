/*
 * TapeEcho — Roland RE-201 Space Echo model for Rack_TapeEcho.
 * Stereo tape echo: a delay line per channel with a dark low-pass + tape
 * saturation in the loop and wow & flutter on the read position. Stereo spreads
 * the two channels' times apart and adds a touch of cross-feedback (ping-pong).
 *   Time -> 0..700 ms   Feedback -> 0..0.95   Filter -> repeat tone
 *   Stereo -> L/R spread + cross-feed   Mix -> wet/dry
 */
#include "DistrhoPlugin.hpp"
#include "TapeEchoParams.h"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

static inline float onePoleCoef(float fc, float fs) {
    const float c = 1.0f - std::exp(-6.2831853f * fc / fs);
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}
static const int kMaxDelay = 68000;

class TapeLine {
    float fs = 48000.f;
    float buf[kMaxDelay]; int w = 0;
    float lpZ = 0.f, cLP = 0.25f, smooth = 9600.f, target = 9600.f, fb = 0.4f;
    float wowPh = 0.f, flutPh = 0.f, wowInc = 0.f, flutInc = 0.f, wowD = 0.f, flutD = 0.f;
public:
    void setSampleRate(float s) {
        fs = (s > 0.f) ? s : 48000.f; std::memset(buf,0,sizeof(buf)); w=0; lpZ=0.f;
        wowInc = 6.2831853f*0.7f/fs; flutInc = 6.2831853f*6.5f/fs;
        wowD = 0.0016f*fs; flutD = 0.0004f*fs;
    }
    void setParams(float ms, float fbP, float filt) {
        target = ms*0.001f*fs; const float md=(float)(kMaxDelay-4); if(target>md)target=md;
        fb = fbP*0.95f;
        cLP = onePoleCoef(350.0f * std::pow(2.0f, filt*3.2f), fs);   // darker than a clean delay
    }
    // returns wet; feedIn lets the caller inject cross-feedback
    inline float process(float x, float feedIn) {
        smooth += 0.0007f*(target-smooth);
        wowPh+=wowInc; if(wowPh>6.2831853f)wowPh-=6.2831853f;
        flutPh+=flutInc; if(flutPh>6.2831853f)flutPh-=6.2831853f;
        const float mod = wowD*std::sin(wowPh) + flutD*std::sin(flutPh);
        float rp=(float)w - smooth - mod; while(rp<0.f)rp+=(float)kMaxDelay; while(rp>=(float)kMaxDelay)rp-=(float)kMaxDelay;
        int i0=(int)rp; float fr=rp-(float)i0; int i1=i0+1; if(i1>=kMaxDelay)i1-=kMaxDelay;
        float wet = buf[i0] + fr*(buf[i1]-buf[i0]);
        lpZ += cLP*(wet-lpZ); wet=lpZ;
        float wn = x + (wet + feedIn)*fb; wn = std::tanh(wn*1.2f)*0.95f;   // tape saturation
        buf[w]=wn; if(++w>=kMaxDelay)w=0;
        lastWet = wet; return wet;
    }
    float lastWet = 0.f;
};

class TapeEchoPlugin : public Plugin {
    TapeLine L, R;
    float fParams[kParamCount];
    float stereo = 0.5f, mix = 0.3f;
    void recalc() {
        const float ms = 5.0f + fParams[kTime]*695.0f;
        stereo = fParams[kStereo]; mix = fParams[kMix];
        L.setParams(ms, fParams[kFeedback], fParams[kFilter]);
        R.setParams(ms * (1.0f + stereo*0.35f), fParams[kFeedback], fParams[kFilter]);  // spread
    }
public:
    TapeEchoPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kTapeEchoDef[i];
        L.setSampleRate((float)getSampleRate()); R.setSampleRate((float)getSampleRate()); recalc();
    }
protected:
    const char* getLabel()       const override { return "TapeEcho"; }
    const char* getDescription() const override { return "Space Echo tape delay"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1,0,0); }
    int64_t     getUniqueId()    const override { return d_cconst('R','T','e','1'); }
    void initParameter(uint32_t i, Parameter& p) override {
        if (i>=(uint32_t)kParamCount) return;
        p.hints=kParameterIsAutomatable;
        p.name=kTapeEchoNames[i]; p.symbol=kTapeEchoSymbols[i];
        p.ranges.min=kTapeEchoMin[i]; p.ranges.max=kTapeEchoMax[i]; p.ranges.def=kTapeEchoDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void  setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fParams[i]=v;recalc();} }
    void  sampleRateChanged(double) override { L.setSampleRate((float)getSampleRate()); R.setSampleRate((float)getSampleRate()); recalc(); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0]; const float* iR=in[1]; float* oL=out[0]; float* oR=out[1];
        const float cross = stereo*0.35f;
        for (uint32_t i=0;i<frames;++i){
            float wl = L.process(iL[i], R.lastWet*cross);
            float wr = R.process(iR[i], L.lastWet*cross);
            oL[i] = iL[i]*(1.0f-0.3f*mix) + wl*mix;
            oR[i] = iR[i]*(1.0f-0.3f*mix) + wr*mix;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoPlugin)
};
Plugin* createPlugin() { return new TapeEchoPlugin(); }
END_NAMESPACE_DISTRHO
