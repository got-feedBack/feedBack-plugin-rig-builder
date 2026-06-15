/*
 * UNPARALLEL DC30 — Matchless DC30 for the game's Amp_BT30. Parody brand; the
 * in-app face must never read "Matchless".
 *
 * A hand-wired AC30-class boutique combo: TWO independent channels into a shared
 * 4xEL84 CLASS-A power amp (~30W), NO global negative feedback -> chimey, jangly,
 * blooms & compresses at high volume.
 *
 *   CHANNEL 1 "Brilliant" (two 12AX7 stages) -> a VOX TOP-BOOST tone stack with
 *     TREBLE (220k, 56pF treble cap) and BASS (1M, .022) only, no mid. Bright.
 *   CHANNEL 2 "EF86" (one EF86 pentode, higher gain, fatter/darker) -> a 6-pos
 *     TONE rotary modelled as a continuous dark(fat)->bright sweep. Thick mids.
 *   Shared: CUT (post/PI treble cut, higher = darker) + MASTER.
 *
 * the game: RS Gain -> CH1 VOLUME (drives the EL84 breakup), Channel pinned to
 * Ch1 Brilliant via _static; Bass/Treble -> the Ch1 top-boost stack. The DSP
 * channel-select morphs between the Ch1 top-boost voice and the Ch2 EF86 voice.
 * Class-A EL84: gentle even-harmonic soft clip, compression that rises with
 * Master; no harsh fizz.
 */
#include "DistrhoPlugin.hpp"
#include "Dc30Params.h"
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

// VOX-style TOP-BOOST stack for Ch1 "Brilliant": a Treble+Bass-only top-boost
// (no mid control on the DC30 Ch1). Reuses the 3rd-order bilinear Fender-derived
// network with the DC30 top-boost component values: TREBLE 220k / treble cap
// 56pF, BASS 1M / .022uF, a tiny slope R + .022uF bass-stage cap. The MIDDLE leg
// is pinned to a small fixed value so the stack behaves as Treble+Bass only.
class MarkToneStack
{
    float b0=1,b1=0,b2=0,b3=0,a1=0,a2=0,a3=0,x1=0,x2=0,x3=0,y1=0,y2=0,y3=0,sampleRate=48000.0f;
public:
    void reset(){ x1=x2=x3=y1=y2=y3=0.0f; }
    void setSampleRate(float sr){ sampleRate=sr>1000.0f?sr:48000.0f; }
    void update(float treble,float mid,float bass)
    {
        const float t=tonePot(treble),m=tonePot(mid),l=tonePot(bass);
        const float R1=220.0e3f, R2=1.0e6f, R3=10.0e3f, R4=47.0e3f;
        const float C1=56.0e-12f, C2=22.0e-9f, C3=22.0e-9f;
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

class Dc30Core
{
    float sampleRate = 48000.0f;
    float ch1vol  = kDc30Def[kCh1Volume];
    float bass    = kDc30Def[kBass];
    float treble  = kDc30Def[kTreble];
    float ch2vol  = kDc30Def[kCh2Volume];
    float tone    = kDc30Def[kTone];
    float cut     = kDc30Def[kCut];
    float master  = kDc30Def[kMaster];
    float channel = kDc30Def[kChannel];

    // derived
    float chan = 0.0f, drv = 0.5f;

    Biquad inputHp, pickupLoad;
    // CH1 "Brilliant" top-boost path
    MarkToneStack topBoost;
    Biquad ch1Bright, ch1InterHp;
    // CH2 "EF86" path
    Biquad ef86Body, ef86ToneLp, ef86ToneShelf, ch2InterHp;
    // shared
    Biquad cutShelf, phaseLp;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerChime, speakerFizz, speakerLp;
    DcBlock dcBlock;
    float sag = 0.0f;

    void updateFilters()
    {
        chan = smoothstep(channel);                               // 0 = Ch1 Brilliant, 1 = Ch2 EF86
        // The RS-mapped drive: Ch1 Volume on Ch1, Ch2 Volume on Ch2.
        drv  = clamp01((1.0f - chan) * ch1vol + chan * ch2vol);
        const float g = smoothstep(drv);
        const float pushed = smoothstepRange(0.40f, 0.95f, drv);

        // input network (68k/1M grid-leak + Miller pickup load)
        inputHp.setHighPass(sampleRate, 30.0f + 18.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 13500.0f - 1200.0f * pushed + 700.0f * treble, 0.62f);

        // CH1 "Brilliant": VOX top-boost (Treble + Bass only, mid pinned), glassy.
        topBoost.update(treble, 0.18f, bass);
        // 560pF+180pF treble-bypass coupling -> a bright shelf that opens at low vol
        ch1Bright.setHighShelf(sampleRate, 2000.0f, 0.72f, 4.5f - 3.0f * ch1vol);
        ch1InterHp.setHighPass(sampleRate, 90.0f + 90.0f * pushed, 0.70f);

        // CH2 "EF86": fatter/darker pentode body + the 6-position TONE rotary as a
        // continuous dark(fat 360p/.01)->bright(56p) sweep.
        ef86Body.setPeaking(sampleRate, 520.0f, 0.80f, 2.2f + 1.4f * ch2vol);  // thick midrange
        ef86ToneLp.setLowPass(sampleRate, 1600.0f + 6500.0f * tone, 0.66f);    // rotary darkness
        ef86ToneShelf.setHighShelf(sampleRate, 2600.0f, 0.72f, -3.0f + 6.0f * tone);
        ch2InterHp.setHighPass(sampleRate, 70.0f + 70.0f * pushed, 0.70f);

        // Shared CUT (250kA, post/PI treble cut -> higher = darker)
        cutShelf.setHighShelf(sampleRate, 2400.0f, 0.74f, -1.0f - 9.0f * cut);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1500.0f * treble - 2000.0f * pushed, 0.62f);

        // AC30-class 2x12 "blue" voicing — chimey, jangly, scooped-bright.
        speakerHp.setHighPass(sampleRate, 85.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 120.0f, 0.84f, 0.5f + 1.8f * bass);
        speakerLowMid.setPeaking(sampleRate, 430.0f, 0.78f, -0.6f + 1.0f * chan);  // EF86 a touch fatter
        speakerChime.setPeaking(sampleRate, 3200.0f + 500.0f * treble, 0.80f, 3.2f + 2.2f * treble - 0.5f * pushed);
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, 9.5f + 2.0f * treble - 4.5f * pushed);
        speakerLp.setLowPass(sampleRate, 16000.0f + 1800.0f * treble - 3500.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset();
        topBoost.reset(); ch1Bright.reset(); ch1InterHp.reset();
        ef86Body.reset(); ef86ToneLp.reset(); ef86ToneShelf.reset(); ch2InterHp.reset();
        cutShelf.reset(); phaseLp.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerChime.reset(); speakerFizz.reset(); speakerLp.reset();
        dcBlock.reset(); sag = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; topBoost.setSampleRate(sampleRate); reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kCh1Volume: ch1vol = v; break;
            case kBass:      bass = v; break;
            case kTreble:    treble = v; break;
            case kCh2Volume: ch2vol = v; break;
            case kTone:      tone = v; break;
            case kCut:       cut = v; break;
            case kMaster:    master = v; break;
            case kChannel:   channel = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kDc30Def[i]); }

    float process(float in)
    {
        const float g = smoothstep(drv);
        const float pushed = smoothstepRange(0.40f, 0.95f, drv);
        const float mPush = smoothstep(master);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);

        // ---- CH1 "Brilliant": two 12AX7 stages, top-boost stack between them ----
        float c1 = asymTube(x, 1.05f + 0.7f * g, 0.008f);          // stage1 (220k plate, 25uF bypass)
        c1 = ch1Bright.process(c1);                                // 560p+180p bright coupling
        c1 = topBoost.process(c1) * 2.0f;                          // VOX top-boost (Treble+Bass)
        c1 = ch1InterHp.process(c1);
        c1 = asymTube(c1 * (0.6f + 2.4f * ch1vol), 1.1f + 2.0f * ch1vol, 0.010f); // stage2 (100k plate)
        c1 *= 0.85f;

        // ---- CH2 "EF86": one pentode (higher gain, fatter/darker) + tone rotary ----
        float c2 = asymTube(x * (0.7f + 2.8f * ch2vol), 1.4f + 3.2f * ch2vol, 0.014f); // EF86 pentode
        c2 = ef86Body.process(c2);                                 // thick midrange (330k+2M2 plate)
        c2 = ef86ToneLp.process(c2);                               // 6-pos rotary darkness
        c2 = ef86ToneShelf.process(c2);
        c2 = ch2InterHp.process(c2);
        c2 *= 0.78f;

        // channel select morph
        float y = chan * c2 + (1.0f - chan) * c1;

        // shared CUT (post/PI treble cut)
        y = cutShelf.process(y);
        y = phaseLp.process(y);

        // 4xEL84 CLASS-A power amp (~30W), NO global NFB: gentle even-harmonic
        // soft clip, bloom & compression that increase with MASTER, no harsh fizz.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0070f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.170f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.34f + 0.78f * mPush + 0.40f * pushed));
        const float powerDrive = (0.78f + 1.70f * mPush + 1.10f * pushed) * sagDrop;
        // even-harmonic class-A bias (asymmetric, no NFB -> chimey bloom)
        y = asymTube(y, powerDrive, 0.020f + 0.010f * mPush);
        y = 0.82f * y + 0.18f * softClip(y * (1.3f + 0.9f * mPush));
        y *= 0.96f - 0.10f * sag;

        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerChime.process(y);
        y = speakerFizz.process(y);
        y = speakerLp.process(y);

        // Loudness normalization: the RS-gain (Ch1 Volume) adds saturation, not
        // level; cleanMakeup carries the drive compensation (folded into 'level')
        // so the gain sweep stays flat ~-14 dBFS and the clean edge is only a few
        // dB quieter. Master keeps a mild swing.
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 16.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 16.0f)
            + 0.008f * std::fabs((tone - 0.5f) * 12.0f) * chan;
        const float cleanMakeup = 1.0f + 6.4f * std::exp(-drv / 0.26f);
        const float level = 0.90f * cleanMakeup / ((1.0f + 0.42f * mPush) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class Dc30Plugin : public Plugin
{
    Dc30Core left;
    Dc30Core right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    Dc30Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kDc30Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "UnparallelDC30"; }
    const char* getDescription() const override { return "Matchless DC30 style class-A boutique combo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('U', 'd', '3', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDc30Names[index];
        parameter.symbol = kDc30Symbols[index];
        parameter.ranges.min = kDc30Min[index];
        parameter.ranges.max = kDc30Max[index];
        parameter.ranges.def = kDc30Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dc30Plugin)
};

Plugin* createPlugin() { return new Dc30Plugin(); }

END_NAMESPACE_DISTRHO
