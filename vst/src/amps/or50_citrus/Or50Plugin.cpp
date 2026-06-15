/*
 * CITRUS OR50 - Orange OR50 (vintage Graphic head) for the game's
 * Amp_OrangeOR50. Parody brand "Citrus"; the in-app face must never read "Orange".
 *
 * Local references (modelled component-by-component / reconstructed):
 *   amps/Orange OR100/Orange100.pdf            (the OR100 power+preamp it scales from)
 *   amps/Orange OR50/Orange OR15 SCH ...pdf    (the modern "OR" preamp + tone stack)
 *   amps/Orange OR50/OR30 preamp.jpg           (3-stage cascade + partial cathode bypass)
 *   amps/Orange OR50/Orange Retro 50 Layout.gif(3x12AX7 + 2xEL34 + GZ34 ~50W complement)
 * There is no standalone OR50 schematic, so the preamp/tone stack come from the
 * OR15 (the same modern Orange preamp), and the 2xEL34 ~50W power amp is scaled
 * down from the OR100.
 *
 * Single-channel British EL34 head: input -> 3x ECC83 gain stages (GAIN = HF Drive)
 * -> tone stack (Bass/Middle[FAC]/Treble) + DEPTH (the low-end bass-cap voicing)
 * -> ECC81 long-tail PI -> 2x EL34 (~50W, FULL or HALF power) -> output. A thick,
 * midrange-forward "Orange" voice (the doom/stoner chunk). VOLUME is the master.
 *
 * the game: RS Gain -> GAIN; Bass/Mid/Treble -> tone stack. See
 * rs_knob_to_vst_param.json (Volume + Depth pinned via _static).
 */
#include "DistrhoPlugin.hpp"
#include "Or50Params.h"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

namespace {

static constexpr float kPi = 3.14159265359f;
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float clampFreq(float hz, float sr) { return std::fmax(20.0f, std::fmin(hz, sr * 0.45f)); }
static inline float smoothstep(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }
static inline float smoothstepRange(float e0, float e1, float x) { return smoothstep((x - e0) / (e1 - e0)); }
static inline float softClip(float x) { return std::tanh(x); }
static inline float asymTube(float x, float drive, float bias)
{
    const float pushed = x * drive + bias;
    const float y = std::tanh(pushed);
    const float correction = std::tanh(bias);
    return (y - correction) / (1.0f - 0.32f * std::fabs(correction));
}
static inline float tonePot(float v) { v = clamp01(v); return v < 0.001f ? 0.001f : (v > 0.999f ? 0.999f : v); }

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

// British FMV tone stack — values taken from the OR15 PCB schematic (the modern
// Orange "OR" preamp the OR50 shares): TREBLE 250K (RV3), BASS A500K (RV4, NOT the
// Marshall 1M -> tighter, less flubby lows), MIDDLE 25K, slope 33K, treble cap
// ~1nF (the OR15's 470p||470p, NOT 470pF -> a fatter, lower-acting treble), C2/C3
// 22nF. The thick "Orange" upper-mid voice is added on top in updateFilters().
class Or50ToneStack
{
    float b0=1,b1=0,b2=0,b3=0,a1=0,a2=0,a3=0,x1=0,x2=0,x3=0,y1=0,y2=0,y3=0,sampleRate=48000.0f;
public:
    void reset(){ x1=x2=x3=y1=y2=y3=0.0f; }
    void setSampleRate(float sr){ sampleRate=sr>1000.0f?sr:48000.0f; }
    void update(float treble,float mid,float bass)
    {
        const float t=tonePot(treble),m=tonePot(mid),l=tonePot(bass);
        const float R1=250.0e3f, R2=500.0e3f, R3=25.0e3f, R4=33.0e3f;
        const float C1=1.0e-9f, C2=22.0e-9f, C3=22.0e-9f;
        const float ab0=0.0f;
        const float ab1=t*C1*R1 + m*C3*R3 + l*(C1*R2+C2*R2) + (C1*R3+C2*R3);
        const float ab2=t*(C1*C2*R1*R4+C1*C3*R1*R4) - m*m*(C1*C3*R3*R3+C2*C3*R3*R3)
                      + m*(C1*C3*R1*R3+C1*C3*R3*R3+C2*C3*R3*R3) + l*(C1*C2*R1*R2+C1*C2*R2*R4+C1*C3*R2*R4)
                      + l*m*(C1*C3*R2*R3+C2*C3*R2*R3) + (C1*C2*R1*R3+C1*C2*R3*R4+C1*C3*R3*R4);
        const float ab3=l*m*(C1*C2*C3*R1*R2*R3+C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4)
                      + m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4) + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                      + t*l*C1*C2*C3*R1*R2*R4;
        const float aa0=1.0f;
        const float aa1=(C1*R1+C1*R3+C2*R3+C2*R4+C3*R4) + m*C3*R3 + l*(C1*R2+C2*R2);
        const float aa2=m*(C1*C3*R1*R3-C2*C3*R3*R4+C1*C3*R3*R3+C2*C3*R3*R3) - m*m*(C1*C3*R3*R3+C2*C3*R3*R3)
                      + l*m*(C1*C3*R2*R3+C2*C3*R2*R3) + l*(C1*C2*R2*R4+C1*C2*R1*R2+C1*C3*R2*R4+C2*C3*R2*R4)
                      + (C1*C2*R1*R4+C1*C3*R1*R4+C1*C2*R3*R4+C1*C2*R1*R3+C1*C3*R3*R4+C2*C3*R3*R4);
        const float aa3=l*m*(C1*C2*C3*R1*R2*R3+C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4)
                      + m*(C1*C2*C3*R3*R3*R4+C1*C2*C3*R1*R3*R3-C1*C2*C3*R1*R3*R4) + l*(C1*C2*C3*R1*R2*R4) + C1*C2*C3*R1*R3*R4;
        const float c=2.0f*sampleRate, c2=c*c, c3=c2*c;
        const float nb0=-ab0-ab1*c-ab2*c2-ab3*c3, nb1=-3.0f*ab0-ab1*c+ab2*c2+3.0f*ab3*c3,
                    nb2=-3.0f*ab0+ab1*c+ab2*c2-3.0f*ab3*c3, nb3=-ab0+ab1*c-ab2*c2+ab3*c3;
        const float na0=-aa0-aa1*c-aa2*c2-aa3*c3, na1=-3.0f*aa0-aa1*c+aa2*c2+3.0f*aa3*c3,
                    na2=-3.0f*aa0+aa1*c+aa2*c2-3.0f*aa3*c3, na3=-aa0+aa1*c-aa2*c2+aa3*c3;
        if(std::fabs(na0)<1.0e-30f){ b0=1.0f; b1=b2=b3=a1=a2=a3=0.0f; return; }
        const float i=1.0f/na0; b0=nb0*i; b1=nb1*i; b2=nb2*i; b3=nb3*i; a1=na1*i; a2=na2*i; a3=na3*i;
    }
    float process(float x){ const float y=b0*x+b1*x1+b2*x2+b3*x3-a1*y1-a2*y2-a3*y3;
        x3=x2; x2=x1; x1=x; y3=y2; y2=y1; y1=y; return y; }
};

class DcBlock
{
    float x1=0.0f,y1=0.0f;
public:
    void reset(){ x1=y1=0.0f; }
    float process(float x){ const float y=x-x1+0.995f*y1; x1=x; y1=y; return y; }
};

} // namespace

class Or50Core
{
    float sampleRate = 48000.0f;
    float gain   = kOr50Def[kGain];
    float bass   = kOr50Def[kBass];
    float mid    = kOr50Def[kMiddle];
    float treble = kOr50Def[kTreble];
    float depth  = kOr50Def[kDepth];
    float volume = kOr50Def[kVolume];
    float half   = kOr50Def[kHalf];

    Biquad inputHp, pickupLoad, preBody, stage1Hp, interHp, cathodeLp;
    Or50ToneStack toneStack;
    Biquad depthShelf, midThick, stackMakeupLow, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    float sag = 0.0f;

    static float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }

    void updateFilters()
    {
        const float g = smoothstep(gain);
        const float pushed = smoothstepRange(0.40f, 0.92f, gain);
        const float mPush = smoothstep(volume);

        inputHp.setHighPass(sampleRate, 46.0f + 36.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12000.0f - 1400.0f * pushed + 800.0f * treble, 0.64f);
        preBody.setPeaking(sampleRate, 640.0f + 240.0f * mid, 0.80f, 0.6f + 1.6f * mid);   // Orange upper-mid push
        // stage1->stage2 coupling = the OR15's 1nF cap into the gain pot (~150 Hz):
        // the tight Orange low end going INTO the gain (less flub the harder you push).
        stage1Hp.setHighPass(sampleRate, 150.0f + 45.0f * pushed, 0.70f);
        // stage2->stage3 coupling = the OR15's 4n7 cap into the 470k grid (~72 Hz).
        interHp.setHighPass(sampleRate, 72.0f + 60.0f * pushed, 0.70f);
        cathodeLp.setLowPass(sampleRate, 8800.0f + 1400.0f * treble - 1500.0f * pushed, 0.64f);

        toneStack.update(treble, mid, bass);
        // DEPTH = the bass-cap rotary: a swept low shelf (more depth = bigger lows).
        depthShelf.setLowShelf(sampleRate, 95.0f + 25.0f * depth, 0.72f, eqDb(depth, 9.0f) + 1.0f);
        // the thick Orange FAC midrange
        midThick.setPeaking(sampleRate, 480.0f + 160.0f * mid, 0.60f, -0.6f + 4.4f * mid + 1.2f * pushed);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f, 0.72f, 1.0f - 1.0f * pushed);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1300.0f * treble - 2000.0f * pushed, 0.64f);
        // fixed presence voicing (no presence knob on the OR50)
        presenceShelf.setHighShelf(sampleRate, 2700.0f, 0.78f, 1.8f + 1.0f * treble);

        // Orange PPC 4x12 (thick, midrange-forward, smooth top)
        speakerHp.setHighPass(sampleRate, 86.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 124.0f, 0.84f, 0.9f + 2.1f * bass + 1.1f * depth);
        speakerLowMid.setPeaking(sampleRate, 440.0f + 90.0f * mid, 0.72f, 1.2f + 2.0f * mid);
        speakerBite.setPeaking(sampleRate, 2400.0f + 480.0f * treble, 0.78f, 2.0f + 1.8f * treble - 0.5f * pushed);
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, 9.5f + 2.0f * treble - 4.5f * pushed);
        speakerLp.setLowPass(sampleRate, 15000.0f + 1700.0f * treble - 3500.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); preBody.reset(); stage1Hp.reset(); interHp.reset(); cathodeLp.reset();
        toneStack.reset(); depthShelf.reset(); midThick.reset(); stackMakeupLow.reset(); phaseLp.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset(); speakerFizz.reset(); speakerLp.reset();
        dcBlock.reset(); sag = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; toneStack.setSampleRate(sampleRate); reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kGain:   gain = v; break;
            case kBass:   bass = v; break;
            case kMiddle: mid = v; break;
            case kTreble: treble = v; break;
            case kDepth:  depth = v; break;
            case kVolume: volume = v; break;
            case kHalf:   half = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kOr50Def[i]); }

    float process(float in)
    {
        const float g = smoothstep(gain);
        const float pushed = smoothstepRange(0.40f, 0.92f, gain);
        const float mPush = smoothstep(volume);
        const float halfP = (half >= 0.5f) ? 1.0f : 0.0f;   // HALF power -> earlier breakup

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = softClip(x * (1.05f + 0.08f * pushed)) * (0.96f - 0.04f * pushed);

        // THREE cascaded ECC83 gain stages (GAIN = HF Drive), as on the OR15/OR30
        // schematics — the source of the thick, midrange-forward Orange "chunk".
        // V1-A: first stage, full 22uF cathode bypass -> fat/full gain.
        float y = preBody.process(x);
        y = asymTube(y, 1.10f + 2.2f * gain + 1.4f * g, 0.011f + 0.012f * gain);
        y = stage1Hp.process(y);                                          // 1nF coupling -> tight
        // V1-B: the GAIN pot drives this stage; PARTIAL 1uF cathode bypass (per the
        // OR30 preamp) only lifts mids/highs -> the Orange midrange honk, tighter lows.
        y = asymTube(y, 0.95f + 2.4f * gain + 1.9f * pushed, -0.006f - 0.010f * gain);
        y = interHp.process(y);                                           // 4n7 coupling
        // V2-A: final preamp gain into the tone stack.
        y = asymTube(y, 0.92f + 1.8f * gain + 1.5f * pushed, 0.005f + 0.008f * gain);
        y = cathodeLp.process(y);

        y = toneStack.process(y) * 1.70f;
        y = depthShelf.process(y);
        y = midThick.process(y);
        y = stackMakeupLow.process(y);
        y = phaseLp.process(y);

        // VOLUME (master) into the power amp
        y *= 0.22f + 1.28f * volume;

        // 2x EL34 (~50W) — half the OR100's power: less headroom, earlier breakup,
        // a bit more sag and a tighter low end (smaller iron). HALF -> earlier still.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0060f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.140f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.40f + 0.65f * mPush + 0.40f * halfP));
        const float powerDrive = (0.98f + 1.75f * mPush + 1.05f * pushed + 0.6f * halfP) * sagDrop;
        y = asymTube(y, powerDrive, 0.006f + 0.010f * (treble - bass));
        y = 0.82f * y + 0.18f * softClip(y * (1.6f + 1.2f * pushed + 0.5f * halfP));
        y *= 0.97f - 0.09f * sag;

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizz.process(y);
        y = speakerLp.process(y);

        // Loudness normalization: cleanMakeup keeps RS Gain (-> GAIN) ~flat; VOLUME
        // (master) gives a mild swing. ~-14 dBFS reference.
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 18.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.009f * std::fabs((depth - 0.5f) * 14.0f);
        const float cleanMakeup = 1.0f + 4.0f * std::exp(-gain / 0.21f);
        const float level = (0.55f + 0.12f * (1.0f - gain)) * cleanMakeup /
            ((1.0f + 0.42f * mPush + 0.28f * pushed) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class Or50Plugin : public Plugin
{
    Or50Core left;
    Or50Core right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    Or50Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kOr50Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "CitrusOR50"; }
    const char* getDescription() const override { return "Orange OR50 British EL34 style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('O', 'r', '5', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kOr50Names[index];
        parameter.symbol = kOr50Symbols[index];
        parameter.ranges.min = kOr50Min[index];
        parameter.ranges.max = kOr50Max[index];
        parameter.ranges.def = kOr50Def[index];
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
            outL[i] = rbAmpLvl(0.560f * left.process(3.2f * inL[i]));
            outR[i] = rbAmpLvl(0.560f * right.process(3.2f * inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Or50Plugin)
};

Plugin* createPlugin() { return new Or50Plugin(); }

END_NAMESPACE_DISTRHO
