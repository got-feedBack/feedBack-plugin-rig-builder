/*
 * MARSTEN JVM410 - Marshall JVM410H for the game's Amp_MarshallJVM410H. Parody
 * brand "Marsten" (Marshall -> Marsten; same family as the Marsten DSL100 /
 * JCM800 / Bluesbreaker copies). The in-app face must never read "Marshall".
 *
 * Local reference (modelled component-by-component):
 *   amps/Marshall JVM410/Marshall_jvm410_sch.pdf
 *     SHT1 PRE AMP (JVM410-60-02), SHT2 POWER AMP / DI (4x EL34),
 *     FRONT PANEL 1/2 (JVM410-61-02): four channel strips (Clean/Crunch/OD1/OD2),
 *     each with its own TMB pots, plus RESONANCE (VR305) + PRESENCE (VR326) NFB,
 *     MASTER 1/2 and the per-channel green/orange/red mode relays.
 *
 * The JVM410H is a 4-CHANNEL 100W EL34 head. The real amp stores all four
 * channels at once; the game plays one sound at a time, so this models the
 * SELECTED channel + mode (the documented simplification). kChannel selects
 * Clean(0..0.25)/Crunch(.25..0.5)/OD1(.5..0.75)/OD2(.75..1) with increasing
 * cascade gain; kMode (green 0 / orange 0.5 / red 1) adds preamp gain+saturation
 * within the channel. Shared Marshall TMB tone stack + Presence + Resonance +
 * Master + op-amp reverb (off at 0). 4x EL34 power amp with sag.
 *
 * the game: RS Gain -> GAIN (Channel pinned to OD1 + Mode orange via the song
 * mapping); Bass/Mid/Treble -> tone stack; Pres -> Presence. See
 * rs_knob_to_vst_param.json.
 */
#include "DistrhoPlugin.hpp"
#include "Jvm410Params.h"
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

// Marshall TMB tone stack (3rd-order bilinear), JVM410 values per the front-panel
// schematic: Treble 220K, Bass 1M, Mid 25K (modelled 22K), slope R 33K, with
// the 470pF treble cap + 22nF/22nF bass/mid caps (the classic Marshall stack
// shared across all four channel strips). Reused verbatim from the Marsten JCM800.
class MarshallToneStack
{
    double b0=1,b1=0,b2=0,b3=0,a1=0,a2=0,a3=0,x1=0,x2=0,x3=0,y1=0,y2=0,y3=0; float sampleRate=48000.0f;
public:
    void reset(){ x1=x2=x3=y1=y2=y3=0.0; }
    void setSampleRate(float sr){ sampleRate=sr>1000.0f?sr:48000.0f; }
    void update(float treble,float mid,float bass)
    {
        const double t=tonePot(treble),m=tonePot(mid),l=tonePot(bass);
        const double R1=220.0e3, R2=1.0e6, R3=22.0e3, R4=33.0e3;
        const double C1=470.0e-12, C2=22.0e-9, C3=22.0e-9;
        const double ab1=t*C1*R1 + m*C3*R3 + l*(C1*R2+C2*R2) + (C1*R3+C2*R3);
        const double ab2=t*(C1*C2*R1*R4+C1*C3*R1*R4) - m*m*(C1*C3*R3*R3+C2*C3*R3*R3)
                      + m*(C1*C3*R1*R3+C1*C3*R3*R3+C2*C3*R3*R3) + l*(C1*C2*R1*R2+C1*C2*R2*R4+C1*C3*R2*R4)
                      + l*m*(C1*C3*R2*R3+C2*C3*R2*R3) + (C1*C2*R1*R3+C1*C2*R3*R4+C1*C3*R3*R4);
        const double ab3=l*m*(C1*C2*C3*R1*R2*R3+C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4)
                      + m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4) + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                      + t*l*C1*C2*C3*R1*R2*R4;
        const double aa0=1.0;
        const double aa1=(C1*R1+C1*R3+C2*R3+C2*R4+C3*R4) + m*C3*R3 + l*(C1*R2+C2*R2);
        const double aa2=m*(C1*C3*R1*R3-C2*C3*R3*R4+C1*C3*R3*R3+C2*C3*R3*R3) - m*m*(C1*C3*R3*R3+C2*C3*R3*R3)
                      + l*m*(C1*C3*R2*R3+C2*C3*R2*R3) + l*(C1*C2*R2*R4+C1*C2*R1*R2+C1*C3*R2*R4+C2*C3*R2*R4)
                      + (C1*C2*R1*R4+C1*C3*R1*R4+C1*C2*R3*R4+C1*C2*R1*R3+C1*C3*R3*R4+C2*C3*R3*R4);
        const double aa3=l*m*(C1*C2*C3*R1*R2*R3+C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3+C1*C2*C3*R3*R3*R4)
                      + m*(C1*C2*C3*R3*R3*R4+C1*C2*C3*R1*R3*R3-C1*C2*C3*R1*R3*R4) + l*(C1*C2*C3*R1*R2*R4) + C1*C2*C3*R1*R3*R4;
        const double c=2.0*sampleRate, c2=c*c, c3=c2*c;
        const double nb0=-ab1*c-ab2*c2-ab3*c3, nb1=-ab1*c+ab2*c2+3.0*ab3*c3,
                     nb2= ab1*c+ab2*c2-3.0*ab3*c3, nb3= ab1*c-ab2*c2+ab3*c3;
        const double na0=-aa0-aa1*c-aa2*c2-aa3*c3, na1=-3.0*aa0-aa1*c+aa2*c2+3.0*aa3*c3,
                     na2=-3.0*aa0+aa1*c+aa2*c2-3.0*aa3*c3, na3=-aa0+aa1*c-aa2*c2+aa3*c3;
        if(std::fabs(na0)<1.0e-30){ b0=1.0; b1=b2=b3=a1=a2=a3=0.0; return; }
        const double i=1.0/na0; b0=nb0*i; b1=nb1*i; b2=nb2*i; b3=nb3*i; a1=na1*i; a2=na2*i; a3=na3*i;
    }
    float process(float xin){ const double x=xin; const double y=b0*x+b1*x1+b2*x2+b3*x3-a1*y1-a2*y2-a3*y3;
        x3=x2; x2=x1; x1=x; y3=y2; y2=y1; y1=y; return (float)y; }
};

class DcBlock
{
    float x1=0.0f,y1=0.0f;
public:
    void reset(){ x1=y1=0.0f; }
    float process(float x){ const float y=x-x1+0.995f*y1; x1=x; y1=y; return y; }
};

// --- compact digital reverb (3 allpass diffusers + 2 damped combs). The JVM's
//     reverb is an op-amp-driven digital tank (CD4052 + RC4558 on JVM410-60/61);
//     off when REVERB = 0 (RS pins it to 0). Same topology as the Marsten DSL100. ---
class DigiReverb
{
    float ap0[1024], ap1[1024], ap2[1024];
    float cb0[3600], cb1[3600];
    int p0 = 0, p1 = 0, p2 = 0, c0 = 0, c1 = 0;
    int n0 = 281, n1 = 401, n2 = 487, nc0 = 1801, nc1 = 2143;
    float damp0 = 0.0f, damp1 = 0.0f;
    Biquad inHp, inLp;
    static inline float apStep(float* buf, int& p, int n, float in, float g)
    {
        const float bo = buf[p];
        const float v = in + bo * g;
        buf[p] = v;
        if (++p >= n) p = 0;
        return bo - v * g;
    }
public:
    void setSampleRate(float sr)
    {
        const float s = (sr > 1000.0f ? sr : 48000.0f) / 48000.0f;
        n0 = (int)(281 * s); n1 = (int)(401 * s); n2 = (int)(487 * s);
        nc0 = (int)(1801 * s); nc1 = (int)(2143 * s);
        if (nc0 > 3599) nc0 = 3599; if (nc1 > 3599) nc1 = 3599;
        inHp.setHighPass(sr, 180.0f, 0.7f);
        inLp.setLowPass(sr, 5200.0f, 0.7f);
        clear();
    }
    void clear()
    {
        for (int i = 0; i < 1024; ++i) ap0[i] = ap1[i] = ap2[i] = 0.0f;
        for (int i = 0; i < 3600; ++i) cb0[i] = cb1[i] = 0.0f;
        p0 = p1 = p2 = c0 = c1 = 0; damp0 = damp1 = 0.0f;
    }
    float process(float x)
    {
        x = inLp.process(inHp.process(x));
        x = apStep(ap0, p0, n0, x, 0.6f);
        x = apStep(ap1, p1, n1, x, 0.6f);
        x = apStep(ap2, p2, n2, x, 0.6f);
        const float o0 = cb0[c0]; damp0 += 0.38f * (o0 - damp0); cb0[c0] = x + damp0 * 0.74f; if (++c0 >= nc0) c0 = 0;
        const float o1 = cb1[c1]; damp1 += 0.38f * (o1 - damp1); cb1[c1] = x + damp1 * 0.72f; if (++c1 >= nc1) c1 = 0;
        return (o0 + o1) * 0.5f;
    }
};

} // namespace

class Jvm410Core
{
    float sampleRate = 48000.0f;
    float channel   = kJvm410Def[kChannel];
    float mode      = kJvm410Def[kMode];
    float gain      = kJvm410Def[kGain];
    float volume    = kJvm410Def[kVolume];
    float bass      = kJvm410Def[kBass];
    float mid       = kJvm410Def[kMiddle];
    float treble    = kJvm410Def[kTreble];
    float presence  = kJvm410Def[kPresence];
    float resonance = kJvm410Def[kResonance];
    float master    = kJvm410Def[kMaster];
    float reverb    = kJvm410Def[kReverb];

    // derived (recomputed in updateFilters)
    float drv = 0.6f;     // total preamp drive 0..1 (channel cascade + mode + gain)
    float chS = 0.66f;    // smoothed channel position (0 Clean .. 1 OD2)
    float modeA = 0.5f;   // smoothed mode (0 green .. 1 red)
    float cleanW=0, crunchW=0, od1W=0, od2W=0;  // channel mix weights
    float dirtW = 0.6f;   // 1 - cleanW: how "dirty"/cascaded the voice is
    float deep = 0.5f;    // resonance/deep amount

    Biquad inputHp, pickupLoad, brightCap;
    Biquad cleanBody, crunchBody, od1Tight, od1Bite, od2Tight, od2Bite;
    Biquad interHp, interLp, cathodeLp;
    MarshallToneStack toneStack;
    Biquad stackMakeupLow, phaseHp, phaseLp, presenceShelf, resonanceShelf, resonancePeak;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    DigiReverb rev;
    float sag = 0.0f;

    void updateFilters()
    {
        chS = clamp01(channel);
        modeA = clamp01(mode);

        // Channel mix weights (four overlapping crossfades across kChannel).
        cleanW  = 1.0f - smoothstepRange(0.18f, 0.34f, chS);
        const float crunchUp = smoothstepRange(0.18f, 0.34f, chS);
        const float crunchDn = smoothstepRange(0.42f, 0.60f, chS);
        crunchW = crunchUp * (1.0f - crunchDn);
        const float od1Up = smoothstepRange(0.42f, 0.60f, chS);
        const float od1Dn = smoothstepRange(0.68f, 0.86f, chS);
        od1W = od1Up * (1.0f - od1Dn);
        od2W = smoothstepRange(0.68f, 0.86f, chS);
        const float sum = cleanW + crunchW + od1W + od2W + 1.0e-6f;
        cleanW/=sum; crunchW/=sum; od1W/=sum; od2W/=sum;
        dirtW = 1.0f - cleanW;

        // Base channel drive ramp (Clean lowest .. OD2 highest) + mode + gain.
        // mode green/orange/red adds cascade preamp gain & saturation per channel.
        const float chBase = 0.10f * cleanW + 0.40f * crunchW + 0.66f * od1W + 0.90f * od2W;
        const float modeGain = 0.18f * modeA;                       // green->red preamp lift
        drv = clamp01(chBase + modeGain * (0.4f + 0.6f * dirtW) + 0.30f * gain * (0.5f + 0.5f * dirtW));

        const float g = smoothstep(drv);
        const float pushed = smoothstepRange(0.40f, 0.92f, drv);
        deep = smoothstep(resonance);

        inputHp.setHighPass(sampleRate, 30.0f + 22.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12800.0f - 1600.0f * pushed + 700.0f * treble, 0.64f);
        // bright cap across the gain pot — more shimmer at low gain (clean/green)
        brightCap.setHighShelf(sampleRate, 1900.0f, 0.72f, (2.6f + 1.4f * cleanW) * (1.0f - g));

        // per-channel voicing bodies (selected by the mix weights)
        cleanBody.setPeaking(sampleRate, 430.0f + 130.0f * mid, 0.74f, -0.6f + 2.4f * mid + 1.2f * bass);
        crunchBody.setPeaking(sampleRate, 720.0f + 220.0f * mid, 0.80f, -1.4f + 4.4f * mid + 1.6f * crunchW);
        od1Tight.setHighPass(sampleRate, 80.0f + 70.0f * od1W + 30.0f * (1.0f - bass), 0.71f);
        od1Bite.setPeaking(sampleRate, 1750.0f + 560.0f * treble, 0.82f, 0.4f + 3.0f * treble + 1.8f * od1W);
        od2Tight.setLowShelf(sampleRate, 150.0f + 30.0f * bass, 0.76f, -3.6f * od2W + 3.0f * bass + 1.0f * deep);
        od2Bite.setPeaking(sampleRate, 2050.0f + 620.0f * treble, 0.84f, 0.6f + 3.4f * treble + 2.2f * od2W + 1.0f * modeA);

        interHp.setHighPass(sampleRate, 110.0f + 150.0f * pushed + 50.0f * dirtW, 0.70f);
        interLp.setLowPass(sampleRate, 9800.0f + 1200.0f * treble - 1800.0f * pushed, 0.64f);
        cathodeLp.setLowPass(sampleRate, 9400.0f + 1300.0f * treble - 1500.0f * pushed, 0.64f);

        toneStack.update(treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f, 0.72f, 1.0f - 1.0f * pushed);

        // power-amp NFB: Presence (HF) + Resonance (LF) + phase inverter rolloff
        phaseHp.setHighPass(sampleRate, 70.0f + 28.0f * dirtW, 0.72f);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1400.0f * treble + 600.0f * presence - 2000.0f * pushed, 0.64f);
        presenceShelf.setHighShelf(sampleRate, 2600.0f + 800.0f * presence, 0.78f, -1.2f + 8.0f * presence + 1.0f * treble);
        resonanceShelf.setLowShelf(sampleRate, 95.0f + 38.0f * resonance, 0.78f, -2.0f + 7.0f * deep + 1.6f * dirtW);
        resonancePeak.setPeaking(sampleRate, 116.0f + 28.0f * resonance, 0.92f, 0.4f + 4.4f * deep + 1.2f * bass);

        // Marshall 4x12 voicing
        speakerHp.setHighPass(sampleRate, 82.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 122.0f, 0.84f, 0.8f + 2.0f * bass + 1.6f * deep);
        speakerLowMid.setPeaking(sampleRate, 420.0f + 90.0f * mid, 0.74f, -1.2f + 1.6f * mid);   // the Marshall mid dip
        speakerBite.setPeaking(sampleRate, 2700.0f + 480.0f * treble, 0.78f, 2.3f + 2.0f * treble + 0.8f * presence - 0.5f * pushed);
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, 9.5f + 2.0f * treble + 2.0f * presence - 4.5f * pushed);
        speakerLp.setLowPass(sampleRate, 14500.0f + 1800.0f * treble - 3500.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); brightCap.reset();
        cleanBody.reset(); crunchBody.reset(); od1Tight.reset(); od1Bite.reset(); od2Tight.reset(); od2Bite.reset();
        interHp.reset(); interLp.reset(); cathodeLp.reset();
        toneStack.reset(); stackMakeupLow.reset();
        phaseHp.reset(); phaseLp.reset(); presenceShelf.reset(); resonanceShelf.reset(); resonancePeak.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset(); speakerFizz.reset(); speakerLp.reset();
        dcBlock.reset(); rev.clear(); sag = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; toneStack.setSampleRate(sampleRate); rev.setSampleRate(sampleRate); reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kChannel:   channel = v; break;
            case kMode:      mode = v; break;
            case kGain:      gain = v; break;
            case kVolume:    volume = v; break;
            case kBass:      bass = v; break;
            case kMiddle:    mid = v; break;
            case kTreble:    treble = v; break;
            case kPresence:  presence = v; break;
            case kResonance: resonance = v; break;
            case kMaster:    master = v; break;
            case kReverb:    reverb = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kJvm410Def[i]); }

    float process(float in)
    {
        const float pushed = smoothstepRange(0.40f, 0.92f, drv);
        const float mPush = smoothstep(master);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = brightCap.process(x);
        // V1 input stage (mild), driven harder on the dirty channels/modes
        x = softClip(x * (1.05f + 0.20f * drv + 0.16f * dirtW));

        // --- four channel voices off the shared front-end, mixed by weight ---
        // CLEAN: nearly clean, one gentle stage.
        float clean = cleanBody.process(x);
        clean = 0.60f * clean + 0.40f * asymTube(clean, 0.84f + 0.9f * drv, 0.004f);

        // CRUNCH: a single hot stage (the classic Marshall crunch).
        float crunch = crunchBody.process(x);
        crunch = asymTube(crunch, 1.25f + 3.0f * drv + 1.4f * modeA, 0.012f + 0.010f * drv);
        crunch = 0.80f * crunch + 0.20f * softClip(crunch * (1.6f + 1.0f * drv));

        // OD1: two cascaded gain stages (the JVM "OD1" lead voice).
        float od1 = od1Tight.process(x);
        od1 = od1Bite.process(od1);
        od1 = asymTube(od1, 1.55f + 4.4f * drv + 1.8f * modeA, 0.014f + 0.010f * presence);
        od1 = asymTube(od1, 1.20f + 3.2f * drv, -0.012f - 0.008f * drv);
        od1 = 0.74f * od1 + 0.26f * softClip(od1 * (1.9f + 1.6f * drv));

        // OD2: hottest, three-stage cascade with extra red-mode saturation.
        float od2 = od2Tight.process(x);
        od2 = od2Bite.process(od2);
        od2 = asymTube(od2, 1.85f + 5.4f * drv + 2.4f * modeA, 0.018f + 0.012f * presence);
        od2 = asymTube(od2, 1.30f + 4.0f * drv, -0.014f - 0.010f * drv);
        od2 = 0.68f * od2 + 0.32f * softClip(od2 * (2.2f + 2.0f * drv));

        float y = clean * cleanW + crunch * crunchW + od1 * od1W + od2 * od2W;

        y = interHp.process(y);
        y = interLp.process(y);
        y = cathodeLp.process(y);

        // shared Marshall TMB tone stack
        y = toneStack.process(y) * 2.0f;
        y = stackMakeupLow.process(y);

        // VOLUME sets how hard the preamp drives the power amp.
        const float chDrive = 0.66f + 0.78f * volume;
        y *= chDrive;

        y = phaseHp.process(y);
        y = phaseLp.process(y);

        // 4x EL34 power amp (~100W) + sag, driven by the master.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0050f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.130f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.40f + 0.95f * mPush + 0.78f * pushed));
        const float powerDrive = (0.92f + 1.55f * mPush + 1.30f * pushed) * sagDrop;
        y = asymTube(y, powerDrive, 0.005f + 0.012f * (presence - bass) + 0.008f * resonance);
        y = 0.84f * y + 0.16f * softClip(y * (1.7f + 1.2f * pushed));
        y *= 0.98f - 0.07f * sag;

        y = presenceShelf.process(y);
        y = resonanceShelf.process(y);
        y = resonancePeak.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizz.process(y);
        y = speakerLp.process(y);

        // op-amp digital reverb (parallel send), off when REVERB = 0
        if (reverb > 0.0005f)
        {
            const float wet = rev.process(y);
            y += wet * reverb * 0.55f;
        }

        // Loudness normalization: keeps the multitone RMS ~constant across the
        // GAIN/channel sweep so the shared kLvl output stage stays calibrated.
        // The clean/low-gain region barely saturates, so cleanMakeup lifts it so
        // the whole RS Gain sweep stays within a couple dB (one kLvl fits all).
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.011f * std::fabs((presence - 0.5f) * 16.0f)
            + 0.010f * std::fabs((resonance - 0.5f) * 16.0f);
        const float cleanMakeup = 1.0f + 5.2f * std::exp(-drv / 0.22f);
        const float level = 0.60f * cleanMakeup / ((1.0f + 0.40f * mPush + 0.55f * pushed) * toneEnergy * chDrive);

        // MASTER volume. Centred at 0.5 = unity so RS songs that leave it at the
        // musical default keep the calibrated loudness.
        const float masterGain = 0.55f + 0.90f * master;

        return softClip(y * level * masterGain) * 0.97f;
    }
};

class Jvm410Plugin : public Plugin
{
    Jvm410Core left;
    Jvm410Core right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    Jvm410Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kJvm410Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarstenJVM410"; }
    const char* getDescription() const override { return "Marsten JVM410 style amp (4 channels)"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('J', 'v', '4', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kJvm410Names[index];
        parameter.symbol = kJvm410Symbols[index];
        parameter.ranges.min = kJvm410Min[index];
        parameter.ranges.max = kJvm410Max[index];
        parameter.ranges.def = kJvm410Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Jvm410Plugin)
};

Plugin* createPlugin() { return new Jvm410Plugin(); }

END_NAMESPACE_DISTRHO
