/*
 * CITRUS AD50 - Orange AD50 (Custom Shop) for the game's Amp_OrangeAD50.
 * Parody brand "Citrus"; the in-app face must never read "Orange".
 *
 * The AD50 is a simple British EL34 tube head — Orange describes it as aiming to
 * be "like the old OR120 with more gain than the AD30". A SIMPLE circuit: one GAIN
 * control (hotter preamp than the OR-series), a 2-BAND EQ (BASS + TREBLE only, NO
 * middle), a PRESENCE (power-amp NFB), a MASTER, plus a footswitchable SUSTAIN
 * boost (an EQ-bypass gain/sustain boost) and a Class A / AB power switch (50W
 * Class AB / 30W Class A — Class A breaks up earlier and compresses more).
 *
 * Lineage: this reuses the Citrus OR50 preamp/power voice (the same modern Orange
 * EL34 lineage) but:
 *   - drops the FMV tone stack + Middle/Depth controls for a 2-band shelving EQ
 *     (Bass low-shelf + Treble high-shelf),
 *   - adds a hotter GAIN preamp stage (more preamp gain than the OR),
 *   - makes PRESENCE a controllable power-amp NFB high-shelf (the OR50's was fixed),
 *   - adds SUSTAIN (>=0.5 = an EQ-bypass gain/sustain boost = extra drive) and the
 *     Class A/AB power switch (ClassA>=0.5 -> earlier breakup, more compression).
 *
 * the game (Amp_OrangeAD50): RS Gain -> GAIN; Bass/Treble -> EQ; Presence ->
 * Presence. Master/Sustain/ClassA via _static defaults; all editable by hand.
 */
#include "DistrhoPlugin.hpp"
#include "Ad50Params.h"
#include "../../_shared/tube_stage.hpp"
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

class Ad50Core
{
    float sampleRate = 48000.0f;
    float gain     = kAd50Def[kGain];
    float bass     = kAd50Def[kBass];
    float treble   = kAd50Def[kTreble];
    float presence = kAd50Def[kPresence];
    float master   = kAd50Def[kMaster];
    float sustain  = kAd50Def[kSustain];
    float classa   = kAd50Def[kClassA];
    float cabSim   = kAd50Def[kCabSim];

    Biquad inputHp, pickupLoad, preBody, stage1Hp, interHp, gainStageHp, cathodeLp;
    // 2-band shelving EQ (Bass low-shelf + Treble high-shelf), NO middle.
    Biquad bassShelf, trebleShelf, midThick, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    float sag = 0.0f;
    rbtube::TubeStage v1a, v1b, v2a, v2b;
    rbtube::Miller12AX7 v1aMiller, v1bMiller, v2aMiller, v2bMiller;
    rbtube::CouplingCapGridLeak coupleV1aToV1b;
    rbtube::CouplingCapGridLeak coupleV1bToV2a;
    rbtube::CouplingCapGridLeak coupleV2aToV2b;
    rbtube::CouplingCapGridLeak coupleToPi;
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;
    rbtube::PowerAmpEL34 power;

    static float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }

    void updateFilters()
    {
        const float g = smoothstep(gain);
        const float pushed = smoothstepRange(0.40f, 0.92f, gain);
        const float mPush = smoothstep(master);
        const float sus = (sustain >= 0.5f) ? 1.0f : 0.0f;   // EQ-bypass gain/sustain boost
        const float clA = (classa >= 0.5f) ? 1.0f : 0.0f;    // Class A -> softer supply/headroom

        inputHp.setHighPass(sampleRate, 46.0f + 36.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12000.0f - 1400.0f * pushed + 800.0f * treble, 0.64f);
        // the thick midrange-forward Orange upper-mid push (fixed — no Middle knob here)
        preBody.setPeaking(sampleRate, 700.0f, 0.80f, 1.4f);
        // stage1->stage2 coupling = the tight Orange low end going INTO the gain
        stage1Hp.setHighPass(sampleRate, 150.0f + 45.0f * pushed, 0.70f);
        // stage2->stage3 coupling
        interHp.setHighPass(sampleRate, 72.0f + 60.0f * pushed, 0.70f);
        // extra HOT gain stage (the AD50's "more gain than the AD30") coupling cap
        gainStageHp.setHighPass(sampleRate, 90.0f + 50.0f * pushed, 0.70f);
        cathodeLp.setLowPass(sampleRate, 8800.0f + 1400.0f * treble - 1500.0f * pushed, 0.64f);

        // 2-BAND shelving EQ — Bass low-shelf + Treble high-shelf (NO middle). The
        // SUSTAIN footswitch BYPASSES the EQ (flat, more gain/sustain), per the AD50.
        if (sus >= 0.5f) {
            bassShelf.setLowShelf(sampleRate, 120.0f, 0.72f, 0.0f);
            trebleShelf.setHighShelf(sampleRate, 2200.0f, 0.78f, 0.0f);
        } else {
            bassShelf.setLowShelf(sampleRate, 120.0f, 0.72f, eqDb(bass, 11.0f));
            trebleShelf.setHighShelf(sampleRate, 2200.0f, 0.78f, eqDb(treble, 10.0f));
        }
        // the thick Orange midrange honk baked into the voice (fixed center)
        midThick.setPeaking(sampleRate, 520.0f, 0.60f, 2.0f + 1.2f * pushed);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1300.0f * treble - 2000.0f * pushed, 0.64f);
        // PRESENCE = power-amp NFB high-shelf (controllable, unlike the OR50's fixed)
        presenceShelf.setHighShelf(sampleRate, 2700.0f, 0.78f, eqDb(presence, 6.0f) + 1.8f);

        // Orange PPC 4x12 (thick, midrange-forward, smooth top)
        speakerHp.setHighPass(sampleRate, 86.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 124.0f, 0.84f, 1.4f + 2.1f * bass);
        speakerLowMid.setPeaking(sampleRate, 460.0f, 0.72f, 1.8f);
        speakerBite.setPeaking(sampleRate, 2400.0f + 480.0f * treble, 0.78f, 2.0f + 1.8f * treble - 0.5f * pushed);
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, -1.8f + 1.6f * treble + 1.2f * presence - 2.8f * pushed);
        speakerLp.setLowPass(sampleRate, 12600.0f + 1500.0f * treble - 3000.0f * pushed, 0.66f);

        v1a.set(sampleRate, 1, 250.0f, 40.0f, 10.0f, 1500.0f);
        v1b.set(sampleRate, 1, 250.0f, 40.0f, 18.0f, 1500.0f);
        v2a.set(sampleRate, 1, 250.0f, 40.0f, 36.0f, 1800.0f);
        v2b.set(sampleRate, 1, 250.0f, 40.0f, 55.0f, 1500.0f);
        v1aMiller.set(sampleRate,  68000.0f, 55.0f, 8.0f);
        v1bMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        v2aMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        v2bMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        coupleV1aToV1b.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f,
                           0.12f, 0.46f, 1.25f);
        coupleV1bToV2a.set(sampleRate, 470000.0f, 22.0e-9f, 180000.0f,
                           0.13f, 0.52f, 1.45f);
        coupleV2aToV2b.set(sampleRate, 470000.0f, 22.0e-9f, 150000.0f,
                           0.13f, 0.50f, 1.35f);
        coupleToPi.set(sampleRate, 1000000.0f, 47.0e-9f, 47000.0f,
                       0.10f, 0.55f + 0.12f * clA, 1.7f + 0.5f * pushed);
        phaseInverter.setMarshall(sampleRate,
                                  0.74f + 1.28f * mPush + 0.46f * pushed + 0.24f * clA,
                                  0.82f + 0.10f * presence);
        supply.set(sampleRate,
                   clA > 0.5f ? 115.0f : 70.0f, clA > 0.5f ? 47.0f : 100.0f,
                   560.0f, 50.0f,
                   10000.0f, 22.0f,
                   0.13f + 0.09f * clA,
                   0.09f + 0.06f * clA,
                   0.045f + 0.020f * clA,
                   0.16f + 0.04f * clA);
        power.set(sampleRate,
                  0.86f + 1.70f * mPush + 0.82f * pushed + 0.42f * clA,
                  clA > 0.5f ? -31.5f : -35.0f,
                  0.10f + 0.09f * clA,
                  62.0f,
                  11800.0f + 900.0f * presence);
        power.out = 0.0108f;
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); preBody.reset(); stage1Hp.reset(); interHp.reset(); gainStageHp.reset(); cathodeLp.reset();
        bassShelf.reset(); trebleShelf.reset(); midThick.reset(); phaseLp.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset(); speakerFizz.reset(); speakerLp.reset();
        dcBlock.reset(); sag = 0.0f;
        v1a.reset(); v1b.reset(); v2a.reset(); v2b.reset();
        v1aMiller.reset(); v1bMiller.reset(); v2aMiller.reset(); v2bMiller.reset();
        coupleV1aToV1b.reset(); coupleV1bToV2a.reset(); coupleV2aToV2b.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kGain:     gain = v; break;
            case kBass:     bass = v; break;
            case kTreble:   treble = v; break;
            case kPresence: presence = v; break;
            case kMaster:   master = v; break;
            case kSustain:  sustain = v; break;
            case kClassA:   classa = v; break;
            case kCabSim:   cabSim = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kAd50Def[i]); }

    float process(float in)
    {
        const float g = smoothstep(gain);
        const float pushed = smoothstepRange(0.40f, 0.92f, gain);
        const float mPush = smoothstep(master);
        const float clA = (classa >= 0.5f) ? 1.0f : 0.0f;    // Class A -> earlier breakup, more compression
        const float sus = (sustain >= 0.5f) ? 1.0f : 0.0f;   // SUSTAIN boost -> extra drive

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = softClip(x * (1.05f + 0.08f * pushed)) * (0.96f - 0.04f * pushed);

        // FOUR cascaded ECC83 gain stages — the AD50 runs HOTTER than the OR-series
        // (one extra hot stage). The SUSTAIN footswitch adds extra preamp drive.
        // V1-A: first stage, full cathode bypass -> fat/full gain.
        float y = preBody.process(x);
        y = v1a.process(v1aMiller.process(y) *
                         (1.5f + 5.0f * gain + 1.8f * g + 0.9f * sus));
        y = stage1Hp.process(y);
        y = coupleV1aToV1b.process(y, 0.78f + 4.8f * gain + 1.3f * sus);
        // V1-B: the GAIN pot drives this stage; partial cathode bypass -> Orange honk.
        y = v1b.process(v1bMiller.process(y) *
                         (1.2f + 4.2f * gain + 2.4f * pushed + 1.0f * sus));
        y = interHp.process(y);
        y = coupleV1bToV2a.process(y, 0.75f + 4.2f * gain + 1.6f * pushed + 0.8f * sus);
        // V2-A: the EXTRA hot gain stage (more gain than the AD30 / OR series).
        y = v2a.process(v2aMiller.process(y) *
                         (1.1f + 3.6f * gain + 2.0f * pushed + 0.8f * sus));
        y = gainStageHp.process(y);
        y = coupleV2aToV2b.process(y, 0.70f + 3.2f * gain + 1.4f * pushed);
        // V2-B: final preamp gain into the EQ.
        y = v2b.process(v2bMiller.process(y) *
                         (0.95f + 2.6f * gain + 1.7f * pushed));
        y = cathodeLp.process(y);

        // 2-BAND shelving EQ (Bass + Treble, NO middle); SUSTAIN bypasses it.
        y = bassShelf.process(y);
        y = trebleShelf.process(y);
        y = midThick.process(y) * 1.55f;
        y = phaseLp.process(y);

        // MASTER into the power amp
        y *= 0.22f + 1.28f * master;

        // 2x EL34 (~50W Class AB / ~30W Class A). Class A -> less headroom, earlier
        // breakup, more sag/compression. A bit of sag and a tight low end.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0075f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.155f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float bLoad = env * (0.85f + 0.80f * mPush + 0.45f * clA);
        const rbtube::SupplyScales bplus = supply.process(bLoad, bLoad * 0.56f, env * 0.24f);
        y *= 0.92f + 0.08f * bplus.preamp;
        y = coupleToPi.process(y * (0.92f + 0.14f * clA), 1.0f + 0.18f * pushed);
        y = phaseInverter.process(y) * bplus.screen;
        y = power.process(y * bplus.power);
        y *= 1.0f / (1.0f + sag * (0.07f + 0.08f * clA));

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        const float ampOnly = y;
        float cab = speakerHp.process(ampOnly);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizz.process(cab);
        cab = speakerLp.process(cab);
        y = ampOnly + cabSim * (cab - ampOnly);

        // Loudness normalization: cleanMakeup keeps RS Gain (-> GAIN) ~flat; MASTER
        // gives a mild swing. ~-14 dBFS reference.
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.009f * std::fabs((presence - 0.5f) * 12.0f)
            + 0.030f * sus;
        const float cleanMakeup = 1.0f + 2.4f * std::exp(-gain / 0.21f);
        const float level = (0.45f + 0.07f * (1.0f - gain)) * cleanMakeup /
            ((1.0f + 0.22f * mPush + 0.14f * pushed) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class Ad50Plugin : public Plugin
{
    Ad50Core left;
    Ad50Core right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    Ad50Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kAd50Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "CitrusAD50"; }
    const char* getDescription() const override { return "Citrus AD50 British EL34 style tube head"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'd', '5', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAd50Names[index];
        parameter.symbol = kAd50Symbols[index];
        parameter.ranges.min = kAd50Min[index];
        parameter.ranges.max = kAd50Max[index];
        parameter.ranges.def = kAd50Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ad50Plugin)
};

Plugin* createPlugin() { return new Ad50Plugin(); }

END_NAMESPACE_DISTRHO
