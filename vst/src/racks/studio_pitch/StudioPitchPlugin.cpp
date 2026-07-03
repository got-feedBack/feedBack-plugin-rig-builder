/* StudioPitch — pitch shifter for Rack_StudioPitch.
 * Classic two-tap crossfading delay-line pitch shifter: two read pointers move
 * at the pitch rate through a delay buffer, crossfaded with triangular windows
 * offset by half a window to hide the splice. Tone low-passes the shifted voice,
 * Mix blends dry/wet, Pan places the wet voice in the stereo field.
 *   Pitch 0.5 = unison, 0 = -1 octave, 1 = +1 octave.
 */
#include "DistrhoPlugin.hpp"
#include "StudioPitchParams.h"
#include <cmath>
#include <cstring>
START_NAMESPACE_DISTRHO
static inline float onePoleCoef(float fc,float fs){ float c=1.f-std::exp(-6.2831853f*fc/fs); return c<0?0:(c>1?1:c); }
static const int kPBuf=16384;
class Pitcher {
    float fs=48000.f; float buf[kPBuf]; int w=0; float ph=0.f; float ratio=1.f; float win=4000.f;
    float lpZ=0.f, cLP=0.4f;
public:
    void setSR(float s){ fs=s; std::memset(buf,0,sizeof(buf)); w=0; ph=0.f; win=fs*0.05f; }
    void setParams(float pitchP,float tone){
        // 0..1 → 0.5x .. 2x (one octave down/up), 0.5 = unison
        float semis=(pitchP-0.5f)*24.0f; ratio=std::pow(2.0f, semis/12.0f);
        cLP=onePoleCoef(800.0f*std::pow(2.0f,tone*4.0f),fs);
    }
    inline float proc(float x){
        buf[w]=x;
        const float rate=(1.0f-ratio);            // pointer drift per sample
        ph+=rate; while(ph<0)ph+=win; while(ph>=win)ph-=win;
        float ph2=ph+win*0.5f; if(ph2>=win)ph2-=win;
        auto rd=[&](float p)->float{ float rp=(float)w-p; while(rp<0)rp+=kPBuf; int i0=(int)rp; float fr=rp-i0; int i1=(i0+1)%kPBuf; return buf[i0]+fr*(buf[i1]-buf[i0]); };
        float g1=1.0f-std::fabs(2.0f*ph/win-1.0f);     // triangular window
        float g2=1.0f-std::fabs(2.0f*ph2/win-1.0f);
        float y=rd(ph+2.f)*g1+rd(ph2+2.f)*g2;
        if(++w>=kPBuf)w=0;
        lpZ+=cLP*(y-lpZ); return lpZ;
    }
};
class StudioPitchPlugin : public Plugin {
    Pitcher P; float fParams[kParamCount]; float mix=0.4f, pan=0.5f;
    void recalc(){ P.setParams(fParams[kPitch],fParams[kTone]); mix=fParams[kMix]; pan=fParams[kPan]; }
public:
    StudioPitchPlugin():Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fParams[i]=kStudioPitchDef[i];
        P.setSR((float)getSampleRate()); recalc(); }
protected:
    const char* getLabel() const override { return "StudioPitch"; }
    const char* getDescription() const override { return "Pitch shifter"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,0,0); }
    int64_t getUniqueId() const override { return d_cconst('R','P','i','1'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return;
        p.hints=kParameterIsAutomatable; p.name=kStudioPitchNames[i]; p.symbol=kStudioPitchSymbols[i];
        p.ranges.min=kStudioPitchMin[i]; p.ranges.max=kStudioPitchMax[i]; p.ranges.def=kStudioPitchDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fParams[i]=v;recalc();} }
    void sampleRateChanged(double) override { P.setSR((float)getSampleRate()); recalc(); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0];const float* iR=in[1];float* oL=out[0];float* oR=out[1];
        const float wpL=std::cos(pan*1.5708f), wpR=std::sin(pan*1.5708f);
        for(uint32_t i=0;i<frames;++i){ float x=(iL[i]+iR[i])*0.5f; float wet=P.proc(x);
            oL[i]=iL[i]*(1.0f-mix)+wet*mix*wpL*1.41f; oR[i]=iR[i]*(1.0f-mix)+wet*mix*wpR*1.41f; } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioPitchPlugin)
};
Plugin* createPlugin(){ return new StudioPitchPlugin(); }
END_NAMESPACE_DISTRHO
