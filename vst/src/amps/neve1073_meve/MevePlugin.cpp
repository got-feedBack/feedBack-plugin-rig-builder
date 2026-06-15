/*
 * MEVE 1073 - Neve 1073 channel amplifier (class-A console preamp/EQ as a DI)
 * for the game's DI_Amp_MixerPre. Parody brand "Meve"; the in-app face must
 * never read "Neve".
 *
 * Local reference (modelled component-by-component):
 *   amps/Neve 1073 Preamp (MixerPre)/1073-fullpak.pdf
 *   (EH10023 module + EK20033 sensitivity switch + BA283/BA284 class-A cards
 *    + B182/C HPF + B205 hi/lo EQ + B211 presence + LO1166 output trafo)
 *
 * See MeveParams.h for the full panel/board breakdown. Key voicing points:
 *  - Marinair-style input iron: low corner ~18 Hz with a small LF bloom.
 *  - The gain is the stepped EK20033 sensitivity mapped to a continuous knob;
 *    pushing it drives the BA283 class-A stages + LO1166 output iron into the
 *    famous progressive 2nd-harmonic colour ("the Neve push").
 *  - HPF is the 3rd-order B182/C board (OFF/50/80/160/300 Hz).
 *  - EQ: LOW shelf +/-16 dB (35/60/110/220), PRESENCE peak +/-18 dB
 *    (0.36/0.7/1.6/3.2/4.8/7.2 kHz, broad class-A Q), HIGH shelf +/-16 dB
 *    fixed @ 12 kHz. A DI: no speaker model.
 *
 * the game: RS Gain -> Gain; RS Bass -> Low; RS Mid -> Mid; RS Treble -> High.
 */
#include "DistrhoPlugin.hpp"
#include "MeveParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps).
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

namespace {

static constexpr float kPi = 3.14159265359f;
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float clampFreq(float hz, float sr) { return std::fmax(20.0f, std::fmin(hz, sr * 0.45f)); }
static inline float smoothstep(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }
static inline float smoothstepRange(float e0, float e1, float x) { return smoothstep((x - e0) / (e1 - e0)); }
static inline float softClip(float x) { return std::tanh(x); }
static inline float eqDb(float v, float rangeDb) { return (clamp01(v) - 0.5f) * 2.0f * rangeDb; }

// Map a continuous 0..1 selector knob onto N switch positions.
static inline int selPos(float v, int n)
{
    int i = (int)(clamp01(v) * (float)n);
    return i >= n ? n - 1 : i;
}

// Class-A transistor + iron colour: asymmetric (2nd-harmonic-forward) soft
// curve - positive bias models the single-ended class-A operating point.
static inline float classA(float x, float bias)
{
    const float g = x + bias;
    const float w = 1.30f * g + 0.42f * g * std::fabs(g);
    const float idle = 1.30f * bias + 0.42f * bias * std::fabs(bias);
    return std::tanh(w) - std::tanh(idle);
}

class Biquad
{
    float b0=1.0f,b1=0.0f,b2=0.0f,a1=0.0f,a2=0.0f,z1=0.0f,z2=0.0f;
    void set(float nb0,float nb1,float nb2,float na0,float na1,float na2)
    { if(std::fabs(na0)<1.0e-12f) na0=1.0f; const float i=1.0f/na0;
      b0=nb0*i; b1=nb1*i; b2=nb2*i; a1=na1*i; a2=na2*i; }
public:
    void reset(){ z1=z2=0.0f; }
    float process(float x){ const float y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; }
    void setHighPass(float sr,float hz,float q){ hz=clampFreq(hz,sr); const float w=2.0f*kPi*hz/sr,c=std::cos(w),al=std::sin(w)/(2.0f*q);
        set((1.0f+c)*0.5f,-(1.0f+c),(1.0f+c)*0.5f,1.0f+al,-2.0f*c,1.0f-al); }
    void setLowPass(float sr,float hz,float q){ hz=clampFreq(hz,sr); const float w=2.0f*kPi*hz/sr,c=std::cos(w),al=std::sin(w)/(2.0f*q);
        set((1.0f-c)*0.5f,1.0f-c,(1.0f-c)*0.5f,1.0f+al,-2.0f*c,1.0f-al); }
    void setPeaking(float sr,float hz,float q,float dB){ hz=clampFreq(hz,sr); const float a=std::pow(10.0f,dB/40.0f),w=2.0f*kPi*hz/sr,c=std::cos(w),al=std::sin(w)/(2.0f*q);
        set(1.0f+al*a,-2.0f*c,1.0f-al*a,1.0f+al/a,-2.0f*c,1.0f-al/a); }
    void setHighShelf(float sr,float hz,float sl,float dB){ hz=clampFreq(hz,sr); const float a=std::pow(10.0f,dB/40.0f),w=2.0f*kPi*hz/sr,c=std::cos(w),sn=std::sin(w),ra=std::sqrt(a),al=sn*0.5f*std::sqrt((a+1.0f/a)*(1.0f/sl-1.0f)+2.0f);
        set(a*((a+1.0f)+(a-1.0f)*c+2.0f*ra*al),-2.0f*a*((a-1.0f)+(a+1.0f)*c),a*((a+1.0f)+(a-1.0f)*c-2.0f*ra*al),
            (a+1.0f)-(a-1.0f)*c+2.0f*ra*al,2.0f*((a-1.0f)-(a+1.0f)*c),(a+1.0f)-(a-1.0f)*c-2.0f*ra*al); }
    void setLowShelf(float sr,float hz,float sl,float dB){ hz=clampFreq(hz,sr); const float a=std::pow(10.0f,dB/40.0f),w=2.0f*kPi*hz/sr,c=std::cos(w),sn=std::sin(w),ra=std::sqrt(a),al=sn*0.5f*std::sqrt((a+1.0f/a)*(1.0f/sl-1.0f)+2.0f);
        set(a*((a+1.0f)-(a-1.0f)*c+2.0f*ra*al),2.0f*a*((a-1.0f)-(a+1.0f)*c),a*((a+1.0f)-(a-1.0f)*c-2.0f*ra*al),
            (a+1.0f)+(a-1.0f)*c+2.0f*ra*al,-2.0f*((a-1.0f)+(a+1.0f)*c),(a+1.0f)+(a-1.0f)*c-2.0f*ra*al); }
};

class OnePoleHp
{
    float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
public:
    void reset() { x1 = y1 = 0.0f; }
    void setHz(float sr, float hz)
    {
        hz = clampFreq(hz, sr);
        const float tau = 1.0f / (2.0f * kPi * hz);
        const float dt = 1.0f / std::fmax(sr, 1000.0f);
        a = tau / (tau + dt);
    }
    float process(float x) { const float y = a * (y1 + x - x1); x1 = x; y1 = y; return y; }
};

class DcBlock
{
    float x1=0.0f,y1=0.0f;
public:
    void reset(){ x1=y1=0.0f; }
    float process(float x){ const float y=x-x1+0.995f*y1; x1=x; y1=y; return y; }
};

} // namespace

class MeveCore
{
    float sampleRate = 48000.0f;
    float gain    = kMeveDef[kGain];
    float low     = kMeveDef[kLow];
    float lowFreq = kMeveDef[kLowFreq];
    float mid     = kMeveDef[kMid];
    float midFreq = kMeveDef[kMidFreq];
    float high    = kMeveDef[kHigh];
    float hpf     = kMeveDef[kHpf];
    float output  = kMeveDef[kOutput];

    Biquad inTrafoHp;     // Marinair-style input iron low corner
    Biquad inTrafoIron;   // small LF bloom
    Biquad inTrafoLp;     // iron top limit
    Biquad hpf2;          // B182/C 3rd-order HPF: 2nd-order section...
    OnePoleHp hpf1;       // ...+ 1st-order section
    bool hpfOn = false;
    Biquad lowShelf;      // B205 LOW
    Biquad presPeak;      // B211 PRESENCE
    Biquad highShelf;     // B205 HIGH (fixed 12 kHz)
    Biquad outTrafoHp;    // LO1166 output iron
    DcBlock dcBlock;

    void updateFilters()
    {
        // Input iron: ~18 Hz corner, +0.8 dB bloom ~55 Hz, top ~18 kHz.
        inTrafoHp.setHighPass(sampleRate, 18.0f, 0.72f);
        inTrafoIron.setPeaking(sampleRate, 55.0f, 0.90f, 0.8f);
        inTrafoLp.setLowPass(sampleRate, 18000.0f, 0.70f);

        // B182/C HPF: OFF/50/80/160/300, 3rd-order (18 dB/oct).
        static const float kHpfHz[5] = { 0.0f, 50.0f, 80.0f, 160.0f, 300.0f };
        const float fh = kHpfHz[selPos(hpf, 5)];
        hpfOn = fh > 0.0f;
        if (hpfOn)
        {
            hpf2.setHighPass(sampleRate, fh, 1.0f);   // Butterworth 3rd-order =
            hpf1.setHz(sampleRate, fh);               // Q=1.0 biquad + 1st-order
        }

        // B205 LOW shelf +/-16 dB @ 35/60/110/220 Hz.
        static const float kLowHz[4] = { 35.0f, 60.0f, 110.0f, 220.0f };
        lowShelf.setLowShelf(sampleRate, kLowHz[selPos(lowFreq, 4)], 0.72f, eqDb(low, 16.0f));

        // B211 PRESENCE peak +/-18 dB @ 0.36/0.7/1.6/3.2/4.8/7.2 kHz, broad Q.
        static const float kMidHz[6] = { 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };
        presPeak.setPeaking(sampleRate, kMidHz[selPos(midFreq, 6)], 0.90f, eqDb(mid, 18.0f));

        // B205 HIGH shelf +/-16 dB, fixed 12 kHz.
        highShelf.setHighShelf(sampleRate, 12000.0f, 0.70f, eqDb(high, 16.0f));

        // LO1166 output iron low corner.
        outTrafoHp.setHighPass(sampleRate, 22.0f, 0.74f);
    }

public:
    void reset()
    {
        inTrafoHp.reset(); inTrafoIron.reset(); inTrafoLp.reset();
        hpf2.reset(); hpf1.reset();
        lowShelf.reset(); presPeak.reset(); highShelf.reset();
        outTrafoHp.reset(); dcBlock.reset();
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kGain:    gain = v;    break;
            case kLow:     low = v;     break;
            case kLowFreq: lowFreq = v; break;
            case kMid:     mid = v;     break;
            case kMidFreq: midFreq = v; break;
            case kHigh:    high = v;    break;
            case kHpf:     hpf = v;     break;
            case kOutput:  output = v;  break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kMeveDef[i]); }

    float process(float in)
    {
        const float g   = smoothstep(gain);
        const float hot = smoothstepRange(0.55f, 0.97f, gain);

        // --- input transformer ---
        float x = inTrafoLp.process(inTrafoIron.process(inTrafoHp.process(in)));

        // --- EK20033 sensitivity -> BA284 input amp. The class-A colour is
        //     progressive: clean at low sensitivity, the "Neve push" on top. ---
        const float drive = 0.45f + 3.4f * g + 2.6f * hot;
        float y = classA(x * drive, 0.085f) * 1.10f;

        // --- B182/C HPF (3rd-order) ---
        if (hpfOn)
            y = hpf1.process(hpf2.process(y));

        // --- EQ boards (class-A, between the amp stages) ---
        y = lowShelf.process(y);
        y = presPeak.process(y);
        y = highShelf.process(y);

        // --- BA283 output stage + LO1166: a second, gentler class-A rounding
        //     plus the output iron. ---
        y = classA(y * 0.58f, 0.060f) * 1.62f;
        y = dcBlock.process(y);
        y = outTrafoHp.process(y);

        // --- Output + loudness normalization (lift capped past breakup) ---
        const float toneEnergy = 1.0f
            + 0.020f * std::fabs((low - 0.5f) * 16.0f)
            + 0.016f * std::fabs((mid - 0.5f) * 16.0f)
            + 0.016f * std::fabs((high - 0.5f) * 16.0f);
        const float liftG = (g < 0.5f) ? g : 0.5f;
        const float makeup = 1.0f + 0.50f * liftG;
        const float cleanMakeup = 1.0f + 1.7f * std::exp(-gain / 0.26f);
        const float lvl = (0.29f * makeup * cleanMakeup * (0.50f + 0.70f * output)) / toneEnergy;
        return softClip(y * lvl) * 0.97f;
    }
};

class MevePlugin : public Plugin
{
    MeveCore left;
    MeveCore right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    MevePlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kMeveDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Meve1073"; }
    const char* getDescription() const override { return "Neve 1073 style class-A console preamp/EQ DI"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'v', '7', '3'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMeveNames[index];
        parameter.symbol = kMeveSymbols[index];
        parameter.ranges.min = kMeveMin[index];
        parameter.ranges.max = kMeveMax[index];
        parameter.ranges.def = kMeveDef[index];
    }

    float getParameterValue(uint32_t index) const override { return index < (uint32_t)kParamCount ? params[index] : 0.0f; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount) return;
        params[index] = clamp01(value);
        left.setParam((int)index, params[index]);
        right.setParam((int)index, params[index]);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            outL[i] = rbAmpLvl(0.560f * left.process(inL[i]));
            outR[i] = rbAmpLvl(0.560f * right.process(inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MevePlugin)
};

Plugin* createPlugin() { return new MevePlugin(); }

END_NAMESPACE_DISTRHO
