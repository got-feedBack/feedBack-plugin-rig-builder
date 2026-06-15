/*
 * POLYSTONE MINIBRUTE - Polytone Mini Brute (CS-100) for the game's Amp_CS100.
 * Parody brand "Polystone" (Polytone -> Polystone); the in-app face must never
 * read "Polytone".
 *
 * Local reference (modelled component-by-component):
 *   amps/Polytone Mini Brute (CS-100)/Polytone_mini_brute.pdf  (Polytone Mini
 *   Brutes I-IV / Mega Brute preamp + PA378B power amp)
 *
 * A SIMPLE SOLID-STATE jazz combo (the warm clean "jazz box" tone, e.g. Joe
 * Pass). Panel (per the photo), 1:1: BASS, TREBLE, VOLUME + a BRITE switch +
 * Hi/Lo inputs. NO middle control. SOLID-STATE -> no tube sag / asymTube
 * cascades / power-tube models: an op-amp (4558) preamp into a Baxandall-ish
 * Bass/Treble TONE AMP, a VOLUME, a gentle op-amp soft-limit only near full
 * VOLUME (jazz amps stay clean), a dark-ish voicing, and a small solid-state
 * 1x12 combo cab. BRITE = a treble high-shelf boost switch. The full schematic's
 * DIST channel (1N4002 diode clipper) + spring reverb are NOT part of this clean
 * voice and are omitted; the PA378B power amp is a clean transistor push-pull.
 *
 * the game: RS Gain -> VOLUME (the only level/drive; mostly clean), RS Bass ->
 * Bass, RS Treble -> Treble (no Mid on this amp).
 */
#include "DistrhoPlugin.hpp"
#include "MiniBruteParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): kLvl matches the
// amp to the common multitone loudness; the soft knee is transparent below
// +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts never hard-clip.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

namespace {

static constexpr float kPi = 3.14159265359f;
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float clampFreq(float hz, float sr) { return std::fmax(20.0f, std::fmin(hz, sr * 0.45f)); }
static inline float smoothstep(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }
static inline float smoothstepRange(float e0, float e1, float x) { return smoothstep((x - e0) / (e1 - e0)); }
static inline float softClip(float x) { return std::tanh(x); }
static inline float tonePot(float v) { v = clamp01(v); return v < 0.001f ? 0.001f : (v > 0.999f ? 0.999f : v); }
static inline float eqDb(float v, float rangeDb) { return (clamp01(v) - 0.5f) * 2.0f * rangeDb; }

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

// Polytone "TONE AMP" Baxandall-style boost/cut (R9/R65/R11 + C7/C8 .039 bass,
// C13 .0047 / C10 .01 treble): a low shelf (BASS) + a high shelf (TREBLE) around
// the op-amp, both centred so 0.5 = flat. Modelled as two shelving biquads.
class BaxandallTone
{
    Biquad bassShelf, trebleShelf;
    float sampleRate = 48000.0f;
public:
    void reset(){ bassShelf.reset(); trebleShelf.reset(); }
    void setSampleRate(float sr){ sampleRate = sr>1000.0f?sr:48000.0f; }
    void update(float bass, float treble)
    {
        const float b = tonePot(bass), t = tonePot(treble);
        // Baxandall bass: ~+/-13 dB low shelf hinged ~320 Hz (the .039 caps).
        bassShelf.setLowShelf(sampleRate, 320.0f, 0.70f, eqDb(b, 13.0f));
        // Baxandall treble: ~+/-13 dB high shelf hinged ~2.2 kHz (the .0047 cap).
        trebleShelf.setHighShelf(sampleRate, 2200.0f, 0.72f, eqDb(t, 13.0f));
    }
    float process(float x){ return trebleShelf.process(bassShelf.process(x)); }
};

class DcBlock
{
    float x1=0.0f,y1=0.0f;
public:
    void reset(){ x1=y1=0.0f; }
    float process(float x){ const float y=x-x1+0.995f*y1; x1=x; y1=y; return y; }
};

} // namespace

class MiniBruteCore
{
    float sampleRate = 48000.0f;
    float volume = kMiniBruteDef[kVolume];
    float bass   = kMiniBruteDef[kBass];
    float treble = kMiniBruteDef[kTreble];
    float brite  = kMiniBruteDef[kBrite];

    Biquad inputHp, inputLp;           // 4558 preamp band-limit (C2 10pF / coupling)
    BaxandallTone tone;                // BASS/TREBLE TONE AMP
    Biquad briteShelf;                 // BRITE switch high-shelf
    Biquad warmth;                     // dark-ish jazz voicing (gentle low-mid lift / top cut)
    Biquad speakerHp, speakerThump, speakerBody, speakerLp;  // small solid-state 1x12 combo
    DcBlock dcBlock;

    void updateFilters()
    {
        // 4558 op-amp preamp: a clean, high-headroom band-pass. The Mini Brute is
        // a dark jazz box -> roll the very top off early.
        inputHp.setHighPass(sampleRate, 42.0f, 0.70f);
        inputLp.setLowPass(sampleRate, 7800.0f, 0.66f);

        tone.update(bass, treble);

        // BRITE switch: a treble high-shelf boost (the S1 NORM/BRITE jumper that
        // adds top to the otherwise dark voice). Off = flat, on = +6 dB / ~3 kHz.
        briteShelf.setHighShelf(sampleRate, 3000.0f, 0.72f, (brite >= 0.5f) ? 6.0f : 0.0f);

        // Dark jazz-box voicing: a warm low-mid presence + a gentle upper-mid dip
        // so single-notes stay round (the classic Polytone "thump").
        warmth.setPeaking(sampleRate, 240.0f, 0.80f, 1.6f);

        // Small solid-state 1x12 combo cab. Open the cab lowpass for a miked-cab
        // brightness, with a gain-dependent retreat (VOLUME is the only drive) so
        // a cranked tone doesn't get fizzy.
        const float spkPush = smoothstep(volume);
        speakerHp.setHighPass(sampleRate, 75.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 110.0f, 0.85f, 1.8f + 1.4f * bass);
        speakerBody.setPeaking(sampleRate, 720.0f, 0.80f, -1.4f);   // scoop the boxy mid
        speakerLp.setLowPass(sampleRate, 14000.0f + 1600.0f * treble + (brite >= 0.5f ? 1400.0f : 0.0f) - 3500.0f * spkPush, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset();
        tone.reset(); briteShelf.reset(); warmth.reset();
        speakerHp.reset(); speakerThump.reset(); speakerBody.reset(); speakerLp.reset();
        dcBlock.reset();
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; tone.setSampleRate(sampleRate); reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kVolume: volume = v; break;
            case kBass:   bass = v;   break;
            case kTreble: treble = v; break;
            case kBrite:  brite = v;  break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kMiniBruteDef[i]); }

    float process(float in)
    {
        // 4558 op-amp preamp (clean, high headroom)
        float x = inputHp.process(in);
        x = inputLp.process(x);
        // a mild first op-amp gain stage (clean - solid-state, no tube asymmetry)
        x *= 1.6f;

        // TONE AMP (Baxandall BASS/TREBLE) + BRITE + dark voicing
        float y = tone.process(x);
        y = briteShelf.process(y);
        y = warmth.process(y);

        // VOLUME: the only level/drive. Jazz amps stay clean - this is mostly a
        // clean gain; only near full VOLUME does the op-amp gently soft-limit.
        const float vPush = smoothstepRange(0.78f, 1.0f, volume);   // 0 until ~0.78
        const float vGain = 0.45f + 2.10f * volume;
        y *= vGain;
        // gentle op-amp soft-limit only near the top of the VOLUME travel
        if (vPush > 0.0f)
            y = y * (1.0f - 0.55f * vPush) + softClip(y * (1.0f + 0.9f * vPush)) * (0.55f * vPush);

        y = dcBlock.process(y);

        // small solid-state 1x12 combo cab
        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerBody.process(y);
        y = speakerLp.process(y);

        // Loudness normalization: VOLUME is the only drive but the amp stays
        // ~clean, so cleanMakeup carries the low-VOLUME compensation and the level
        // settles flat-ish (~-14 dBFS) across the VOLUME sweep. toneEnergy keeps
        // big BASS/TREBLE boosts from running the shared kLvl stage hot.
        const float toneEnergy = 1.0f
            + 0.030f * std::fabs((bass - 0.5f) * 16.0f)
            + 0.018f * std::fabs((treble - 0.5f) * 16.0f)
            + (brite >= 0.5f ? 0.06f : 0.0f);
        const float cleanMakeup = 1.0f + 2.6f * std::exp(-volume / 0.34f);
        const float level = (1.40f * cleanMakeup) / ((0.70f + 0.55f * smoothstep(volume)) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class MiniBrutePlugin : public Plugin
{
    MiniBruteCore left;
    MiniBruteCore right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    MiniBrutePlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kMiniBruteDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "PolystoneMiniBrute"; }
    const char* getDescription() const override { return "Polytone Mini Brute style solid-state jazz combo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('P', 'm', 'b', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMiniBruteNames[index];
        parameter.symbol = kMiniBruteSymbols[index];
        parameter.ranges.min = kMiniBruteMin[index];
        parameter.ranges.max = kMiniBruteMax[index];
        parameter.ranges.def = kMiniBruteDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniBrutePlugin)
};

Plugin* createPlugin() { return new MiniBrutePlugin(); }

END_NAMESPACE_DISTRHO
