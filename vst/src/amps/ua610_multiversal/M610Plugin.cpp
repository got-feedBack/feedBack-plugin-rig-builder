/*
 * MULTIVERSAL 610 - Universal Audio 610-A tube DI/preamp for the game's
 * DI_Amp_TubePre. Parody brand "Multiversal"; the in-app face must never read
 * "Universal Audio" / "UA".
 *
 * Local reference (modelled component-by-component):
 *   amps/UA 610 Preamp (TubePre)/610-A UA 610 Preamp Schematic.pdf
 *   amps/UA 610 Preamp (TubePre)/Universal-Audio-610-Notes.pdf
 *
 * Topology per the schematic (see M610Params.h for the full breakdown):
 *   T1 UTC O-1 input transformer (Mic = full 1:7-ish step-up, Line = padded
 *   via the 15K/27R network) -> V1 12AX7 cascade (270K/100K plates, .047uF
 *   coupling, 1M2 grid leaks) -> S1 Lo/Hi gain divider (39K/68K + .2uF) ->
 *   passive stepped HF/LF shelf EQ (mapped to continuous +/-6 dB knobs) ->
 *   V2 12AY7 (lower-mu, cleaner; 250K plate, 4K7 / 560R cathodes) ->
 *   T2 UTC PA-5946 30K:600 output transformer (~7 mA standing current; the
 *   low-end limit lives here) -> Line Out. A DI: NO speaker / cab voicing.
 *
 * the game: RS Gain -> Gain; RS Bass -> Low EQ; RS Treble -> High EQ.
 */
#include "DistrhoPlugin.hpp"
#include "M610Params.h"
#include "../../_shared/tube_stage.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts
// never hard-clip.
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

class M610Core
{
    float sampleRate = 48000.0f;
    float gain   = kM610Def[kGain];
    float lowEq  = kM610Def[kLowEq];
    float highEq = kM610Def[kHighEq];
    float level  = kM610Def[kLevel];
    float hiGain = kM610Def[kHiGain];
    float micIn  = kM610Def[kMicLine];

    Biquad inTrafoHp;      // T1 UTC O-1 low corner
    Biquad inTrafoIron;    // small LF iron bump
    Biquad inTrafoLp;      // transformer top-end limit
    Biquad lfShelf;        // S3 LF stepped shelf (continuous)
    Biquad hfShelf;        // S2 HF stepped shelf (continuous)
    Biquad outTrafoHp;     // T2 PA-5946 low-end limit (7 mA -> inductance drop)
    Biquad outTrafoLp;     // output iron top
    DcBlock dcBlock;
    rbtube::TubeStage    v1;   // V1 12AX7 — REAL Koren plate-transfer stage
    rbtube::TubeStageAY7 v2;   // V2 12AY7 — REAL Koren (lower-mu, cleaner)
    rbtube::Miller12AX7  v1Miller;
    rbtube::Miller12AY7  v2Miller;
    rbtube::CouplingCapGridLeak coupleV1ToV2; // passive EQ/coupling -> 12AY7 grid

    void updateFilters()
    {
        // T1 UTC O-1: good iron - low corner ~25 Hz with a touch of LF bloom,
        // top end gently limited ~16 kHz.
        inTrafoHp.setHighPass(sampleRate, 25.0f, 0.72f);
        inTrafoIron.setPeaking(sampleRate, 70.0f, 0.85f, 0.7f);
        inTrafoLp.setLowPass(sampleRate, 16000.0f, 0.70f);

        // Passive stepped shelf EQ mapped to continuous knobs (0.5 = flat).
        // LF: C13 4n7/C16 10n/C17 2n + 270K legs -> shelf hinged ~110 Hz, -6..+6.
        // HF: C10 100p/C12 47p/C9 4n7 + 220K/100K -> shelf hinged ~7 kHz, -6..+6.
        lfShelf.setLowShelf(sampleRate, 110.0f, 0.72f, eqDb(lowEq, 6.0f));
        hfShelf.setHighShelf(sampleRate, 7000.0f, 0.72f, eqDb(highEq, 6.0f));

        // T2 UTC PA-5946 (30K:600, ~7 mA standing current): the low-end limit
        // of the module lives here (inductance drops with current).
        outTrafoHp.setHighPass(sampleRate, 38.0f, 0.74f);
        outTrafoLp.setLowPass(sampleRate, 17500.0f, 0.70f);

        // V1 12AX7 + V2 12AY7 — REAL Koren plate-transfer stages (cathode-biased,
        // self-bias solved), same framework as the amps. High-headroom config so
        // the 610 stays a clean DI at normal Gain; tube grit only when cranked/Hi.
        v1.set(sampleRate, 1, 250.0f, 40.0f, 25.0f, 1500.0f);   // 12AX7
        v2.set(sampleRate, 0, 250.0f, 40.0f, 20.0f, 1500.0f);   // 12AY7
        v1Miller.set(sampleRate, 50000.0f, 55.0f, 8.0f);         // transformer/pad source + 12AX7 Miller
        v2Miller.set(sampleRate, 180000.0f, 24.0f, 8.0f);        // passive EQ source + 12AY7 Miller
        coupleV1ToV2.set(sampleRate, 470000.0f, 100.0e-9f, 180000.0f,
                         0.16f, 0.22f, 0.55f);
    }

public:
    void reset()
    {
        inTrafoHp.reset(); inTrafoIron.reset(); inTrafoLp.reset();
        lfShelf.reset(); hfShelf.reset();
        outTrafoHp.reset(); outTrafoLp.reset();
        dcBlock.reset();
        v1Miller.reset(); v2Miller.reset();
        coupleV1ToV2.reset();
        v1.reset(); v2.reset();
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kGain:    gain = v;   break;
            case kLowEq:   lowEq = v;  break;
            case kHighEq:  highEq = v; break;
            case kLevel:   level = v;  break;
            case kHiGain:  hiGain = v; break;
            case kMicLine: micIn = v;  break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kM610Def[i]); }

    float process(float in)
    {
        const bool hi  = hiGain >= 0.5f;
        const bool mic = micIn  >= 0.5f;
        const float g   = smoothstep(gain);
        const float hot = smoothstepRange(0.55f, 0.97f, gain);

        // --- T1 input transformer: Line = padded (15K/27R network), Mic = the
        //     full step-up (hotter into V1). ---
        float x = inTrafoLp.process(inTrafoIron.process(inTrafoHp.process(in)));
        x *= mic ? 1.8f : 1.0f;

        // --- V1/V2 12AX7+12AY7 — modelled as a CLEAN, transparent DI by default.
        //     This unit is the game's acoustic-guitar DI (used by hundreds of
        //     acoustic tones), so at normal Gain it must stay clean; the tube
        //     colour/grit only emerges as Gain is cranked (or Hi Gain engaged).
        //     We blend a near-linear clean path with a gentle asymmetric tube
        //     curve, the blend amount tracking how hard the stage is driven. ---
        const float drive = (2.5f + 4.0f * g + 7.0f * hot) * (hi ? 1.6f : 1.0f);
        float y = v1.process(v1Miller.process(x) * drive);   // REAL 12AX7 (clean at normal Gain, grit when cranked)

        // --- passive stepped shelf EQ (between V1 and V2) ---
        y = lfShelf.process(y);
        y = hfShelf.process(y);

        // --- V2 12AY7: lower-mu, high-headroom — clean makeup glue (transparent;
        //     the output stage + level bound it, no per-stage tube saturation). ---
        y = coupleV1ToV2.process(y, 0.75f + 0.35f * g + 0.55f * hot);
        y = v2.process(v2Miller.process(y) * 1.3f);   // REAL 12AY7 (lower-mu clean makeup)

        // --- T2 output transformer (the DI line out; no speaker) ---
        y = dcBlock.process(y);
        y = outTrafoHp.process(y);
        // output-iron 2nd-harmonic: kept near-transparent at clean settings,
        // only blooming as the unit is cranked (scaled by `hot`).
        y = (1.0f - 0.10f * hot) * y + (0.10f * hot) * softClip(y * 1.4f);
        y = outTrafoLp.process(y);

        // --- Level + loudness normalization: cleanMakeup keeps the low-Gain
        //     end at the reference; the lift is CAPPED past breakup so cranking
        //     Gain never blasts (the DC30 lesson). ---
        const float toneEnergy = 1.0f
            + 0.022f * std::fabs((lowEq - 0.5f) * 12.0f)
            + 0.022f * std::fabs((highEq - 0.5f) * 12.0f)
            + (mic ? 0.10f : 0.0f) + (hi ? 0.16f : 0.0f);
        // The clean path doesn't self-compress, so the Gain-driven level growth
        // shows straight through; divide by (a power of) the drive proxy to hold
        // the output ~flat across the Gain sweep (so cranking Gain doesn't blast).
        const float preComp = 0.85f + 0.95f * g + 1.50f * hot;
        const float lvl = (0.70f * (0.50f + 0.70f * level)) / (preComp * toneEnergy);
        return softClip(y * lvl) * 0.97f;
    }
};

class M610Plugin : public Plugin
{
    M610Core left;
    M610Core right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    M610Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kM610Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Multiversal610"; }
    const char* getDescription() const override { return "Universal Audio 610 style tube DI preamp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', '6', '1', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kM610Names[index];
        parameter.symbol = kM610Symbols[index];
        parameter.ranges.min = kM610Min[index];
        parameter.ranges.max = kM610Max[index];
        parameter.ranges.def = kM610Def[index];
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
            // 1.25 lands the default operating point at the fleet's ~-16 dBFS
            // RMS reference (was 0.560 = -23.8 dBFS: a clean DI keeps the full
            // ~15 dB guitar crest, so equal-peak trimming left it ~8 dB quieter
            // than the compressed amps). Peaks sit ~-1.3 dBFS, under the
            // rbAmpLvl knee.
            outL[i] = rbAmpLvl(1.25f * left.process(inL[i]));
            outR[i] = rbAmpLvl(1.25f * right.process(inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(M610Plugin)
};

Plugin* createPlugin() { return new M610Plugin(); }

END_NAMESPACE_DISTRHO
