/*
 * SILLA BOOGIE MARK III - Mesa/Boogie Mark III for the game's Amp_CA85
 * ("Mesa Boogie Mark III Crunch"). Parody brand "Silla"; the in-app face must
 * never read "Mesa" or "Boogie".
 *
 * Local reference (modelled component-by-component):
 *   amps/Mesa Mark III (CA_38)/boogie_mkiii.pdf  (Tri-Mode preamp + GEQ + power)
 *
 * Full Mark III head panel, 1:1 (see MarkIIIParams.h): VOLUME (pull BRIGHT),
 * TREBLE, BASS, MIDDLE, MASTER, LEAD DRIVE, LEAD MASTER + the signature 5-band
 * GRAPHIC EQ (80/240/750/2200/6600) with an EQ IN switch. Two voices off one
 * (scooped, 10K-mid) Fender-derived tone stack: RHYTHM (Volume->Master) and
 * LEAD (Lead Drive cascade -> Lead Master), picked by the LEAD switch. 6L6/EL34
 * Simul-Class power amp (~75W) with a fixed presence NFB.
 *
 * the game: RS Gain -> LEAD DRIVE; Bass/Mid/Treble -> tone stack. See
 * rs_knob_to_vst_param.json (Channel pinned LEAD + the GEQ "V" via _static).
 */
#include "DistrhoPlugin.hpp"
#include "MarkIIIParams.h"
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

// Mark III Fender-derived tone stack with the scooped 10K mid pot: Treble 250K,
// Bass 1M, Middle 10K, slope R 100K, C 250pF / 22nF / 22nF.
class MarkToneStack
{
    float b0=1,b1=0,b2=0,b3=0,a1=0,a2=0,a3=0,x1=0,x2=0,x3=0,y1=0,y2=0,y3=0,sampleRate=48000.0f;
public:
    void reset(){ x1=x2=x3=y1=y2=y3=0.0f; }
    void setSampleRate(float sr){ sampleRate=sr>1000.0f?sr:48000.0f; }
    void update(float treble,float mid,float bass)
    {
        const float t=tonePot(treble),m=tonePot(mid),l=tonePot(bass);
        const float R1=250.0e3f, R2=1.0e6f, R3=10.0e3f, R4=100.0e3f;
        const float C1=250.0e-12f, C2=22.0e-9f, C3=22.0e-9f;
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

class MarkIIICore
{
    float sampleRate = 48000.0f;
    float volume = kMarkIIIDef[kVolume];
    float treble = kMarkIIIDef[kTreble];
    float bass   = kMarkIIIDef[kBass];
    float mid    = kMarkIIIDef[kMiddle];
    float master = kMarkIIIDef[kMaster];
    float leadDrive = kMarkIIIDef[kLeadDrive];
    float leadMaster = kMarkIIIDef[kLeadMaster];
    float eq[5] = { kMarkIIIDef[kEq80], kMarkIIIDef[kEq240], kMarkIIIDef[kEq750], kMarkIIIDef[kEq2200], kMarkIIIDef[kEq6600] };
    float lead = kMarkIIIDef[kLead];
    float bright = kMarkIIIDef[kBright];
    float eqIn = kMarkIIIDef[kEqIn];

    // derived
    float chan = 1.0f, drv = 0.5f, outM = 0.5f;

    Biquad inputHp, pickupLoad, brightShelf;
    MarkToneStack toneStack;
    Biquad stackMakeupLow, interHp, cathodeLp;
    Biquad geq[5];
    Biquad phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    float sag = 0.0f;

    static float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }

    void updateFilters()
    {
        chan = smoothstep(lead);                                  // 0 = Rhythm, 1 = Lead
        drv  = clamp01(chan * leadDrive + (1.0f - chan) * volume * 0.60f);
        outM = chan * leadMaster + (1.0f - chan) * master;
        const float g = smoothstep(drv);
        const float pushed = smoothstepRange(0.40f, 0.92f, drv);
        const float mPush = smoothstep(outM);

        inputHp.setHighPass(sampleRate, 44.0f + 40.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12600.0f - 1500.0f * pushed + 800.0f * treble, 0.64f);
        // VOLUME pull-BRIGHT: a treble bypass cap across the volume pot.
        brightShelf.setHighShelf(sampleRate, 1700.0f, 0.72f, (bright >= 0.5f) ? 5.5f : 0.0f);

        toneStack.update(treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f + 30.0f * bass, 0.72f, eqDb(bass, 4.0f) - 1.0f * pushed);
        interHp.setHighPass(sampleRate, 150.0f + 150.0f * pushed, 0.70f);          // tighten between lead cascade stages
        cathodeLp.setLowPass(sampleRate, 9000.0f + 1400.0f * treble - 1500.0f * pushed, 0.64f);

        // 5-band graphic EQ (the Boogie GEQ). EQ IN out -> all bands flat.
        const float freqs[5] = { 80.0f, 240.0f, 750.0f, 2200.0f, 6600.0f };
        const float qs[5]    = { 0.9f, 1.0f, 1.1f, 1.0f, 0.8f };
        for (int i = 0; i < 5; ++i)
            geq[i].setPeaking(sampleRate, freqs[i], qs[i], (eqIn >= 0.5f) ? eqDb(eq[i], 12.0f) : 0.0f);

        phaseLp.setLowPass(sampleRate, 10500.0f + 1500.0f * treble - 2000.0f * pushed, 0.64f);
        presenceShelf.setHighShelf(sampleRate, 2700.0f, 0.78f, 2.4f + 1.0f * treble);

        // Mesa 1x12/4x12 voicing
        speakerHp.setHighPass(sampleRate, 80.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 116.0f, 0.84f, 0.7f + 2.2f * bass);
        speakerLowMid.setPeaking(sampleRate, 360.0f + 90.0f * mid, 0.78f, 0.5f + 1.6f * mid - 0.6f * pushed);
        speakerBite.setPeaking(sampleRate, 2600.0f + 480.0f * treble, 0.76f, 2.3f + 2.0f * treble - 0.5f * pushed);
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, 9.5f + 2.0f * treble - 4.5f * pushed);
        speakerLp.setLowPass(sampleRate, 14500.0f + 1900.0f * treble - 3500.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); brightShelf.reset();
        toneStack.reset(); stackMakeupLow.reset(); interHp.reset(); cathodeLp.reset();
        for (int i = 0; i < 5; ++i) geq[i].reset();
        phaseLp.reset(); presenceShelf.reset();
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
            case kVolume:     volume = v; break;
            case kTreble:     treble = v; break;
            case kBass:       bass = v; break;
            case kMiddle:     mid = v; break;
            case kMaster:     master = v; break;
            case kLeadDrive:  leadDrive = v; break;
            case kLeadMaster: leadMaster = v; break;
            case kEq80:       eq[0] = v; break;
            case kEq240:      eq[1] = v; break;
            case kEq750:      eq[2] = v; break;
            case kEq2200:     eq[3] = v; break;
            case kEq6600:     eq[4] = v; break;
            case kLead:       lead = v; break;
            case kBright:     bright = v; break;
            case kEqIn:       eqIn = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kMarkIIIDef[i]); }

    float process(float in)
    {
        const float g = smoothstep(drv);
        const float pushed = smoothstepRange(0.40f, 0.92f, drv);
        const float mPush = smoothstep(outM);

        // Input pre-gain (2026-06): the VST chain's pre-amp input boost does NOT
        // reach VST amp stages, so this amp ran on the raw, quiet guitar and the
        // LEAD channel stayed too clean even at RS Gain 90. Feed the preamp a
        // guitar-level push so the singing Boogie lead actually saturates.
        float x = inputHp.process(in * 3.2f);
        x = pickupLoad.process(x);
        x = brightShelf.process(x);
        // V1 first gain stage (mild) -> the scooped tone stack (pre-distortion EQ)
        x = asymTube(x, 1.05f + 0.6f * g, 0.008f);
        float t = toneStack.process(x) * 2.0f;
        t = stackMakeupLow.process(t);

        // RHYTHM voice: Volume sets the gain, one stage.
        float rh = asymTube(t * (0.6f + 2.2f * volume), 1.1f + 1.5f * volume, 0.010f);
        rh *= 0.72f + 1.20f * master;

        // LEAD voice: cascaded gain (V3A/V3B) -> the singing Boogie lead.
        // ~1.27x hotter cascade drive (2026-06): the Mark III lead channel was
        // too clean at high LEAD DRIVE; this pushes both gain stages further into
        // saturation for the singing/compressed Boogie lead. Loudness held flat
        // by cleanMakeup (verified ~-8.4 dBFS at gain 90, no level blast).
        float ld = asymTube(t, 1.40f + 5.4f * leadDrive + 2.9f * g, 0.012f + 0.014f * leadDrive);
        ld = interHp.process(ld);
        ld = asymTube(ld, 1.15f + 6.1f * leadDrive + 3.3f * pushed, -0.008f - 0.012f * leadDrive);
        ld = cathodeLp.process(ld);
        ld *= 0.45f + 1.0f * leadMaster;

        // channel select
        float y = chan * ld + (1.0f - chan) * rh;

        // graphic EQ (post-preamp, pre power)
        for (int i = 0; i < 5; ++i) y = geq[i].process(y);

        y = phaseLp.process(y);

        // 6L6/EL34 Simul-Class power amp (~75W) + sag, driven by the active master.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0060f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.140f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.30f + 0.65f * mPush + 0.45f * pushed));
        const float powerDrive = (0.85f + 1.55f * mPush + 1.30f * pushed) * sagDrop;
        y = asymTube(y, powerDrive, 0.006f + 0.012f * (treble - bass));
        y = 0.86f * y + 0.14f * softClip(y * (1.5f + 1.1f * pushed));
        y *= 0.97f - 0.08f * sag;

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizz.process(y);
        y = speakerLp.process(y);

        // Loudness normalization: DRIVE adds clipping (not level — the masters are
        // the volume), so cleanMakeup carries the drive compensation and the base
        // is flat across DRIVE; the active master keeps a mild swing (~-14 dBFS).
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f);
        const float cleanMakeup = 1.0f + 7.0f * std::exp(-drv / 0.22f);
        const float level = 0.62f * cleanMakeup / ((1.0f + 0.45f * mPush) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class MarkIIIPlugin : public Plugin
{
    MarkIIICore left;
    MarkIIICore right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    MarkIIIPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kMarkIIIDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarkIII"; }
    const char* getDescription() const override { return "Mesa Boogie Mark III style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('M', 'k', '3', 'c'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMarkIIINames[index];
        parameter.symbol = kMarkIIISymbols[index];
        parameter.ranges.min = kMarkIIIMin[index];
        parameter.ranges.max = kMarkIIIMax[index];
        parameter.ranges.def = kMarkIIIDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkIIIPlugin)
};

Plugin* createPlugin() { return new MarkIIIPlugin(); }

END_NAMESPACE_DISTRHO
