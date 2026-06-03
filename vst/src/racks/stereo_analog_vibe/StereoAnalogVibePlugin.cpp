/* StereoAnalogVibe — Univibe-style 4-stage phase vibrato for
 * Rack_StereoAnalogVibe. Four all-pass stages swept by an LFO whose shape morphs
 * from sine to square (Waveform); full-wet gives the pitch-vibrato "vibe", less
 * gives the chorale shimmer. L/R LFOs offset for stereo.
 *   Speed -> 0.1..8 Hz   Waveform -> sine..square   Mix -> dry/wet */
#include "DistrhoPlugin.hpp"
#include "StereoAnalogVibeParams.h"
#include <cmath>
START_NAMESPACE_DISTRHO
static const int kStages = 4;
class VibeCh {
    float fs=48000.f; float xz[kStages]={0},yz[kStages]={0}; float mix=0.5f;
public:
    void setSampleRate(float s){ fs=(s>0.f)?s:48000.f; }
    void reset(){ for(int i=0;i<kStages;++i){xz[i]=yz[i]=0.f;} }
    void setMix(float m){ mix=m; }
    inline float process(float x, float lfo01){
        // lfo01 in 0..1 → sweep 200..1600 Hz
        float fc = 200.0f * std::pow(8.0f, lfo01);
        const float nyq=fs*0.45f; if(fc>nyq)fc=nyq;
        const float t=std::tan(3.14159265f*fc/fs); const float a=(t-1.0f)/(t+1.0f);
        float s=x;
        for(int i=0;i<kStages;++i){ float in=s; s=a*in+xz[i]-a*yz[i]; xz[i]=in; yz[i]=s; }
        return x*(1.0f-mix)+s*mix;
    }
};
class StereoAnalogVibePlugin : public Plugin {
    VibeCh L,R; float lfoPhase=0.f, lfoInc=0.f, wave=0.2f; float fParams[kParamCount];
    void recalc(){ lfoInc=6.2831853f*(0.1f+fParams[kSpeed]*7.9f)/(float)getSampleRate();
        wave=fParams[kWaveform]; L.setMix(fParams[kMix]); R.setMix(fParams[kMix]); }
    static inline float shape(float ph, float w){
        float s=std::sin(ph);                       // -1..1 sine
        float sq=(s>=0.f)?1.0f:-1.0f;               // square
        float m=s*(1.0f-w)+sq*w;                    // morph
        return 0.5f+0.5f*m;                         // 0..1
    }
public:
    StereoAnalogVibePlugin():Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fParams[i]=kStereoAnalogVibeDef[i];
        L.setSampleRate((float)getSampleRate());R.setSampleRate((float)getSampleRate());L.reset();R.reset();recalc(); }
protected:
    const char* getLabel() const override { return "StereoAnalogVibe"; }
    const char* getDescription() const override { return "Univibe vibrato"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,0,0); }
    int64_t getUniqueId() const override { return d_cconst('R','V','i','1'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return;
        p.hints=kParameterIsAutomatable; p.name=kStereoAnalogVibeNames[i]; p.symbol=kStereoAnalogVibeSymbols[i];
        p.ranges.min=kStereoAnalogVibeMin[i]; p.ranges.max=kStereoAnalogVibeMax[i]; p.ranges.def=kStereoAnalogVibeDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fParams[i]=v;recalc();} }
    void sampleRateChanged(double) override { L.setSampleRate((float)getSampleRate());R.setSampleRate((float)getSampleRate());recalc(); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0];const float* iR=in[1];float* oL=out[0];float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ lfoPhase+=lfoInc; if(lfoPhase>6.2831853f)lfoPhase-=6.2831853f;
            oL[i]=L.process(iL[i],shape(lfoPhase,wave)); oR[i]=R.process(iR[i],shape(lfoPhase+1.5708f,wave)); } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoAnalogVibePlugin)
};
Plugin* createPlugin(){ return new StereoAnalogVibePlugin(); }
END_NAMESPACE_DISTRHO
