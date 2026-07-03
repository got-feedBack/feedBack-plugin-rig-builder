/* StereoTubeTrem — tube tremolo (amplitude modulation) for Rack_StereoTubeTrem.
 * The LFO (Speed, shape morphs sine→square via Waveform) modulates the gain;
 * Mix sets the depth. The two channels modulate in opposite phase for a stereo
 * "panning" tremolo. A gentle tube-ish soft clip adds warmth at depth.
 *   Speed -> 0.1..12 Hz   Mix -> depth   Waveform -> sine..square */
#include "DistrhoPlugin.hpp"
#include "StereoTubeTremParams.h"
#include <cmath>
START_NAMESPACE_DISTRHO
class StereoTubeTremPlugin : public Plugin {
    float lfoPhase=0.f, lfoInc=0.f, depth=0.5f, wave=0.2f; float fParams[kParamCount];
    void recalc(){ lfoInc=6.2831853f*(0.1f+fParams[kSpeed]*11.9f)/(float)getSampleRate();
        depth=fParams[kMix]; wave=fParams[kWaveform]; }
    static inline float shape(float ph,float w){ float s=std::sin(ph); float sq=(s>=0.f)?1.f:-1.f;
        return 0.5f+0.5f*(s*(1.0f-w)+sq*w); }
public:
    StereoTubeTremPlugin():Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fParams[i]=kStereoTubeTremDef[i]; recalc(); }
protected:
    const char* getLabel() const override { return "StereoTubeTrem"; }
    const char* getDescription() const override { return "Tube tremolo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,0,0); }
    int64_t getUniqueId() const override { return d_cconst('R','T','t','1'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return;
        p.hints=kParameterIsAutomatable; p.name=kStereoTubeTremNames[i]; p.symbol=kStereoTubeTremSymbols[i];
        p.ranges.min=kStereoTubeTremMin[i]; p.ranges.max=kStereoTubeTremMax[i]; p.ranges.def=kStereoTubeTremDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fParams[i]=v;recalc();} }
    void sampleRateChanged(double) override { recalc(); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0];const float* iR=in[1];float* oL=out[0];float* oR=out[1];
        for(uint32_t i=0;i<frames;++i){ lfoPhase+=lfoInc; if(lfoPhase>6.2831853f)lfoPhase-=6.2831853f;
            float gL=1.0f-depth*(1.0f-shape(lfoPhase,wave));
            float gR=1.0f-depth*(1.0f-shape(lfoPhase+3.14159265f,wave));   // opposite phase
            oL[i]=std::tanh(iL[i]*gL*1.05f); oR[i]=std::tanh(iR[i]*gR*1.05f); } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoTubeTremPlugin)
};
Plugin* createPlugin(){ return new StereoTubeTremPlugin(); }
END_NAMESPACE_DISTRHO
