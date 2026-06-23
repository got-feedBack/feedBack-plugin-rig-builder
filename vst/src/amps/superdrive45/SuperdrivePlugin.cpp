/*
 * GANDDI SUPERDRIVE 45 - Budda Superdrive 45 Series II for the game's Amp_BT45.
 * Parody brand "Ganddi"; the in-app face must never read "Budda".
 *
 * Local reference (modelled component-by-component):
 *   amps/Budda SuperDrive 45 (BTQ_45)/Superdrive45_manual.pdf
 *   amps/Budda SuperDrive 45 (BTQ_45)/BuddaSuperdrive80Schematic.jpg
 *
 * Two channels off one tone stack (3x 12AX7 + 2x KT66 ~45W + 5AR4 rectifier):
 *   - RHYTHM (clean -> edge), gain via the RHYTHM knob (+ pull BRITE treble boost)
 *   - HI-GAIN (the lead "Drive" voice), cascaded 12AX7 gain via DRIVE (+ pull
 *     MODERN: scoops mids, lifts bass+treble — hi-gain only)
 *   MASTER pull selects the channel (in = Rhythm / out = Hi-gain). Shared
 *   Bass/Mid/Treble (Treble 500K/220pF, Bass 500K/22nF, Mid 50K/22nF, 56K slope)
 *   -> long-tail PI -> 2x KT66 -> output transformer with a fixed presence NFB.
 *
 * Circuit-real port: every preamp/PI stage is rbtube::TubeStage (Koren 12AX7
 * plate transfer + physical cathode feedback), the passive Budda TMB is
 * rbtube::ToneStackYeh in double precision, and the KT66 pair uses a generated
 * rbtube::PowerAmpKT66 table from public Koren constants, validated against the
 * Marconi KT66 datasheet operating points. The schematic image is the SD80
 * family drawing, while the BT45 manual confirms the KT66 + 5AR4/solid-state
 * rectifier power section; this model follows the 45W manual.
 *
 * the game: RS Gain -> DRIVE; Bass/Mid/Treble -> tone stack. See
 * rs_knob_to_vst_param.json (Channel pinned Hi-gain + Modern ON via _static).
 */
#include "DistrhoPlugin.hpp"
#include "SuperdriveParams.h"
#include "../../_shared/tube_stage.hpp"
#include "../../_shared/oversampler.hpp"
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

class SuperdriveCore
{
    float sampleRate = 48000.0f;
    float master = kSuperDef[kMaster];
    float bass   = kSuperDef[kBass];
    float mid    = kSuperDef[kMid];
    float treble = kSuperDef[kTreble];
    float drive  = kSuperDef[kDrive];
    float rhythm = kSuperDef[kRhythm];
    float channel= kSuperDef[kChannel];
    float modern = kSuperDef[kModern];
    float brite  = kSuperDef[kBrite];
    float cabSim = kSuperDef[kCabSim];

    // derived
    float chan = 1.0f, drv = 0.5f, leadHot = 0.0f;

    Biquad inputHp, pickupLoad;
    Biquad rhythmBody, briteShelf;
    Biquad hiBody, interHp, cathodeLp, modernScoop, modernEdge;
    rbtube::ToneStackYeh toneStack;       // Budda FMV stack, double-precision Yeh model
    Biquad stackMakeupLow, stackMakeupBody, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    rbtube::TubeStage vInput, vRhythm, vLead1, vLead2, vLead3;
    rbtube::Miller12AX7 inputMiller, rhythmMiller, lead1Miller, lead2Miller, lead3Miller;
    rbtube::CouplingCapGridLeak coupleToPi;   // master -> long-tail-pair grid blocking
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;            // 5AR4/GZ34-style B+ nodes
    rbtube::PowerAmpKT66 power;           // 2x KT66 push-pull, Marconi/Genalex-family curve
    float lastPowerLoad = 0.0f, lastScreenLoad = 0.0f, lastPreampLoad = 0.0f;

    static float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }

    void setupCircuit()
    {
        // Schematic-derived cathode points. The TubeStage helper fixes Rp at
        // 100k, so higher-plate-load Budda stages are matched by drive/divider.
        vInput.set(sampleRate, 1, 250.0f, 40.0f, 1.3f, 2700.0f);   // 2k7/47u input
        vRhythm.set(sampleRate, 1, 250.0f, 40.0f, 28.5f, 820.0f);  // rhythm gain/recovery
        vLead1.set(sampleRate, 1, 250.0f, 40.0f, 49.8f, 4700.0f);  // .68u/4k7 lead stage
        vLead2.set(sampleRate, 1, 250.0f, 40.0f, 285.0f, 820.0f);  // .68u/820R tight stage
        vLead3.set(sampleRate, 1, 250.0f, 40.0f, 49.8f, 4700.0f);  // lead recovery/cascade
        inputMiller.set(sampleRate, 68000.0f, 55.0f, 8.0f);
        rhythmMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        lead1Miller.set(sampleRate, 220000.0f, 52.0f, 8.0f);
        lead2Miller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        lead3Miller.set(sampleRate, 150000.0f, 52.0f, 8.0f);
    }

    void updateFilters()
    {
        chan = smoothstep(channel);                         // 0 = Rhythm, 1 = Hi-gain
        drv  = clamp01(chan * drive + (1.0f - chan) * rhythm * 0.70f);
        const float g = smoothstep(drv);
        const float pushed = smoothstepRange(0.40f, 0.92f, drv);
        const float mPush = smoothstep(master);
        leadHot = smoothstepRange(0.22f, 0.96f, drive);
        setupCircuit();

        inputHp.setHighPass(sampleRate, 46.0f + 40.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12800.0f - 1500.0f * pushed + 800.0f * treble, 0.64f);

        // RHYTHM (clean) channel voicing + the pull-BRITE treble boost
        rhythmBody.setPeaking(sampleRate, 300.0f + 80.0f * mid, 0.70f, 0.6f + 1.2f * mid);
        briteShelf.setHighShelf(sampleRate, 3000.0f, 0.72f, (brite >= 0.5f) ? 6.0f : 0.0f);

        // HI-GAIN channel: cascade voicing + the pull-MODERN scoop/lift
        hiBody.setPeaking(sampleRate, 620.0f + 260.0f * mid, 0.80f, -0.6f + 2.0f * mid);
        interHp.setHighPass(sampleRate, 120.0f + 130.0f * pushed, 0.70f);   // tightening between cascade stages
        cathodeLp.setLowPass(sampleRate, 8800.0f + 1500.0f * treble - 1600.0f * pushed, 0.64f);
        // "Modern": scoop mids + lift treble (the manual's "more presence / aggressive")
        modernScoop.setPeaking(sampleRate, 680.0f, 0.90f, (modern >= 0.5f) ? -6.5f : 0.0f);
        modernEdge.setHighShelf(sampleRate, 2400.0f, 0.70f, (modern >= 0.5f) ? 4.0f : 0.0f);

        toneStack.setComponents(500.0e3, 500.0e3, 50.0e3, 56.0e3, 220.0e-12, 22.0e-9, 22.0e-9);
        toneStack.update(sampleRate, treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f + 30.0f * bass, 0.72f,
                                   eqDb(bass, 4.4f) + ((modern >= 0.5f) ? 2.4f : 0.0f) - 1.2f * pushed);
        stackMakeupBody.setPeaking(sampleRate, 560.0f + 180.0f * mid, 0.66f, -0.8f + 4.4f * mid + 1.2f * pushed);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1500.0f * treble - 2000.0f * pushed, 0.64f);
        // fixed NFB presence voicing (no presence knob on this amp)
        presenceShelf.setHighShelf(sampleRate, 2700.0f, 0.78f, 2.6f + 1.0f * treble);

        // Budda 1x12/2x12 voicing
        speakerHp.setHighPass(sampleRate, 80.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 118.0f, 0.84f, 0.8f + 2.2f * bass);
        speakerLowMid.setPeaking(sampleRate, 360.0f + 90.0f * mid, 0.78f, 0.7f + 1.8f * mid - 0.6f * pushed);
        speakerBite.setPeaking(sampleRate, 2600.0f + 480.0f * treble, 0.76f, 2.4f + 2.0f * treble - 0.5f * pushed);
        // Was a fizz NOTCH (top cut, made it dark). Now an AIR high-shelf: lifts the
        // top, retreats with gain (de-fizz on crank). Member name kept.
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, 9.5f + 2.0f * treble - 4.5f * pushed);
        // Speaker LP opened from ~6.0k (too dark) to ~16k (miked cab), eases on crank.
        speakerLp.setLowPass(sampleRate, 16000.0f + 1900.0f * treble - 3500.0f * pushed, 0.66f);

        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 100000.0f, 0.12f, 0.45f, 1.00f);
        phaseInverter.setMarshall(sampleRate, 0.88f + 1.30f * mPush + 0.65f * pushed + 0.35f * leadHot, 0.86f);
        // 5AR4/GZ34 rectifier feel: softer than diode Marshall, tighter than the AC30.
        supply.set(sampleRate,
                   115.0f, 32.0f,
                   1200.0f, 32.0f,
                   10000.0f, 16.0f,
                   0.14f + 0.05f * pushed,
                   0.10f + 0.04f * pushed,
                   0.055f + 0.020f * drv,
                   0.20f);
        // 2x KT66 push-pull, fixed-bias AB1. The generated KT66 table is centered
        // around the datasheet guitar-amp region: ~470V B+, ~6.6k a-a OT,
        // bias near -40V. B+ nodes above provide rectifier/supply dynamics.
        power.set(sampleRate, 1.55f + 4.25f * mPush + 3.70f * pushed + 1.55f * leadHot,
                  -40.0f, 0.12f + 0.05f * pushed, 54.0f, 11200.0f);
        power.out = 0.0098f;
        power.biasShift = 2.15f;
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); rhythmBody.reset(); briteShelf.reset();
        hiBody.reset(); interHp.reset(); cathodeLp.reset(); modernScoop.reset(); modernEdge.reset();
        toneStack.reset(); stackMakeupLow.reset(); stackMakeupBody.reset(); phaseLp.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset(); speakerFizz.reset(); speakerLp.reset();
        dcBlock.reset();
        inputMiller.reset(); rhythmMiller.reset(); lead1Miller.reset();
        lead2Miller.reset(); lead3Miller.reset();
        vInput.reset(); vRhythm.reset(); vLead1.reset(); vLead2.reset(); vLead3.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kMaster:  master = v; break;
            case kBass:    bass = v; break;
            case kMid:     mid = v; break;
            case kTreble:  treble = v; break;
            case kDrive:   drive = v; break;
            case kRhythm:  rhythm = v; break;
            case kChannel: channel = v; break;
            case kModern:  modern = v; break;
            case kBrite:   brite = v; break;
            case kCabSim:  cabSim = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kSuperDef[i]); }

    float process(float in)
    {
        const float g = smoothstep(drv);
        const float pushed = smoothstepRange(0.40f, 0.92f, drv);
        const float mPush = smoothstep(master);
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = vInput.process(inputMiller.process(x) * (0.72f + 0.35f * rhythm + 0.58f * drive) * bplus.preamp);

        // RHYTHM channel: lower-gain 12AX7 path, bright cap as pull-BRITE.
        float rh = rhythmBody.process(x);
        rh = vRhythm.process(rhythmMiller.process(rh) * (0.80f + 2.20f * rhythm + 0.85f * g) * bplus.preamp);
        rh = briteShelf.process(rh);
        rh *= 0.72f + 1.45f * rhythm;

        // HI-GAIN channel: cascaded 12AX7 stages. DRIVE sits before the cascade,
        // so late-stage drive also tracks the knob; low settings must clean up.
        float hi = hiBody.process(x);
        hi = vLead1.process(lead1Miller.process(hi) * (0.95f + 3.70f * drive + 1.40f * leadHot) * bplus.preamp);
        hi = interHp.process(hi);
        hi = vLead2.process(lead2Miller.process(hi) * (1.10f + 4.40f * drive + 2.60f * pushed) * bplus.preamp);
        hi = cathodeLp.process(hi);
        hi = vLead3.process(lead3Miller.process(hi) * (0.85f + 3.80f * drive + 3.20f * pushed) * bplus.preamp);
        hi = modernScoop.process(hi);
        hi = modernEdge.process(hi);
        hi *= 0.92f + 1.35f * drive;

        // channel select (the MASTER pull)
        float y = chan * hi + (1.0f - chan) * rh;

        y = toneStack.process(y) * 12.0f;     // Yeh insertion-loss makeup
        y = stackMakeupLow.process(y);
        y = stackMakeupBody.process(y);
        y = phaseLp.process(y);

        // Master volume feeds the LTP and then the KT66 power pair.
        y *= 0.28f + 1.42f * master;
        y = coupleToPi.process(y, 1.0f + 0.12f * pushed);
        lastPreampLoad = 0.11f * std::fabs(y) + 0.045f * drv;
        y = phaseInverter.process(y * bplus.preamp);
        lastPowerLoad = 0.70f * std::fabs(y) + 0.18f * pushed + 0.10f * mPush;
        lastScreenLoad = 0.45f * std::fabs(y) + 0.08f * drv;
        y = power.process(y * bplus.power * bplus.screen);

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        float cab = speakerHp.process(y);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizz.process(cab);
        cab = speakerLp.process(cab);
        y += cabSim * (cab - y);

        // Loudness normalization: post-distortion compensation only. The old
        // exponential cleanMakeup made low-gain settings hit the output clipper;
        // here gain adds distortion, not a volume jump.
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f);
        const float driveComp = chan > 0.5f
            ? (1.34f - 0.38f * smoothstep(drive) - 0.22f * pushed)
            : (1.12f - 0.18f * smoothstep(rhythm));
        const float level = 0.84f * driveComp / ((0.86f + 0.46f * mPush) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class SuperdrivePlugin : public Plugin
{
    SuperdriveCore left;
    SuperdriveCore right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    SuperdrivePlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kSuperDef[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Superdrive45"; }
    const char* getDescription() const override { return "Budda Superdrive 45 Series II style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
    int64_t getUniqueId() const override { return d_cconst('G', 'd', '4', '5'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kSuperNames[index];
        parameter.symbol = kSuperSymbols[index];
        parameter.ranges.min = kSuperMin[index];
        parameter.ranges.max = kSuperMax[index];
        parameter.ranges.def = kSuperDef[index];
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
        left.setSampleRate(kOS * (float)newSampleRate);
        right.setSampleRate(kOS * (float)newSampleRate);
        osL.reset();
        osR.reset();
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
            float ubL[kOS], ubR[kOS];
            osL.upsample(3.2f * inL[i], ubL);
            osR.upsample(3.2f * inR[i], ubR);
            for (int k = 0; k < kOS; ++k)
            {
                ubL[k] = rbAmpLvl(0.620f * left.process(ubL[k]));
                ubR[k] = rbAmpLvl(0.620f * right.process(ubR[k]));
            }
            outL[i] = osL.downsample(ubL);
            outR[i] = osR.downsample(ubR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SuperdrivePlugin)
};

Plugin* createPlugin() { return new SuperdrivePlugin(); }

END_NAMESPACE_DISTRHO
