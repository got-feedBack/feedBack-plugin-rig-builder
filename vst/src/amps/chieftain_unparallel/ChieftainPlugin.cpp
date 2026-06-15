/*
 * UNPARALLEL CHIEFTAIN - Matchless Chieftain (Reverb), Mark Sampson, for
 * the game's Amp_BT15. Parody brand "RigBuilder"; the in-app face must never
 * read "Matchless" or "Chieftain".
 *
 * Reference (modelled component-by-component): a hand-traced 7-page schematic of
 * the Matchless Chieftain Reverb. A single-channel CLEAN/crunch boutique head:
 *   V1 12AX7 (68k/1M in, 1k5 cathode) -> Marshall-style TMB tone stack (BASS
 *   1MRA, MID 250kA, TREBLE 1MA, LARGE 5100pF treble cap => warmer/lower treble)
 *   -> V2 12AX7 -> VOLUME 250kA -> 12AX7 long-tail PI (MASTER 500kA + BRILLIANCE
 *   500kA presence shelf via a .0047 cap) -> 2x EL34 CATHODE-BIASED class AB
 *   (~40W, 270ohm/250uF bias, moderate sag, BIG headroom). Spring REVERB (12AX7
 *   driver/recovery + tank, 100kA level). GZ34 rect, OT WTI9356, 4/8/16 ohm.
 *
 * Voice: Fender-meets-Marshall, lots of clean before breakup. Stays cleaner and
 * has more headroom than the Mark/Boogie -- deliberately NOT over-saturated.
 *
 * the game: RS Gain -> VOLUME (drives the preamp into the EL34s); Bass/Mid/
 * Treble -> tone stack. Brilliance/Master/Reverb set on the face.
 */
#include "DistrhoPlugin.hpp"
#include "ChieftainParams.h"
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

// Matchless TMB tone stack — same FMV/Marshall topology, but with the Chieftain
// component values from the schematic: BASS 1M, MIDDLE 250K, TREBLE 1M, and a
// LARGE 5100pF (5.1nF) treble cap (vs a Marshall's 500pF) so the treble acts
// warmer and lower. Slope resistor ~100K (the .0022/560p/100k network). Placed
// after V1, before the Volume — the classic boutique clean/crunch voice.
// NOTE: this stack runs its recursion in DOUBLE precision. With the Chieftain's
// large 5.1nF treble cap into 1M pots, the bilinear poles sit extremely close to
// the unit circle (radius ~0.9999). The transfer function is stable in exact
// arithmetic (verified: bounded impulse response over the whole tone grid), but
// FLOAT roundoff in the difference equation accumulates and diverges to inf over
// a few thousand samples at the t=m=l extreme. Double state/coeffs removes that
// roundoff drift while preserving the exact schematic voicing.
class ChieftainToneStack
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, b3 = 0.0;
    double a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double x1 = 0.0, x2 = 0.0, x3 = 0.0, y1 = 0.0, y2 = 0.0, y3 = 0.0;
    double sampleRate = 48000.0;

public:
    void reset() { x1 = x2 = x3 = y1 = y2 = y3 = 0.0; }
    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? (double)sr : 48000.0; }

    void update(float trebleF, float midF, float bassF)
    {
        const double t = tonePot(trebleF);
        const double m = tonePot(midF);
        const double l = tonePot(bassF);

        const double R1 = 1.0e6;     // Treble pot (Chieftain: 1MA)
        const double R2 = 1.0e6;     // Bass pot   (Chieftain: 1MRA)
        const double R3 = 250.0e3;   // Middle pot (Chieftain: 250kA)
        const double R4 = 100.0e3;   // slope resistor (the .0022/560p/100k net)
        const double C1 = 5100.0e-12;// Treble cap (Chieftain: LARGE 5.1nF, warm)
        const double C2 = 22.0e-9;   // Bass cap
        const double C3 = 22.0e-9;   // Middle cap

        const double ab0 = 0.0;
        const double ab1 = t*C1*R1 + m*C3*R3 + l*(C1*R2 + C2*R2) + (C1*R3 + C2*R3);
        const double ab2 = t*(C1*C2*R1*R4 + C1*C3*R1*R4)
                        - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                        + m*(C1*C3*R1*R3 + C1*C3*R3*R3 + C2*C3*R3*R3)
                        + l*(C1*C2*R1*R2 + C1*C2*R2*R4 + C1*C3*R2*R4)
                        + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                        + (C1*C2*R1*R3 + C1*C2*R3*R4 + C1*C3*R3*R4);
        const double ab3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                        - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                        + t*l*C1*C2*C3*R1*R2*R4;
        const double aa0 = 1.0;
        const double aa1 = (C1*R1 + C1*R3 + C2*R3 + C2*R4 + C3*R4)
                        + m*C3*R3 + l*(C1*R2 + C2*R2);
        const double aa2 = m*(C1*C3*R1*R3 - C2*C3*R3*R4 + C1*C3*R3*R3 + C2*C3*R3*R3)
                        - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                        + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                        + l*(C1*C2*R2*R4 + C1*C2*R1*R2 + C1*C3*R2*R4 + C2*C3*R2*R4)
                        + (C1*C2*R1*R4 + C1*C3*R1*R4 + C1*C2*R3*R4
                           + C1*C2*R1*R3 + C1*C3*R3*R4 + C2*C3*R3*R4);
        const double aa3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                        - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + m*(C1*C2*C3*R3*R3*R4 + C1*C2*C3*R1*R3*R3
                             - C1*C2*C3*R1*R3*R4)
                        + l*(C1*C2*C3*R1*R2*R4) + C1*C2*C3*R1*R3*R4;

        const double c = 2.0 * sampleRate;
        const double c2 = c * c;
        const double c3 = c2 * c;
        const double nb0 = -ab0 - ab1*c - ab2*c2 - ab3*c3;
        const double nb1 = -3.0*ab0 - ab1*c + ab2*c2 + 3.0*ab3*c3;
        const double nb2 = -3.0*ab0 + ab1*c + ab2*c2 - 3.0*ab3*c3;
        const double nb3 = -ab0 + ab1*c - ab2*c2 + ab3*c3;
        const double na0 = -aa0 - aa1*c - aa2*c2 - aa3*c3;
        const double na1 = -3.0*aa0 - aa1*c + aa2*c2 + 3.0*aa3*c3;
        const double na2 = -3.0*aa0 + aa1*c + aa2*c2 - 3.0*aa3*c3;
        const double na3 = -aa0 + aa1*c - aa2*c2 + aa3*c3;

        if (std::fabs(na0) < 1.0e-30)
        {
            b0 = 1.0; b1 = b2 = b3 = a1 = a2 = a3 = 0.0;
            return;
        }
        const double invA0 = 1.0 / na0;
        b0 = nb0 * invA0; b1 = nb1 * invA0; b2 = nb2 * invA0; b3 = nb3 * invA0;
        a1 = na1 * invA0; a2 = na2 * invA0; a3 = na3 * invA0;
    }

    float process(float xf)
    {
        const double x = (double)xf;
        const double y = b0*x + b1*x1 + b2*x2 + b3*x3 - a1*y1 - a2*y2 - a3*y3;
        x3 = x2; x2 = x1; x1 = x;
        y3 = y2; y2 = y1; y1 = y;
        return (float)y;
    }
};

class DcBlock
{
    float x1=0.0f,y1=0.0f;
public:
    void reset(){ x1=y1=0.0f; }
    float process(float x){ const float y=x-x1+0.995f*y1; x1=x; y1=y; return y; }
};

// Compact Schroeder spring reverb (2 combs + 2 allpass). The Chieftain reverb is
// off for songs (RS pins it to 0); this gives the REVERB knob something musical
// when dialled by hand. 12AX7 driver/recovery + tank, parallel send.
class SpringReverb
{
    static const int N1=1481, N2=1709, A1=229, A2=97;
    float c1[N1], c2[N2], ap1[A1], ap2[A2];
    int i1=0,i2=0,j1=0,j2=0; float lp1=0.0f,lp2=0.0f;
public:
    void reset(){ for(int i=0;i<N1;++i)c1[i]=0.f; for(int i=0;i<N2;++i)c2[i]=0.f;
        for(int i=0;i<A1;++i)ap1[i]=0.f; for(int i=0;i<A2;++i)ap2[i]=0.f;
        i1=i2=j1=j2=0; lp1=lp2=0.f; }
    float process(float x){
        float y1=c1[i1]; lp1 += 0.42f*(y1-lp1); c1[i1]= x + 0.80f*lp1; if(++i1>=N1)i1=0;
        float y2=c2[i2]; lp2 += 0.42f*(y2-lp2); c2[i2]= x + 0.76f*lp2; if(++i2>=N2)i2=0;
        float y=(y1+y2)*0.5f;
        float t1=ap1[j1]; float o1=-0.6f*y+t1; ap1[j1]= y+0.6f*o1; if(++j1>=A1)j1=0; y=o1;
        float t2=ap2[j2]; float o2=-0.6f*y+t2; ap2[j2]= y+0.6f*o2; if(++j2>=A2)j2=0; y=o2;
        return y;
    }
};

} // namespace

class ChieftainCore
{
    float sampleRate = 48000.0f;
    float volume     = kChieftainDef[kVolume];
    float bass       = kChieftainDef[kBass];
    float mid        = kChieftainDef[kMiddle];
    float treble     = kChieftainDef[kTreble];
    float brilliance = kChieftainDef[kBrilliance];
    float master     = kChieftainDef[kMaster];
    float reverb     = kChieftainDef[kReverb];

    // derived
    float drv = 0.5f, outM = 0.7f;

    Biquad inputHp, pickupLoad;
    ChieftainToneStack toneStack;
    Biquad stackMakeupLow, interHp, cathodeLp;
    Biquad phaseLp, brillShelf, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    SpringReverb spring;
    float sag = 0.0f;

    void updateFilters()
    {
        drv  = clamp01(volume);
        outM = clamp01(master);
        const float g = smoothstep(drv);
        // The Chieftain has BIG headroom: it only pushes into real grind near the
        // top of Volume, so the "pushed" range is high & late.
        const float pushed = smoothstepRange(0.62f, 1.00f, drv);

        inputHp.setHighPass(sampleRate, 38.0f + 24.0f * g, 0.70f);
        pickupLoad.setLowPass(sampleRate, 13200.0f - 900.0f * pushed + 700.0f * treble, 0.64f);

        toneStack.update(treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 110.0f + 30.0f * bass, 0.72f,
            ((clamp01(bass) - 0.5f) * 7.0f) - 0.6f * pushed);
        interHp.setHighPass(sampleRate, 120.0f + 90.0f * pushed, 0.70f);
        cathodeLp.setLowPass(sampleRate, 10500.0f + 1400.0f * treble - 1100.0f * pushed, 0.64f);

        phaseLp.setLowPass(sampleRate, 10500.0f + 1500.0f * treble - 2000.0f * pushed, 0.64f);
        // BRILLIANCE 500kA: a presence/high-shelf on the PI via the .0047 cap.
        // Higher Brilliance = more top sparkle.
        brillShelf.setHighShelf(sampleRate, 2600.0f, 0.76f, 6.0f * brilliance);
        presenceShelf.setHighShelf(sampleRate, 3000.0f, 0.78f, 1.4f + 0.8f * treble);

        // EL34 head into a 2x12 / 4x12 — open, articulate boutique voicing.
        speakerHp.setHighPass(sampleRate, 78.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 120.0f, 0.84f, 0.6f + 2.0f * bass);
        speakerLowMid.setPeaking(sampleRate, 420.0f + 90.0f * mid, 0.78f, 0.4f + 1.5f * mid - 0.4f * pushed);
        speakerBite.setPeaking(sampleRate, 2700.0f + 480.0f * treble, 0.76f, 2.1f + 1.8f * treble - 0.5f * pushed);
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, 9.5f + 2.0f * treble - 4.5f * pushed);
        speakerLp.setLowPass(sampleRate, 16000.0f + 2000.0f * treble - 3500.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset();
        toneStack.reset(); stackMakeupLow.reset(); interHp.reset(); cathodeLp.reset();
        phaseLp.reset(); brillShelf.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset(); speakerFizz.reset(); speakerLp.reset();
        dcBlock.reset(); spring.reset(); sag = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; toneStack.setSampleRate(sampleRate); reset(); }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kVolume:     volume = v; break;
            case kBass:       bass = v; break;
            case kMiddle:     mid = v; break;
            case kTreble:     treble = v; break;
            case kBrilliance: brilliance = v; break;
            case kMaster:     master = v; break;
            case kReverb:     reverb = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kChieftainDef[i]); }

    float process(float in)
    {
        const float g = smoothstep(drv);
        const float pushed = smoothstepRange(0.62f, 1.00f, drv);
        const float mPush = smoothstep(outM);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        // V1 first gain stage (mild, clean) -> the TMB tone stack.
        x = asymTube(x, 1.04f + 0.45f * g, 0.006f);
        float t = toneStack.process(x) * 2.0f;
        t = stackMakeupLow.process(t);

        // V2 gain stage set by VOLUME. The Chieftain stays clean far up the dial;
        // modest gain, gentle bias — Fender-meets-Marshall headroom.
        float y = asymTube(t * (0.5f + 1.7f * volume), 1.05f + 1.35f * volume, 0.008f);
        y = interHp.process(y);
        y = cathodeLp.process(y);

        // BRILLIANCE presence shelf sits on the PI (post-preamp).
        y = brillShelf.process(y);

        // spring reverb (parallel send), off when REVERB = 0
        if (reverb > 0.001f) { const float wet = spring.process(y); y += (0.9f * reverb) * wet; }

        y = phaseLp.process(y);

        // 2x EL34 cathode-biased class AB (~40W). Moderate sag, lots of headroom:
        // the power stage barely clips until master + volume are both high.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0070f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.160f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.20f + 0.42f * mPush + 0.30f * pushed));
        const float powerDrive = (0.78f + 1.05f * mPush + 0.95f * pushed) * sagDrop;
        y = asymTube(y, powerDrive, 0.005f + 0.008f * (treble - bass));
        y = 0.90f * y + 0.10f * softClip(y * (1.3f + 0.8f * pushed));
        y *= 0.98f - 0.05f * sag;

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizz.process(y);
        y = speakerLp.process(y);

        // Loudness normalization: VOLUME adds clipping (not level), so cleanMakeup
        // carries the drive compensation and the base is flat-ish across VOLUME;
        // the master keeps a mild swing (~-14 dBFS). This amp is clean with big
        // headroom, so the makeup is gentler / spread wider than the Boogie.
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f);
        const float cleanMakeup = 1.0f + 4.2f * std::exp(-drv / 0.40f);
        const float level = 0.685f * cleanMakeup / ((1.0f + 0.40f * mPush) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class ChieftainPlugin : public Plugin
{
    ChieftainCore left;
    ChieftainCore right;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) { left.setParam(i, params[i]); right.setParam(i, params[i]); } }

public:
    ChieftainPlugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kChieftainDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "UnparallelChieftain"; }
    const char* getDescription() const override { return "Matchless Chieftain style boutique clean/crunch head"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('U', 'c', 'h', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kChieftainNames[index];
        parameter.symbol = kChieftainSymbols[index];
        parameter.ranges.min = kChieftainMin[index];
        parameter.ranges.max = kChieftainMax[index];
        parameter.ranges.def = kChieftainDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChieftainPlugin)
};

Plugin* createPlugin() { return new ChieftainPlugin(); }

END_NAMESPACE_DISTRHO
