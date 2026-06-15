/*
 * LOVOLT DR504 - Hiwatt DR504 "Custom Hiwatt 50" for the game's Amp_HG500.
 * Parody brand "Lovolt" (Hiwatt = high watt -> Lovolt = low volt; same brand as
 * the Lovolt 100). The in-app face must never read "Hiwatt".
 *
 * Local reference (modelled component-by-component):
 *   amps/Hiwatt DR504 (HG500)/DR504_Complete.pdf
 *
 * Full DR504 panel, 1:1 (see Dr504Params.h): a high-headroom, clean-and-loud
 * EL34 amp (3x ECC83 + ECC81 PI + 2x EL34 ~50W). Two jumperable channels —
 * NORMAL and BRILLIANT (bright cap) — sum into a shared tone stack (Bass 500K,
 * Treble 250K, Middle 100K -> the strong Hiwatt mids) -> MASTER VOLUME -> the
 * EL34 power amp. PRESENCE taps the NFB. Unlike a Plexi it stays clean far
 * longer; breakup comes mostly from cranking the MASTER.
 *
 * the game: RS Gain -> BRILLIANT VOL (breakup driver); Treble/Bass/Mid -> tone
 * stack, Pres -> Presence. See rs_knob_to_vst_param.json (input pinned BOTH).
 */
#include "DistrhoPlugin.hpp"
#include "Dr504Params.h"
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

// Hiwatt tone stack (Marshall-style FMV with the Hiwatt values): Treble 250K,
// Bass 500K, Middle 100K (the strong Hiwatt mids), slope R 56K, C 250pF/22nF/22nF.
class HiwattToneStack
{
    float b0=1,b1=0,b2=0,b3=0,a1=0,a2=0,a3=0,x1=0,x2=0,x3=0,y1=0,y2=0,y3=0,sampleRate=48000.0f;
public:
    void reset(){ x1=x2=x3=y1=y2=y3=0.0f; }
    void setSampleRate(float sr){ sampleRate=sr>1000.0f?sr:48000.0f; }
    void update(float treble,float mid,float bass)
    {
        const float t=tonePot(treble),m=tonePot(mid),l=tonePot(bass);
        const float R1=250.0e3f, R2=500.0e3f, R3=100.0e3f, R4=56.0e3f;
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

class Dr504Core
{
    float sampleRate = 48000.0f;
    float normalVol = kDr504Def[kNormalVol];
    float brightVol = kDr504Def[kBrightVol];
    float bass   = kDr504Def[kBass];
    float treble = kDr504Def[kTreble];
    float mid    = kDr504Def[kMiddle];
    float pres   = kDr504Def[kPresence];
    float master = kDr504Def[kMaster];
    float input  = kDr504Def[kInput];

    // derived
    float brightG = 1.0f, normalG = 1.0f;
    float preDrive = 0.5f;

    Biquad inputHp, pickupLoad, brightCapShelf, brightBody, normalBody;
    Biquad interstageHp, cathodeLp;
    HiwattToneStack toneStack;
    Biquad stackMakeupLow, stackMakeupBody, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    float sag = 0.0f;

    static float eqDb(float v, float r) { return (clamp01(v) - 0.5f) * 2.0f * r; }

    void updateFilters()
    {
        // Input cable: Normal(<0.25) / Both(jumpered, 0.25-0.75) / Brilliant(>=0.75).
        normalG = (input < 0.75f) ? 1.0f : 0.0f;
        brightG = (input >= 0.25f) ? 1.0f : 0.0f;
        // Preamp drive (the volumes); the Hiwatt preamp stays clean -> a gentle proxy.
        preDrive = clamp01(brightG * brightVol + normalG * normalVol * 0.80f);
        const float g = smoothstep(preDrive);
        const float pushed = smoothstepRange(0.28f, 0.88f, preDrive);
        const float bright = clamp01(0.32f * treble + 0.20f * pres + 0.45f * (1.0f - brightVol));

        inputHp.setHighPass(sampleRate, 40.0f + 30.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 13500.0f - 1200.0f * pushed + 800.0f * treble, 0.64f);
        brightCapShelf.setHighShelf(sampleRate, 1500.0f + 1100.0f * treble, 0.70f, -0.8f + 5.2f * bright + 1.6f * pres);
        brightBody.setPeaking(sampleRate, 680.0f + 360.0f * mid, 0.80f, -0.4f + 2.2f * mid);
        normalBody.setPeaking(sampleRate, 200.0f + 60.0f * bass, 0.72f, 0.6f + 2.2f * bass - 0.8f * pushed);

        interstageHp.setHighPass(sampleRate, 52.0f + 50.0f * pushed, 0.70f);
        cathodeLp.setLowPass(sampleRate, 9500.0f + 1500.0f * treble - 1100.0f * pushed, 0.64f);
        toneStack.update(treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f + 30.0f * bass, 0.72f, eqDb(bass, 4.4f));
        // the famous Hiwatt strong mids (100K mid pot) — a gentle upper-mid push
        stackMakeupBody.setPeaking(sampleRate, 620.0f + 180.0f * mid, 0.62f, -0.6f + 4.6f * mid);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1500.0f * treble + 900.0f * pres - 2000.0f * pushed, 0.64f);
        presenceShelf.setHighShelf(sampleRate, 2600.0f + 900.0f * pres, 0.78f, -3.6f + 8.4f * pres + 0.9f * treble);

        // Hiwatt 4x12 (Fane) voicing: full lows, strong mids, smooth top.
        speakerHp.setHighPass(sampleRate, 78.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 120.0f, 0.84f, 1.0f + 2.4f * bass);
        speakerLowMid.setPeaking(sampleRate, 420.0f + 90.0f * mid, 0.74f, 1.0f + 2.2f * mid);
        speakerBite.setPeaking(sampleRate, 2400.0f + 500.0f * treble, 0.78f, 2.0f + 1.8f * treble + 1.0f * pres - 0.5f * pushed);
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, 9.5f + 2.0f * treble + 2.0f * pres - 4.5f * pushed);
        speakerLp.setLowPass(sampleRate, 16000.0f + 1900.0f * treble + 800.0f * pres - 3500.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); brightCapShelf.reset(); brightBody.reset(); normalBody.reset();
        interstageHp.reset(); cathodeLp.reset();
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
            case kNormalVol: normalVol = v; break;
            case kBrightVol: brightVol = v; break;
            case kBass:      bass = v; break;
            case kTreble:    treble = v; break;
            case kMiddle:    mid = v; break;
            case kPresence:  pres = v; break;
            case kMaster:    master = v; break;
            case kInput:     input = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kDr504Def[i]); }

    float process(float in)
    {
        const float g = smoothstep(preDrive);
        const float pushed = smoothstepRange(0.28f, 0.88f, preDrive);
        const float mPush = smoothstep(master);

        // Input pre-gain (2026-06): the VST chain's pre-amp input boost does NOT
        // reach VST amp stages (it only drives the NAM path), so this amp ran on
        // the raw, quiet guitar and stayed clean even at RS Gain 100. Feed the
        // preamp a guitar-level push here so the BRILLIANT VOL sweep actually
        // breaks up — gain ~44 already gets crunch, gain 100 is full distortion.
        float x = inputHp.process(in * 3.2f);
        x = pickupLoad.process(x);
        x = softClip(x * (1.03f + 0.06f * pushed)) * (0.97f - 0.03f * pushed);

        // BRILLIANT channel (bright cap) + NORMAL channel — hotter than a stock
        // Hiwatt so the in-game (quiet-input) tones reach their intended breakup.
        float bch = brightCapShelf.process(brightBody.process(x));
        bch = asymTube(bch, 2.05f + 4.4f * brightVol + 2.3f * g, 0.010f + 0.012f * brightVol);
        float nch = normalBody.process(x);
        nch = asymTube(nch, 1.30f + 3.4f * normalVol + 1.9f * g, 0.009f + 0.010f * normalVol);

        // jumpered mix — soften the channel-volume attenuation so low/mid Gain
        // still drives the recovery stage (was brightVol*bch = collapsed at 44).
        float y = brightG * (0.34f + 0.66f * brightVol) * bch
                + normalG * (0.30f + 0.62f * normalVol) * nch;
        const float cleanLeak = 0.40f * (1.0f - smoothstepRange(0.30f, 0.85f, preDrive));
        y = y * (1.0f - cleanLeak) + x * cleanLeak * (brightG * brightVol + normalG * normalVol * 0.5f);

        // recovery (ECC83) into the tone stack
        y = interstageHp.process(y);
        y = asymTube(y, 1.30f + 2.7f * preDrive + 1.8f * pushed, -0.006f - 0.008f * preDrive);
        y = cathodeLp.process(y);

        y = toneStack.process(y) * 1.75f;
        y = stackMakeupLow.process(y);
        y = stackMakeupBody.process(y);
        y = phaseLp.process(y);

        // MASTER VOLUME into the power amp
        y *= 0.20f + 1.30f * master;

        // 2x EL34 (~50W) — high headroom, stiff supply (little sag), late breakup.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0050f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.120f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.22f + 0.55f * mPush));
        const float powerDrive = (0.78f + 1.7f * mPush + 0.8f * pushed) * sagDrop;
        y = asymTube(y, powerDrive, 0.005f + 0.010f * (treble - bass) + 0.008f * pres);
        y = 0.90f * y + 0.10f * softClip(y * (1.4f + 1.0f * mPush));
        y *= 0.98f - 0.06f * sag;

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizz.process(y);
        y = speakerLp.process(y);

        // Loudness normalization: the channel volumes are the gain (no gain knob),
        // so cleanMakeup keeps the RS Gain (-> Brilliant Vol) sweep ~flat; MASTER
        // is the volume -> a mild swing in the denominator. (~-14 dBFS reference.)
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.010f * std::fabs((pres - 0.5f) * 16.0f);
        const float cleanMakeup = 1.0f + 5.5f * std::exp(-preDrive / 0.26f);
        const float level = (0.66f + 0.12f * (1.0f - preDrive)) * cleanMakeup /
            ((1.0f + 0.40f * mPush + 0.20f * pushed) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class Dr504Plugin : public Plugin
{
    Dr504Core left;
    Dr504Core right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    Dr504Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kDr504Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "LovoltDR504"; }
    const char* getDescription() const override { return "Hiwatt DR504 Custom 50 style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('L', 'v', '5', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDr504Names[index];
        parameter.symbol = kDr504Symbols[index];
        parameter.ranges.min = kDr504Min[index];
        parameter.ranges.max = kDr504Max[index];
        parameter.ranges.def = kDr504Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dr504Plugin)
};

Plugin* createPlugin() { return new Dr504Plugin(); }

END_NAMESPACE_DISTRHO
