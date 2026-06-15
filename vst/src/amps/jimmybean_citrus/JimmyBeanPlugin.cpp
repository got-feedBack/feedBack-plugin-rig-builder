/*
 * CITRUS JIMMY BEAN - Orange Jimmy Bean JB150 (1975-76) for the game's
 * Amp_OrangeJimmyBean. Parody brand "Citrus" (Orange -> Citrus); the in-app face
 * must never read "Orange".
 *
 * RECONSTRUCTION (no schematic exists): a ~150W SOLID-STATE (transistor, op-amp)
 * TWIN-CHANNEL head with a built-in TREMOLO and a switchable SUSTAIN circuit,
 * denim/leather styling. It is mostly a clean/loud solid-state amp; the SUSTAIN
 * circuit adds gain/dirt; the TREMOLO is an amplitude LFO.
 *
 * DSP (SOLID-STATE - NO tube sag / asymTube cascade / power-tube model): an
 * op-amp clean preamp -> Baxandall-ish BASS/TREBLE (no MID) -> BRIGHT high-shelf
 * switch -> SUSTAIN (a solid-state compressor + op-amp diode-style soft clip that
 * adds gain + sustain; 0 = clean, up = more dirt/sustain, fuzzy-ish but
 * controlled) -> small solid-state cab -> TREMOLO (an amplitude LFO on the
 * output; SPEED = rate 2..8 Hz via a per-sample phase accumulator, no Date/rand;
 * DEPTH = amount, 0 = OFF). CHANNEL 0/1 = the two channels (Ch2 a touch brighter
 * / more gain).
 *
 * the game: RS Gain -> SUSTAIN (the dirt/sustain); Bass/Treble -> tone (no Mid).
 */
#include "DistrhoPlugin.hpp"
#include "JimmyBeanParams.h"
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

// Baxandall-style boost/cut: a low shelf (BASS) + a high shelf (TREBLE) around
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
        // Baxandall bass: ~+/-13 dB low shelf hinged ~320 Hz.
        bassShelf.setLowShelf(sampleRate, 320.0f, 0.70f, eqDb(b, 13.0f));
        // Baxandall treble: ~+/-13 dB high shelf hinged ~2.2 kHz.
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

class JimmyBeanCore
{
    float sampleRate = 48000.0f;
    float volume  = kJimmyBeanDef[kVolume];
    float bass    = kJimmyBeanDef[kBass];
    float treble  = kJimmyBeanDef[kTreble];
    float sustain = kJimmyBeanDef[kSustain];
    float speed   = kJimmyBeanDef[kSpeed];
    float depth   = kJimmyBeanDef[kDepth];
    float channel = kJimmyBeanDef[kChannel];
    float bright  = kJimmyBeanDef[kBright];

    Biquad inputHp, inputLp;                  // op-amp preamp band-limit
    BaxandallTone tone;                        // BASS/TREBLE tone amp (no MID)
    Biquad briteShelf;                         // BRIGHT switch high-shelf
    Biquad chanShelf;                          // CHANNEL 2 voicing (a touch brighter)
    Biquad sustainHp, sustainLp;               // SUSTAIN band-shaping around the clip
    Biquad speakerHp, speakerThump, speakerBody, speakerLp;  // solid-state cab
    DcBlock dcBlock, dcBlock2;
    // SUSTAIN compressor envelope (solid-state level-dependent gain)
    float env = 0.0f;
    // TREMOLO LFO (per-sample phase accumulator; no Date/rand)
    float lfoPhase = 0.0f;

    void updateFilters()
    {
        const float ch = (channel >= 0.5f) ? 1.0f : 0.0f;   // Ch1 / Ch2

        // op-amp clean preamp: a clean, high-headroom band-pass.
        inputHp.setHighPass(sampleRate, 40.0f, 0.70f);
        inputLp.setLowPass(sampleRate, 9000.0f, 0.66f);

        tone.update(bass, treble);

        // BRIGHT switch: a treble high-shelf boost. Off = flat, on = +6 dB.
        briteShelf.setHighShelf(sampleRate, 3000.0f, 0.72f, (bright >= 0.5f) ? 6.0f : 0.0f);

        // CHANNEL 2: a touch brighter (small top lift) vs Ch1.
        chanShelf.setHighShelf(sampleRate, 2600.0f, 0.74f, ch * 2.5f);

        // SUSTAIN band-shaping: tighten the lows into the soft-clip and tame fizz.
        sustainHp.setHighPass(sampleRate, 90.0f + 60.0f * sustain, 0.70f);
        sustainLp.setLowPass(sampleRate, 6800.0f - 1200.0f * sustain, 0.68f);

        // small solid-state cab. Open the cab lowpass for a miked-cab brightness,
        // with a gain-dependent retreat (VOLUME/SUSTAIN drive proxy) so cranked
        // tones don't fizz.
        const float spkPush = smoothstep(0.5f * volume + 0.5f * sustain);
        speakerHp.setHighPass(sampleRate, 72.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 110.0f, 0.85f, 1.6f + 1.4f * bass);
        speakerBody.setPeaking(sampleRate, 700.0f, 0.80f, -1.2f);   // scoop the boxy mid
        speakerLp.setLowPass(sampleRate, 15000.0f + 1600.0f * treble + (bright >= 0.5f ? 1200.0f : 0.0f) - 3500.0f * spkPush, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset();
        tone.reset(); briteShelf.reset(); chanShelf.reset();
        sustainHp.reset(); sustainLp.reset();
        speakerHp.reset(); speakerThump.reset(); speakerBody.reset(); speakerLp.reset();
        dcBlock.reset(); dcBlock2.reset();
        env = 0.0f; lfoPhase = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; tone.setSampleRate(sampleRate); reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kVolume:  volume = v;  break;
            case kBass:    bass = v;    break;
            case kTreble:  treble = v;  break;
            case kSustain: sustain = v; break;
            case kSpeed:   speed = v;   break;
            case kDepth:   depth = v;   break;
            case kChannel: channel = v; break;
            case kBright:  bright = v;  break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kJimmyBeanDef[i]); }

    float process(float in)
    {
        const float ch = (channel >= 0.5f) ? 1.0f : 0.0f;       // Ch1 / Ch2
        const float sus = smoothstep(sustain);                   // SUSTAIN amount

        // op-amp clean preamp (clean, high headroom)
        float x = inputHp.process(in);
        x = inputLp.process(x);
        // mild first op-amp gain stage (clean - solid-state, no tube asymmetry);
        // Ch2 has a touch more gain.
        x *= 1.5f + 0.4f * ch;

        // TONE AMP (Baxandall BASS/TREBLE, no MID) + BRIGHT + channel voicing
        float y = tone.process(x);
        y = briteShelf.process(y);
        y = chanShelf.process(y);

        // SUSTAIN = a solid-state compressor + op-amp diode-style soft clip that
        // adds gain + sustain. 0 = clean; up = more dirt/sustain (fuzzy-ish but
        // controlled). A peak-follower lifts low-level signal (the "sustain"),
        // then a hard-ish diode soft-clip adds the controlled op-amp dirt.
        if (sus > 0.0001f)
        {
            y = sustainHp.process(y);
            // peak-follower compressor: fast attack, slow release.
            const float rect = std::fabs(y);
            const float attack = 1.0f - std::exp(-1.0f / (0.0030f * sampleRate));
            const float release = 1.0f - std::exp(-1.0f / (0.220f * sampleRate));
            env += (rect - env) * (rect > env ? attack : release);
            // makeup gain that swells as the note decays (the sustain feel).
            const float comp = 1.0f / (1.0f + (3.0f + 9.0f * sus) * env);
            const float susGain = (1.0f + 7.0f * sus) * (0.55f + 0.45f * comp);
            // op-amp diode soft-clip (symmetric tanh = the controlled fuzzy edge).
            const float drive = 1.0f + 6.0f * sus;
            float clipped = std::tanh(y * susGain * drive) / std::tanh(drive);
            // blend so low SUSTAIN stays mostly clean.
            y = (1.0f - 0.85f * sus) * y + (0.85f * sus) * clipped;
            y = sustainLp.process(y);
        }

        y = dcBlock.process(y);

        // small solid-state cab
        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerBody.process(y);
        y = speakerLp.process(y);

        // VOLUME: master output level (solid-state, mostly clean).
        const float vGain = 0.40f + 1.60f * volume;
        y *= vGain;
        // gentle solid-state limit only near the very top of VOLUME travel.
        const float vPush = smoothstepRange(0.85f, 1.0f, volume);
        if (vPush > 0.0f)
            y = y * (1.0f - 0.45f * vPush) + softClip(y * (1.0f + 0.7f * vPush)) * (0.45f * vPush);

        y = dcBlock2.process(y);

        // Loudness normalization: SUSTAIN adds dirt + level, so cleanMakeup carries
        // the low-SUSTAIN compensation and the level settles flat-ish (~-14 dBFS)
        // across the SUSTAIN sweep. toneEnergy keeps big BASS/TREBLE/BRIGHT boosts
        // from running the shared kLvl stage hot.
        const float toneEnergy = 1.0f
            + 0.030f * std::fabs((bass - 0.5f) * 16.0f)
            + 0.018f * std::fabs((treble - 0.5f) * 16.0f)
            + (bright >= 0.5f ? 0.06f : 0.0f)
            + 1.85f * sus * sus + 0.35f * sus;
        const float cleanMakeup = 1.0f + 2.4f * std::exp(-sustain / 0.30f);
        const float level = (1.32f * cleanMakeup) / ((0.70f + 0.55f * smoothstep(volume)) * toneEnergy);
        y = softClip(y * level) * 0.97f;

        // TREMOLO = an amplitude LFO on the output. SPEED = rate 2..8 Hz via a
        // per-sample phase accumulator (no Date/rand). DEPTH = amount; 0 = OFF.
        if (depth > 0.0001f)
        {
            const float rateHz = 2.0f + 6.0f * clamp01(speed);
            lfoPhase += rateHz / sampleRate;
            if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
            const float lfo = 0.5f - 0.5f * std::cos(2.0f * kPi * lfoPhase);  // 0..1
            const float trem = 1.0f - depth * lfo;                            // dip on peaks
            y *= trem;
        }

        return y;
    }
};

class JimmyBeanPlugin : public Plugin
{
    JimmyBeanCore left;
    JimmyBeanCore right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    JimmyBeanPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kJimmyBeanDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "CitrusJimmyBean"; }
    const char* getDescription() const override { return "Citrus Jimmy Bean style solid-state head with sustain + tremolo"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('J', 'b', '1', '5'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kJimmyBeanNames[index];
        parameter.symbol = kJimmyBeanSymbols[index];
        parameter.ranges.min = kJimmyBeanMin[index];
        parameter.ranges.max = kJimmyBeanMax[index];
        parameter.ranges.def = kJimmyBeanDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JimmyBeanPlugin)
};

Plugin* createPlugin() { return new JimmyBeanPlugin(); }

END_NAMESPACE_DISTRHO
