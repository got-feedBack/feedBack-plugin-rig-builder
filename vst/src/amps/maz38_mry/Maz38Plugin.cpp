#include "DistrhoPlugin.hpp"
#include "Maz38Params.h"
#include "Maz38Core.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>
START_NAMESPACE_DISTRHO
static inline float rbAmpLvl(float x){const float t=.9f,c=.99f,a=std::fabs(x);if(a<=t)return x;return std::copysign(t+(c-t)*std::tanh((a-t)/(c-t)),x);}
class Maz38Plugin:public Plugin{
    maz38::Maz38Core core;float fP[kParamCount];rbshared::Oversampler4x os;static constexpr int kOS=rbshared::Oversampler4x::OS;
    void applyAll(){for(int i=0;i<kParamCount;++i)core.setParam(i,fP[i]);}
public:Maz38Plugin():Plugin(kParamCount,0,0){for(int i=0;i<kParamCount;++i)fP[i]=kMaz38Def[i];core.setSampleRate(kOS*(float)getSampleRate());applyAll();}
protected:
    const char* getLabel()const override{return "MrYMaz38";}const char* getDescription()const override{return "MrYMaz38 - component model";}const char* getMaker()const override{return "RigBuilder";}const char* getLicense()const override{return "ISC";}
    uint32_t getVersion()const override{return d_version(2,2,0);}int64_t getUniqueId()const override{return d_cconst('Y','m','3','8');}
    void initParameter(uint32_t i,Parameter& p)override{if(i>=kParamCount)return;p.hints=kParameterIsAutomatable;if(i==kCabSim)p.hints|=kParameterIsBoolean;p.name=kMaz38Names[i];p.symbol=kMaz38Symbols[i];p.ranges.min=kMaz38Min[i];p.ranges.max=kMaz38Max[i];p.ranges.def=kMaz38Def[i];}
    float getParameterValue(uint32_t i)const override{return i<kParamCount?fP[i]:0;}void setParameterValue(uint32_t i,float v)override{if(i<kParamCount){fP[i]=v;applyAll();}}
    void sampleRateChanged(double r)override{core.setSampleRate(kOS*(float)r);os.reset();applyAll();}
    void run(const float**in,float**out,uint32_t n)override{for(uint32_t i=0;i<n;++i){float u[kOS];os.upsample(in[0][i],u);for(int k=0;k<kOS;++k)u[k]=rbAmpLvl(.42f*core.process(u[k]));float y=os.downsample(u);out[0][i]=out[1][i]=y;}}
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Maz38Plugin)
};Plugin* createPlugin(){return new Maz38Plugin();}END_NAMESPACE_DISTRHO
