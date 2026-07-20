/*
 * WH10Wah - Ibañez WH10 (WN10) active op-amp wah (parody; the in-app face
 * reads "Ibañez", never "Ibanez"). Reference: pedals/Ibanez WH10/ibanez_wh10_wah.pdf.
 *
 * DSP in WH10WahCore.h — a swept resonant op-amp band-pass with the WH10's
 * wide vocal sweep, DEPTH-driven resonance, GUITAR/BASS range switch, and the
 * signature op-amp overload. Its parameters are only the real treadle, DEPTH
 * and GUITAR/BASS switch.
 *
 * EXTRA gear — not mapped to any RS song.
 */
#include "DistrhoPlugin.hpp"
#include "WH10WahParams.h"
#include "WH10WahCore.h"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float clamp01(float v){ return v<0.0f?0.0f:(v>1.0f?1.0f:v); }

class WH10WahPlugin : public Plugin {
    wh10wah::WH10WahCore left, right;
    float params[kParamCount];

    void recalc(){
        left.setParams (params[kAuto],params[kPosition],params[kDepth],params[kSens],params[kRange]);
        right.setParams(params[kAuto],params[kPosition],params[kDepth],params[kSens],params[kRange]);
    }

public:
    WH10WahPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) params[i]=kWH10WahDef[i];
        const float sr=(float)getSampleRate();
        left.setSampleRate(sr);  right.setSampleRate(sr);
        left.reset(); right.reset();
        recalc();
    }

protected:
    const char* getLabel() const override { return "WH10Wah"; }
    const char* getDescription() const override { return "Ibanez WH10 style active op-amp wah"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(2,1,0); }
    int64_t getUniqueId() const override { return d_cconst('W','H','1','0'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i>=(uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i==(uint32_t)kRange || i==(uint32_t)kAuto) p.hints |= kParameterIsBoolean;
        p.name = kWH10WahNames[i]; p.symbol = kWH10WahSymbols[i];
        p.ranges.min = kWH10WahMin[i]; p.ranges.max = kWH10WahMax[i]; p.ranges.def = kWH10WahDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i<(uint32_t)kParamCount)?params[i]:0.f; }
    void setParameterValue(uint32_t i, float v) override {
        if (i<(uint32_t)kParamCount){ params[i]=clamp01(v); recalc(); }
    }
    void sampleRateChanged(double r) override {
        left.setSampleRate((float)r); right.setSampleRate((float)r);
        left.reset(); right.reset(); recalc();
    }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0]; const float* iR=in[1];
        float* oL=out[0]; float* oR=out[1];
        for (uint32_t i=0;i<frames;++i){
            oL[i]=left.process(iL[i]);
            oR[i]=right.process(iR[i]);
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WH10WahPlugin)
};

Plugin* createPlugin() { return new WH10WahPlugin(); }

END_NAMESPACE_DISTRHO
