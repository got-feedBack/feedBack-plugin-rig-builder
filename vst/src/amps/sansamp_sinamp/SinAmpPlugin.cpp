/*
 * SINAMP BASS DRIVER - Tech 21 SansAmp Bass Driver DI (V2) for the game's
 * DI_Amp_BassDriver. Parody brand "SinAmp"; the in-app face must never read
 * "Tech 21" / "SansAmp".
 *
 * Local reference (modelled component-by-component):
 *   amps/TECH21 Sansamp Bass Driver (BassDriver)/sansamp_v2_mod.pdf
 *
 * An ANALOG SOLID-STATE tube-amp emulator in a DI box (see SinAmpParams.h for
 * the stage-by-stage breakdown): TLC2272 op-amp chain, PRESENCE = pre-clip HF
 * boost (VR1 + C5 10n/C7 100p), DRIVE into D4/D5 3.3V zener clippers (R15
 * 220K feedback), fixed post-clip "speaker emulation" filters (U3), BLEND
 * between the clean input buffer and the amp path, BASS/MID/TREBLE active
 * tone (+ BASS-SHIFT 80->40 Hz, MID-SHIFT 1k->500 Hz), LEVEL. A DI: the
 * speaker-ish voicing is part of the box itself.
 *
 * the game: RS Gain -> Drive; RS Bass -> Bass; RS Mid -> Mid;
 * RS Treble -> Treble; RS Pres -> Presence.
 */
#include "DistrhoPlugin.hpp"
#include "SinAmpParams.h"
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
static inline float softClip(float x) { return std::tanh(x); }
static inline float eqDb(float v, float rangeDb) { return (clamp01(v) - 0.5f) * 2.0f * rangeDb; }

// 3.3V zener pair around the op-amp: harder knee than a tube but still
// rounded - a tanh core with a touch of hard limit blended in.
static inline float zenerClip(float x)
{
    const float t = std::tanh(x);
    const float h = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
    return 0.78f * t + 0.22f * h;
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

class DcBlock
{
    float x1=0.0f,y1=0.0f;
public:
    void reset(){ x1=y1=0.0f; }
    float process(float x){ const float y=x-x1+0.995f*y1; x1=x; y1=y; return y; }
};

} // namespace

class SinAmpCore
{
    float sampleRate = 48000.0f;
    float drive    = kSinAmpDef[kDrive];
    float presence = kSinAmpDef[kPresence];
    float blend    = kSinAmpDef[kBlend];
    float bass     = kSinAmpDef[kBass];
    float mid      = kSinAmpDef[kMid];
    float treble   = kSinAmpDef[kTreble];
    float level    = kSinAmpDef[kLevel];
    float bShift   = kSinAmpDef[kBassShift];
    float mShift   = kSinAmpDef[kMidShift];

    Biquad bufferHp;                   // U1B input buffer coupling
    Biquad presShelf;                  // VR1 PRESENCE pre-clip HF boost
    Biquad postDeEmph;                 // post-clip de-emphasis (smooths the zener edge)
    Biquad spkLow, spkScoop, spkLp;    // U3 fixed "speaker emulation" filters
    Biquad bassShelf, midPeak, trebShelf;  // VR7/VR5/VR6 active tone
    DcBlock dcBlock;

    void updateFilters()
    {
        bufferHp.setHighPass(sampleRate, 18.0f, 0.71f);

        // VR1 PRESENCE: 0..+12 dB high shelf hinged ~1.2 kHz, BEFORE the
        // clippers (so more presence = more edge/definition in the clip).
        presShelf.setHighShelf(sampleRate, 1200.0f, 0.70f, 12.0f * clamp01(presence));
        // matching post-clip de-emphasis (fixed): keeps the loudness/voicing
        // balanced while the harmonic content tracks the presence boost.
        postDeEmph.setHighShelf(sampleRate, 1400.0f, 0.70f, -4.5f * clamp01(presence));

        // U3 fixed post filters - the SansAmp "speaker" voice: a low warmth
        // bump, a slight boxy-mid scoop and the trademark HF rolloff.
        spkLow.setPeaking(sampleRate, 120.0f, 0.85f, 2.0f);
        spkScoop.setPeaking(sampleRate, 800.0f, 0.90f, -2.2f);
        spkLp.setLowPass(sampleRate, 4000.0f, 0.68f);

        // Active tone (V2 panel): BASS +/-12 @ 80 Hz (shift -> 40 Hz),
        // MID +/-12 @ 1 kHz (shift -> 500 Hz), TREBLE +/-12 @ 3.2 kHz.
        bassShelf.setLowShelf(sampleRate, (bShift >= 0.5f) ? 40.0f : 80.0f, 0.71f, eqDb(bass, 12.0f));
        midPeak.setPeaking(sampleRate, (mShift >= 0.5f) ? 500.0f : 1000.0f, 0.90f, eqDb(mid, 12.0f));
        trebShelf.setHighShelf(sampleRate, 3200.0f, 0.71f, eqDb(treble, 12.0f));
    }

public:
    void reset()
    {
        bufferHp.reset(); presShelf.reset(); postDeEmph.reset();
        spkLow.reset(); spkScoop.reset(); spkLp.reset();
        bassShelf.reset(); midPeak.reset(); trebShelf.reset();
        dcBlock.reset();
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kDrive:     drive = v;    break;
            case kPresence:  presence = v; break;
            case kBlend:     blend = v;    break;
            case kBass:      bass = v;     break;
            case kMid:       mid = v;      break;
            case kTreble:    treble = v;   break;
            case kLevel:     level = v;    break;
            case kBassShift: bShift = v;   break;
            case kMidShift:  mShift = v;   break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kSinAmpDef[i]); }

    float process(float in)
    {
        const float d = smoothstep(drive);

        // --- U1B clean input buffer (the BLEND dry leg) ---
        const float clean = bufferHp.process(in);

        // --- amp-emulation path: PRESENCE pre-emphasis -> DRIVE -> zeners ---
        float x = presShelf.process(clean);
        const float driveGain = 1.4f + 22.0f * d;          // VR2 + R15 220K range
        float y = zenerClip(x * driveGain);
        y = postDeEmph.process(y);

        // --- U3 fixed speaker-emulation voicing (part of the box) ---
        y = spkLow.process(y);
        y = spkScoop.process(y);
        y = spkLp.process(y);

        // Gain-compensate the wet leg so BLEND stays usable at every drive:
        // the zener output is ~unit amplitude regardless of drive, while the
        // clean leg follows the input. Bring the wet leg near the clean ref.
        y *= 0.55f;

        // --- VR3 BLEND ---
        const float b = clamp01(blend);
        float out = y * b + clean * (1.0f - b) * 0.85f;

        // --- active tone + LEVEL ---
        out = bassShelf.process(out);
        out = midPeak.process(out);
        out = trebShelf.process(out);
        out = dcBlock.process(out);

        // Loudness: the zener stage self-limits, so the wet path is already
        // ~flat vs Drive; cleanMakeup lifts only the very low-Drive end.
        const float toneEnergy = 1.0f
            + 0.026f * std::fabs((bass - 0.5f) * 16.0f)
            + 0.020f * std::fabs((mid - 0.5f) * 16.0f)
            + 0.020f * std::fabs((treble - 0.5f) * 16.0f)
            + 0.05f * clamp01(presence);
        const float cleanMakeup = 1.0f + 1.1f * std::exp(-drive / 0.22f);
        const float lvl = (0.82f * cleanMakeup * (0.50f + 0.70f * level)) / toneEnergy;
        return softClip(out * lvl) * 0.97f;
    }
};

class SinAmpPlugin : public Plugin
{
    SinAmpCore left;
    SinAmpCore right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    SinAmpPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kSinAmpDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "SinAmpBassDriver"; }
    const char* getDescription() const override { return "Tech 21 SansAmp Bass Driver DI style analog bass preamp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'b', 'd', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kSinAmpNames[index];
        parameter.symbol = kSinAmpSymbols[index];
        parameter.ranges.min = kSinAmpMin[index];
        parameter.ranges.max = kSinAmpMax[index];
        parameter.ranges.def = kSinAmpDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SinAmpPlugin)
};

Plugin* createPlugin() { return new SinAmpPlugin(); }

END_NAMESPACE_DISTRHO
