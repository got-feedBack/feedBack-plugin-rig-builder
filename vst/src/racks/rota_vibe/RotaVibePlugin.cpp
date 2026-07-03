/* RotaVibe — rotary speaker (Leslie) for Rack_RotaVibe.
 * Input is split at ~800 Hz into a rotor (bass) and horn (treble) band; each is
 * given Doppler pitch wobble (a modulated delay) + amplitude tremolo, the horn
 * faster than the rotor, panned in opposite stereo directions. Balance sets the
 * horn/rotor level, Depth the modulation amount, Rate the rotation speed.
 */
#include "DistrhoPlugin.hpp"
#include "RotaVibeParams.h"
#include <cmath>
#include <cstring>
START_NAMESPACE_DISTRHO
static inline float onePoleCoef(float fc,float fs){ float c=1.f-std::exp(-6.2831853f*fc/fs); return c<0?0:(c>1?1:c); }
static const int kRBuf=2048;
class RotorTap {
    float buf[kRBuf]; int w=0; float fs=48000.f;
public:
    void setSR(float s){ fs=s; std::memset(buf,0,sizeof(buf)); w=0; }
    inline float proc(float x, float delaySamp){
        buf[w]=x; float rp=(float)w-delaySamp; while(rp<0)rp+=kRBuf; int i0=(int)rp; float fr=rp-i0; int i1=(i0+1)%kRBuf;
        float y=buf[i0]+fr*(buf[i1]-buf[i0]); if(++w>=kRBuf)w=0; return y; }
};
class RotaVibePlugin : public Plugin {
    float fs=48000.f; float xoLP=0.f, cXo=0.1f;          // crossover state
    RotorTap hornL,hornR,rotL,rotR;
    float phH=0.f,phR=0.f,incH=0.f,incR=0.f, depth=0.6f, mix=0.6f, bal=0.5f;
    float fParams[kParamCount];
    void recalc(){
        const float rate=0.1f+fParams[kRate]*6.5f;        // Hz
        incH=6.2831853f*rate/fs; incR=6.2831853f*(rate*0.78f)/fs;
        depth=fParams[kDepth]; mix=fParams[kMix]; bal=fParams[kBalance];
        cXo=onePoleCoef(800.f,fs);
    }
public:
    RotaVibePlugin():Plugin(kParamCount,0,0){ for(int i=0;i<kParamCount;++i)fParams[i]=kRotaVibeDef[i];
        fs=(float)getSampleRate(); hornL.setSR(fs);hornR.setSR(fs);rotL.setSR(fs);rotR.setSR(fs); recalc(); }
protected:
    const char* getLabel() const override { return "RotaVibe"; }
    const char* getDescription() const override { return "Rotary speaker"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,0,0); }
    int64_t getUniqueId() const override { return d_cconst('R','R','v','1'); }
    void initParameter(uint32_t i, Parameter& p) override { if(i>=(uint32_t)kParamCount)return;
        p.hints=kParameterIsAutomatable; p.name=kRotaVibeNames[i]; p.symbol=kRotaVibeSymbols[i];
        p.ranges.min=kRotaVibeMin[i]; p.ranges.max=kRotaVibeMax[i]; p.ranges.def=kRotaVibeDef[i]; }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?fParams[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override { if(i<(uint32_t)kParamCount){fParams[i]=v;recalc();} }
    void sampleRateChanged(double) override { fs=(float)getSampleRate(); hornL.setSR(fs);hornR.setSR(fs);rotL.setSR(fs);rotR.setSR(fs); recalc(); }
    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0];const float* iR=in[1];float* oL=out[0];float* oR=out[1];
        const float dopH=depth*0.9f*0.001f*fs, dopR=depth*1.8f*0.001f*fs;   // ms→samp Doppler
        for(uint32_t i=0;i<frames;++i){
            float x=(iL[i]+iR[i])*0.5f;
            xoLP+=cXo*(x-xoLP); float low=xoLP, high=x-xoLP;
            phH+=incH; if(phH>6.2831853f)phH-=6.2831853f;
            phR+=incR; if(phR>6.2831853f)phR-=6.2831853f;
            float sH=std::sin(phH), sR=std::sin(phR);
            float hL=hornL.proc(high, 6.f+dopH*(0.5f+0.5f*sH));
            float hR=hornR.proc(high, 6.f+dopH*(0.5f+0.5f*(-sH)));
            float rL=rotL.proc(low,  6.f+dopR*(0.5f+0.5f*sR));
            float rR=rotR.proc(low,  6.f+dopR*(0.5f+0.5f*(-sR)));
            float amH=0.7f+0.3f*sH, amR=0.85f+0.15f*sR;          // tremolo
            float hornW=bal, rotW=1.0f-bal*0.4f;                  // balance favours horn
            float wL=(hL*amH*hornW + rL*amR*rotW);
            float wR=(hR*amH*hornW + rR*amR*rotW);
            oL[i]=iL[i]*(1.0f-mix)+wL*mix*1.3f;
            oR[i]=iR[i]*(1.0f-mix)+wR*mix*1.3f;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaVibePlugin)
};
Plugin* createPlugin(){ return new RotaVibePlugin(); }
END_NAMESPACE_DISTRHO
