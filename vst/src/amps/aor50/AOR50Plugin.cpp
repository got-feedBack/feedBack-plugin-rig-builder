/*
 * RANEY AOR50 - Laney AOR 50 "Pro Tube Lead" (A50 Series II) for the game's
 * Amp_GB100. Parody brand "Raney"; the in-app face must never read "Laney".
 *
 * Local reference (modelled component-by-component):
 *   amps/Laney AOR 50 (GB100)/Laney_aor50_series2.pdf
 *
 * Full AOR50 front panel, 1:1 (see AOR50Params.h): two footswitchable channels
 * off an ECC83 preamp -> EL34 power amp (silicon rectifier, fairly tight):
 *   CHANNEL ONE : Preamp + Master (Pull-Bright)        — British clean/rhythm
 *   AOR CHANNEL : Preamp (Pull-AOR-On) + Master (Bright) — cascaded lead/overdrive
 *   shared tone : Bass (Pull-Deep), Middle (Pull-Boost), Treble, Presence (NFB)
 *
 * the game: the Gain knob drives the channel morph (Channel One clean -> AOR
 * lead), matching the gain_variants clean/crunch/dist split. See
 * rs_knob_to_vst_param.json.
 */
#include "DistrhoPlugin.hpp"
#include "AOR50Params.h"
#include "../../_shared/tube_stage.hpp"   // real 12AX7 stages + EL34 PP + Yeh tone stack
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

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    return std::fmax(20.0f, std::fmin(hz, sr * 0.45f));
}

static inline float smoothstep(float v)
{
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float smoothstepRange(float edge0, float edge1, float x)
{
    return smoothstep((x - edge0) / (edge1 - edge0));
}

static inline float softClip(float x)
{
    return std::tanh(x);
}

class Biquad
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f)
            na0 = 1.0f;
        const float inv = 1.0f / na0;
        b0 = nb0 * inv;
        b1 = nb1 * inv;
        b2 = nb2 * inv;
        a1 = na1 * inv;
        a2 = na2 * inv;
    }

public:
    void reset() { z1 = z2 = 0.0f; }

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setHighPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setPeaking(float sr, float hz, float q, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a,
            1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }

    void setHighShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
        set(a * ((a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
            a * ((a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha,
            2.0f * ((a - 1.0f) - (a + 1.0f) * c),
            (a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha);
    }

    void setLowShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr;
        const float c = std::cos(w0);
        const float s = std::sin(w0);
        const float rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
        set(a * ((a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha),
            2.0f * a * ((a - 1.0f) - (a + 1.0f) * c),
            a * ((a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha,
            -2.0f * ((a - 1.0f) + (a + 1.0f) * c),
            (a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
};

class DcBlock
{
    float x1 = 0.0f;
    float y1 = 0.0f;

public:
    void reset() { x1 = y1 = 0.0f; }
    float process(float x)
    {
        const float y = x - x1 + 0.995f * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

// Real anti-parallel silicon diode-pair clipper (Shockley). The AOR lead channel
// clips on a 1N4148 pair around an op-amp (D3/D4 on the schematic) — that diode
// edge ON TOP of the ECC83 cascade is the "Advanced Overdrive Response". Newton-
// solve the node KCL (physical, not a tanh fit):
//     (vin - v)/R = 2*Is*sinh(v/(n*Vt))
struct DiodeClipper
{
    static constexpr float Is  = 2.52e-9f;            // 1N4148 saturation current
    static constexpr float nVt = 1.752f * 0.02585f;   // emission coeff * thermal voltage (~45 mV)
    float R = 2200.0f;
    float v = 0.0f;
    void reset() { v = 0.0f; }
    inline float process(float vin)
    {
        for (int i = 0; i < 8; ++i)
        {
            const float e  = v / nVt;
            const float sh = std::sinh(e), ch = std::cosh(e);
            const float f  = (v - vin) / R + 2.0f * Is * sh;
            const float fp = 1.0f / R + 2.0f * Is * ch / nVt;
            v -= f / fp;
            if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
        }
        return v;
    }
};

} // namespace

class AOR50Core
{
    float sampleRate = 48000.0f;
    float channel   = kAOR50Def[kChannel];
    float aorPreamp = kAOR50Def[kAorPreamp];
    float aorMaster = kAOR50Def[kAorMaster];
    float aorBright = kAOR50Def[kAorBright];
    float ch1Preamp = kAOR50Def[kCh1Preamp];
    float ch1Master = kAOR50Def[kCh1Master];
    float ch1Bright = kAOR50Def[kCh1Bright];
    float bass      = kAOR50Def[kBass];
    float mid       = kAOR50Def[kMiddle];
    float treble    = kAOR50Def[kTreble];
    float deep      = kAOR50Def[kDeep];
    float midBoost  = kAOR50Def[kMidBoost];
    float presence  = kAOR50Def[kPresence];
    float cabSim    = kAOR50Def[kCabSim];

    // derived
    float chS = 1.0f;        // 0 = Channel One .. 1 = AOR
    float m = 0.7f;          // effective drive/voicing morph
    float activeMaster = 0.5f;
    float activeBright = 0.0f;
    float crunchA = 0.0f, leadA = 0.0f, deepA = 0.0f, midB = 0.0f;

    Biquad inputHp, inputLp, brightShelf;
    Biquad ch1Body, aorTight, aorBite;
    Biquad interHp, interLp, toneDeep, toneMidBoost;   // Pull-Deep / Pull-Boost switches
    Biquad phaseHp, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizzNotch, speakerLp;
    DcBlock dcBlock;
    // ── real circuit (Koren tubes + Shockley diode + Yeh stack) ──
    rbtube::TubeStage    v1, vCh1, vCrunch, vAorA, vAorB, vCascade;  // 12AX7 stages
    rbtube::Miller12AX7  v1Miller, ch1Miller, crunchMiller, aorAMiller, aorBMiller, cascadeMiller;
    rbtube::CouplingCapGridLeak coupleToPi;                            // master -> LTP grid blocking
    rbtube::PhaseInverterLTP12AX7 phaseInverter;                       // ECC83 long-tail pair
    rbtube::MultiNodeBPlus supply;                                      // diode rectifier + B+ filter nodes
    rbtube::PowerAmpEL34 power;                                      // EL34 power (~50W)
    rbtube::ToneStackYeh tone;                                       // real JCM800-style TMB
    DiodeClipper         aorDiode;                                   // AOR lead diode overdrive
    float inScale = 1.3f, toneMk = 13.0f;
    float sag = 0.0f;
    float lastPowerLoad = 0.0f, lastScreenLoad = 0.0f, lastPreampLoad = 0.0f;

    void setupTubes()
    {
        v1.set(sampleRate, 1, 250.0f, 40.0f, 22.0f, 1500.0f);       // V1 input
        vCh1.set(sampleRate, 1, 250.0f, 40.0f, 30.0f, 1500.0f);     // Channel One recovery
        vCrunch.set(sampleRate, 1, 250.0f, 40.0f, 35.0f, 1500.0f);
        vAorA.set(sampleRate, 1, 250.0f, 40.0f, 45.0f, 1500.0f);    // AOR cascade 1
        vAorB.set(sampleRate, 1, 250.0f, 40.0f, 60.0f, 1500.0f);    // AOR cascade 2
        vCascade.set(sampleRate, 1, 250.0f, 40.0f, 55.0f, 1500.0f); // extra lead cascade
        v1Miller.set(sampleRate,       68000.0f, 55.0f, 8.0f);
        ch1Miller.set(sampleRate,     180000.0f, 52.0f, 8.0f);
        crunchMiller.set(sampleRate,  180000.0f, 52.0f, 8.0f);
        aorAMiller.set(sampleRate,    180000.0f, 55.0f, 8.0f);
        aorBMiller.set(sampleRate,    150000.0f, 55.0f, 8.0f);
        cascadeMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
    }

    void updateFilters()
    {
        chS = smoothstep(channel);
        // Channel One drive (clean -> edge) vs AOR drive (cascaded lead). The
        // Gain knob (-> channel) crosses from one to the other; per-channel
        // Preamp pots set how hot each side is (pinned in _static for songs).
        const float ch1Drive = 0.06f + 0.42f * ch1Preamp;   // ~0.06 .. 0.48
        const float aorDrive = 0.55f + 0.42f * aorPreamp;   // ~0.55 .. 0.97
        m = clamp01(ch1Drive * (1.0f - chS) + aorDrive * chS);
        activeMaster = ch1Master * (1.0f - chS) + aorMaster * chS;
        activeBright = ch1Bright * (1.0f - chS) + aorBright * chS;
        crunchA = smoothstepRange(0.24f, 0.60f, m);
        leadA   = smoothstepRange(0.52f, 0.95f, m);
        deepA   = smoothstep(deep);
        midB    = clamp01(midBoost);

        inputHp.setHighPass(sampleRate, 52.0f + 60.0f * leadA + 26.0f * (1.0f - bass), 0.70f);
        inputLp.setLowPass(sampleRate, 13800.0f - 2600.0f * leadA + 1000.0f * treble, 0.64f);
        // Pull-Bright = a treble-bleed lift on the active master; plus base
        // brightness from Treble/Presence.
        brightShelf.setHighShelf(sampleRate, 1500.0f + 1100.0f * treble, 0.70f,
                                 -1.6f + 4.2f * treble + 1.6f * presence + 4.6f * activeBright + 1.0f * crunchA);
        ch1Body.setPeaking(sampleRate, 360.0f + 150.0f * mid, 0.74f,
                           -0.4f + 2.4f * mid + 1.4f * bass);
        aorTight.setLowShelf(sampleRate, 150.0f + 30.0f * bass, 0.76f,
                             -3.4f * leadA + 3.0f * bass + 1.2f * deepA);
        // The AOR's signature aggressive upper-mid bite.
        aorBite.setPeaking(sampleRate, 2050.0f + 560.0f * treble, 0.82f,
                           0.6f + 3.2f * treble + 2.6f * leadA + 1.0f * presence);
        interHp.setHighPass(sampleRate, 68.0f + 80.0f * leadA + 30.0f * (1.0f - bass), 0.71f);
        interLp.setLowPass(sampleRate, 9400.0f + 1200.0f * treble - 1700.0f * leadA, 0.64f);

        // Laney AOR tone stack — CIRCUIT-REAL (Yeh, real R/C from the A50 Series II
        // schematic: Treble 220k/470pF, Bass 1M/22nF, Mid 22k/22nF, slope 33k = the
        // JCM800-style British stack). Pull-Deep / Pull-Boost = the extra switches.
        tone.setComponents(220e3, 1.0e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9);
        tone.update(sampleRate, treble, mid, bass);
        toneDeep.setLowShelf(sampleRate, 110.0f, 0.72f, 5.5f * deepA);                         // Pull-Deep
        toneMidBoost.setPeaking(sampleRate, 560.0f + 300.0f * mid, 0.80f, 6.0f * midB);        // Pull-Boost

        phaseHp.setHighPass(sampleRate, 74.0f + 30.0f * leadA, 0.72f);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1400.0f * treble + 700.0f * presence - 2000.0f * leadA, 0.65f);
        presenceShelf.setHighShelf(sampleRate, 2650.0f + 850.0f * presence, 0.78f,
                                   -4.0f + 8.4f * presence + 1.2f * treble);

        speakerHp.setHighPass(sampleRate, 78.0f + 8.0f * leadA, 0.72f);
        speakerThump.setPeaking(sampleRate, 124.0f, 0.86f, 0.8f + 2.2f * bass + 1.8f * deepA);
        speakerLowMid.setPeaking(sampleRate, 420.0f + 150.0f * mid, 0.76f, 0.6f + 2.4f * mid);
        speakerBite.setPeaking(sampleRate, 2750.0f + 600.0f * treble, 0.78f,
                               2.2f + 2.3f * treble + 1.8f * presence + 0.8f * leadA - 0.5f * leadA);
        // a real 4x12 ROLLS OFF the top (no +9 dB fizz shelf -> inflates crest); gentle cut + LP
        speakerFizzNotch.setHighShelf(sampleRate, 4700.0f, 0.70f, -3.0f + 2.0f * treble + 2.0f * presence - 2.0f * leadA);
        speakerLp.setLowPass(sampleRate, 11500.0f + 1800.0f * treble + 850.0f * presence - 3000.0f * leadA, 0.66f);

        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f, 0.14f, 0.50f, 1.10f);
        phaseInverter.setMarshall(sampleRate, 0.95f + 1.45f * m + 0.75f * leadA, 0.88f);
        supply.set(sampleRate,
                   20.0f, 100.0f,
                   1000.0f, 50.0f,
                   10000.0f, 22.0f,
                   0.07f + 0.03f * leadA,
                   0.055f + 0.025f * leadA,
                   0.035f + 0.020f * m,
                   0.14f);
        // EL34 power amp (silicon rectifier = tight). Low drive floor so the
        // clean Channel One stays clean; B+ nodes above provide the dynamic sag.
        power.set(sampleRate, 0.95f + 8.8f * m + 4.8f * leadA, -40.0f, 0.08f, 55.0f, 11200.0f);
        power.out = 0.011f;
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset(); brightShelf.reset();
        ch1Body.reset(); aorTight.reset(); aorBite.reset();
        interHp.reset(); interLp.reset(); toneDeep.reset(); toneMidBoost.reset();
        tone.reset(); phaseHp.reset(); phaseLp.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset();
        speakerBite.reset(); speakerFizzNotch.reset(); speakerLp.reset();
        dcBlock.reset();
        v1Miller.reset(); ch1Miller.reset(); crunchMiller.reset();
        aorAMiller.reset(); aorBMiller.reset(); cascadeMiller.reset();
        v1.reset(); vCh1.reset(); vCrunch.reset(); vAorA.reset(); vAorB.reset(); vCascade.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset();
        power.reset(); aorDiode.reset();
        sag = 0.0f;
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
        setupTubes();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kChannel:   channel = v; break;
            case kAorPreamp: aorPreamp = v; break;
            case kAorMaster: aorMaster = v; break;
            case kAorBright: aorBright = v; break;
            case kCh1Preamp: ch1Preamp = v; break;
            case kCh1Master: ch1Master = v; break;
            case kCh1Bright: ch1Bright = v; break;
            case kBass:      bass = v; break;
            case kMiddle:    mid = v; break;
            case kTreble:    treble = v; break;
            case kDeep:      deep = v; break;
            case kMidBoost:  midBoost = v; break;
            case kPresence:  presence = v; break;
            case kCabSim:    cabSim = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults()
    {
        for (int i = 0; i < kParamCount; ++i)
            setParam(i, kAOR50Def[i]);
    }

    float process(float in)
    {
        const float cleanW = 1.0f - smoothstepRange(0.20f, 0.48f, m);
        const float leadW = smoothstepRange(0.55f, 0.95f, m);
        float crunchW = 1.0f - cleanW - leadW;
        if (crunchW < 0.0f) crunchW = 0.0f;
        const float sum = cleanW + crunchW + leadW + 1.0e-6f;
        const float cleanMix = cleanW / sum, crunchMix = crunchW / sum, leadMix = leadW / sum;
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = brightShelf.process(x);
        x = v1.process(v1Miller.process(x) * inScale * bplus.preamp);  // V1 shared input (real ECC83 + Miller)

        // Channel One voicing: fuller low-mid, moderate gain (one real stage).
        float ch1 = ch1Body.process(x);
        ch1 = vCh1.process(ch1Miller.process(ch1) * (0.6f + 1.4f * m) * bplus.preamp);

        // AOR voicing: tightened lows + bite, cascaded ECC83 PLUS the 1N4148 diode
        // overdrive (the "Advanced Overdrive Response" = a diode clip riding on the
        // tube cascade — the real D3/D4 around the op-amp).
        float aor = aorTight.process(x);
        aor = aorBite.process(aor);
        aor = vAorA.process(aorAMiller.process(aor) * (1.5f + 5.0f * m) * bplus.preamp);
        aor = vAorB.process(aorBMiller.process(aor) * (1.1f + 3.0f * m) * bplus.preamp);
        aor = aorDiode.process(aor * (0.7f + 7.0f * leadA)) * 1.9f;   // AOR diode clip

        // Crunch is the in-between (Channel One pushed).
        float crunch = ch1Body.process(x);
        crunch = vCrunch.process(crunchMiller.process(crunch) * (0.9f + 3.0f * m) * bplus.preamp);

        float y = ch1 * cleanMix + crunch * crunchMix - aor * leadMix; // aor = 2 triode stages (non-inverting) vs ch1/crunch's 1 (inverting); negate to phase-align, else the crunch->lead crossfade cancels into a mid-sweep notch
        y = interHp.process(y);
        y = interLp.process(y);

        const float extraCascade = smoothstepRange(0.46f, 0.90f, m);
        const float cascaded = vCascade.process(cascadeMiller.process(y) *
                                                (1.0f + 3.0f * m + 2.0f * leadMix) * bplus.preamp);
        y = y * (1.0f - 0.54f * extraCascade) + cascaded * (0.54f * extraCascade);

        // real JCM800-style tone stack + insertion-loss makeup + Deep/Mid-Boost switches
        y = tone.process(y) * toneMk;
        y = toneDeep.process(y);
        y = toneMidBoost.process(y);
        y = phaseHp.process(y);
        y = phaseLp.process(y);

        y = coupleToPi.process(y, 1.0f + 0.16f * leadA);
        lastPreampLoad = 0.12f * std::fabs(y) + 0.05f * m;
        y = phaseInverter.process(y * bplus.preamp);
        lastPowerLoad = 0.82f * std::fabs(y) + 0.22f * leadA;
        lastScreenLoad = 0.50f * std::fabs(y) + 0.10f * m;

        // EL34 power amp — real pentode table + LTP/B+ dynamics (drive set in updateFilters).
        y = power.process(y * bplus.power * bplus.screen);

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        float cab = speakerHp.process(y);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizzNotch.process(cab);
        cab = speakerLp.process(cab);
        y += cabSim * (cab - y);

        // Loudness normalization across the Gain (channel) sweep: the clean
        // Channel One barely saturates, so cleanMakeup lifts it to keep the RS
        // sweep within a couple dB and the shared kLvl stage calibrated.
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.011f * std::fabs((presence - 0.5f) * 16.0f);
        const float cleanMakeup = 1.0f + 2.5f * std::exp(-m / 0.30f);
        const float level = (0.74f + 0.12f * (1.0f - m)) * cleanMakeup /
            ((1.0f + 0.30f * m + 0.60f * leadMix) * toneEnergy);

        // Master volume (selected channel). Centred at 0.5 = unity so RS songs
        // that leave it at the musical default keep the calibrated loudness.
        const float masterGain = 0.55f + 0.90f * activeMaster;

        // loudness flattening vs the Channel One->AOR morph (clean post-output makeup; ~0 dB at 0.5)
        float gcDb = -0.502f + 8.888f * channel - 12.373f * channel * channel;
        if (gcDb > 12.0f) gcDb = 12.0f; else if (gcDb < -12.0f) gcDb = -12.0f;
        return softClip(y * level * masterGain) * 0.97f * std::pow(10.0f, 0.05f * gcDb);
    }
};

class AOR50Plugin : public Plugin
{
    AOR50Core left;
    AOR50Core right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;          // 2x anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll()
    {
        for (int i = 0; i < kParamCount; ++i)
        {
            left.setParam(i, params[i]);
            right.setParam(i, params[i]);
        }
    }

public:
    AOR50Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAOR50Def[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "AOR50"; }
    const char* getDescription() const override { return "Laney AOR 50 Pro Tube Lead style amp (2 channels)"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('A', 'r', '5', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kAOR50Names[index];
        parameter.symbol = kAOR50Symbols[index];
        parameter.ranges.min = kAOR50Min[index];
        parameter.ranges.max = kAOR50Max[index];
        parameter.ranges.def = kAOR50Def[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
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
                ubL[k] = rbAmpLvl(0.445f * left.process(ubL[k]));
                ubR[k] = rbAmpLvl(0.445f * right.process(ubR[k]));
            }
            outL[i] = osL.downsample(ubL);
            outR[i] = osR.downsample(ubR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AOR50Plugin)
};

Plugin* createPlugin()
{
    return new AOR50Plugin();
}

END_NAMESPACE_DISTRHO
