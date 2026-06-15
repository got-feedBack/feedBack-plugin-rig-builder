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
 * the game: RS Gain -> DRIVE; Bass/Mid/Treble -> tone stack. See
 * rs_knob_to_vst_param.json (Channel pinned Hi-gain + Modern ON via _static).
 */
#include "DistrhoPlugin.hpp"
#include "SuperdriveParams.h"
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

// Budda passive tone stack (FMV topology, Budda values): Treble 500K/220pF,
// Bass 500K/22nF, Middle 50K/22nF, slope R 56K (off the Superdrive schematic).
class SuperToneStack
{
    float b0=1,b1=0,b2=0,b3=0,a1=0,a2=0,a3=0,x1=0,x2=0,x3=0,y1=0,y2=0,y3=0,sampleRate=48000.0f;
public:
    void reset(){ x1=x2=x3=y1=y2=y3=0.0f; }
    void setSampleRate(float sr){ sampleRate=sr>1000.0f?sr:48000.0f; }
    void update(float treble,float mid,float bass)
    {
        const float t=tonePot(treble),m=tonePot(mid),l=tonePot(bass);
        const float R1=500.0e3f, R2=500.0e3f, R3=50.0e3f, R4=56.0e3f;
        const float C1=220.0e-12f, C2=22.0e-9f, C3=22.0e-9f;
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

    // derived
    float chan = 1.0f, drv = 0.5f;

    Biquad inputHp, pickupLoad;
    Biquad rhythmBody, briteShelf;
    Biquad hiBody, interHp, cathodeLp, modernScoop, modernEdge;
    SuperToneStack toneStack;
    Biquad stackMakeupLow, stackMakeupBody, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    float sag = 0.0f;

    static float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }

    void updateFilters()
    {
        chan = smoothstep(channel);                         // 0 = Rhythm, 1 = Hi-gain
        drv  = clamp01(chan * drive + (1.0f - chan) * rhythm * 0.70f);
        const float g = smoothstep(drv);
        const float pushed = smoothstepRange(0.40f, 0.92f, drv);
        const float mPush = smoothstep(master);

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

        toneStack.update(treble, mid, bass);
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
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); rhythmBody.reset(); briteShelf.reset();
        hiBody.reset(); interHp.reset(); cathodeLp.reset(); modernScoop.reset(); modernEdge.reset();
        toneStack.reset(); stackMakeupLow.reset(); stackMakeupBody.reset(); phaseLp.reset(); presenceShelf.reset();
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
            case kMaster:  master = v; break;
            case kBass:    bass = v; break;
            case kMid:     mid = v; break;
            case kTreble:  treble = v; break;
            case kDrive:   drive = v; break;
            case kRhythm:  rhythm = v; break;
            case kChannel: channel = v; break;
            case kModern:  modern = v; break;
            case kBrite:   brite = v; break;
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

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = softClip(x * (1.05f + 0.10f * pushed)) * (0.96f - 0.04f * pushed);

        // RHYTHM channel (single lower-gain 12AX7 stage), scaled by its own volume.
        float rh = rhythmBody.process(x);
        rh = asymTube(rh, 0.85f + 2.4f * rhythm + 1.2f * g, 0.010f + 0.012f * rhythm);
        rh = briteShelf.process(rh);
        rh *= 0.85f + 1.25f * rhythm;

        // HI-GAIN channel: cascaded 12AX7 -> the singing Budda lead.
        float hi = hiBody.process(x);
        hi = asymTube(hi, 1.10f + 3.8f * drive + 2.6f * g, 0.014f + 0.016f * drive);
        hi = interHp.process(hi);
        hi = asymTube(hi, 0.95f + 4.4f * drive + 2.8f * pushed, -0.008f - 0.012f * drive);
        hi = cathodeLp.process(hi);
        hi = modernScoop.process(hi);
        hi = modernEdge.process(hi);

        // channel select (the MASTER pull)
        float y = chan * hi + (1.0f - chan) * rh;

        y = toneStack.process(y) * 1.70f;
        y = stackMakeupLow.process(y);
        y = stackMakeupBody.process(y);
        y = phaseLp.process(y);

        // MASTER into the power amp
        y *= 0.25f + 1.25f * master;

        // 2x KT66 push-pull (~45W) + rectifier sag.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0060f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.140f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.34f + 0.70f * mPush + 0.50f * pushed));
        const float powerDrive = (0.85f + 1.60f * mPush + 1.40f * pushed) * sagDrop;
        y = asymTube(y, powerDrive, 0.006f + 0.012f * (treble - bass));
        y = 0.85f * y + 0.15f * softClip(y * (1.50f + 1.20f * pushed));
        y *= 0.97f - 0.08f * sag;

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizz.process(y);
        y = speakerLp.process(y);

        // Loudness normalization: keep multitone RMS ~constant across DRIVE/RHYTHM
        // + MASTER so the shared kLvl stays calibrated (~-14 dBFS reference).
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f);
        // cleanMakeup carries the whole drive-vs-loudness compensation (the drive
        // adds clipping, not level — MASTER is the volume); the base is flat across
        // DRIVE so the sweep stays even, and MASTER keeps a mild ~4 dB swing.
        const float cleanMakeup = 1.0f + 7.5f * std::exp(-drv / 0.22f);
        const float level = 0.66f * cleanMakeup / ((1.0f + 0.45f * mPush) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class SuperdrivePlugin : public Plugin
{
    SuperdriveCore left;
    SuperdriveCore right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    SuperdrivePlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kSuperDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Superdrive45"; }
    const char* getDescription() const override { return "Budda Superdrive 45 Series II style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SuperdrivePlugin)
};

Plugin* createPlugin() { return new SuperdrivePlugin(); }

END_NAMESPACE_DISTRHO
