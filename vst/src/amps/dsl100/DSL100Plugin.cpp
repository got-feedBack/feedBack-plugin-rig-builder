/*
 * MARSTEN DSL100 - Marshall JCM2000 DSL100(H) for the game's Amp_MarshallDSL100H.
 * Parody brand "Marsten" (matches the GM-2 / UV-1 Marshall-copy pedals); the face
 * must never read "Marshall".
 *
 * Local references (full circuit, component-by-component):
 *   amps/Marshall DSL100/JCM2-60-02 (2003) iss9.pdf   (power amp + PSU, 4x EL34)
 *   amps/Marshall DSL100/JCM2-61-00 (2001) iss5.pdf   (control board, tone stack,
 *                                                       relay channel switch, reverb)
 *   amps/Marshall DSL100/JCM2-62/63/64                (preamp / channel boards)
 *   amps/Marshall DSL100/DSL50-100 manual (2004).pdf
 *
 * Full front panel modelled 1:1 (see DSL100Params.h): two channels (CLASSIC
 * Clean/Crunch + ULTRA OD1/OD2) off a shared 3x ECC83 preamp into an EL34 power
 * amp with Presence/Resonance NFB, dual master, per-channel reverb and a
 * Low/High (50W/100W) output switch.
 *
 * the game: the Gain knob drives the channel morph (Classic clean -> Crunch ->
 * Ultra), matching the gain_variants split. Cab Sim stays on until the host
 * supplies a cabinet/IR. See rs_knob_to_vst_param.json.
 */
#include "DistrhoPlugin.hpp"
#include "DSL100Params.h"
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
    void reset()
    {
        z1 = z2 = 0.0f;
    }

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
    void reset()
    {
        x1 = y1 = 0.0f;
    }

    float process(float x)
    {
        const float y = x - x1 + 0.995f * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

// --- compact digital reverb (3 allpass diffusers + 2 damped combs). The DSL's
//     reverb is an op-amp-driven digital tank (RC4558 on JCM2-61); voiced a
//     touch brighter/longer than a spring tank. ---
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

class DSL100Core
{
    float sampleRate = 48000.0f;
    // panel params
    float channel      = kDSL100Def[kChannel];
    float classicGain  = kDSL100Def[kClassicGain];
    float classicVol   = kDSL100Def[kClassicVol];
    float classicMode  = kDSL100Def[kClassicMode];
    float ultraGain    = kDSL100Def[kUltraGain];
    float ultraVol     = kDSL100Def[kUltraVol];
    float ultraMode    = kDSL100Def[kUltraMode];
    float bass         = kDSL100Def[kBass];
    float mid          = kDSL100Def[kMid];
    float treble       = kDSL100Def[kTreble];
    float toneShiftSw  = kDSL100Def[kToneShift];
    float resonance    = kDSL100Def[kResonance];
    float presence     = kDSL100Def[kPresence];
    float revClassic   = kDSL100Def[kRevClassic];
    float revUltra     = kDSL100Def[kRevUltra];
    float master1      = kDSL100Def[kMaster1];
    float master2      = kDSL100Def[kMaster2];
    float masterSelect = kDSL100Def[kMasterSelect];
    float output       = kDSL100Def[kOutput];
    float cabSim       = kDSL100Def[kCabSim];

    // derived (recomputed in updateFilters)
    float m = 0.7f;        // channel/drive morph: 0 = Classic clean .. 1 = Ultra max
    float chS = 1.0f;      // smoothed channel position (0 Classic .. 1 Ultra)
    float chVol = 0.5f;    // active channel volume
    float ts = 0.0f;       // effective Tone Shift depth
    float crunchA = 0.0f;  // crunch amount
    float ultraA = 0.0f;   // ultra amount
    float deep = 0.5f;     // resonance/deep

    Biquad inputHp, inputLp, brightShelf;
    Biquad cleanBody, crunchBody, ultraTight, ultraBite;
    Biquad interHp, interLp;
    Biquad toneBass, toneMid, toneTreble, toneShiftMid, toneShiftBite;
    Biquad phaseHp, phaseLp, presenceShelf, resonanceShelf, resonancePeak;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizzNotch, speakerLp;
    DcBlock dcBlock;
    DigiReverb reverb;
    // ── real circuit (Koren tube tables) replacing the tanh asymTube ──
    rbtube::TubeStage    v1, vClean, vCrunch, vUltraA, vUltraB, vCascade;  // 12AX7 stages
    rbtube::Miller12AX7  v1Miller, cleanMiller, crunchMiller, ultraAMiller, ultraBMiller, cascadeMiller;
    rbtube::CouplingCapGridLeak coupleToPi;                                // master -> LTP grid
    rbtube::PhaseInverterLTP12AX7 phaseInverter;                           // ECC83 long-tail pair
    rbtube::MultiNodeBPlus supply;                                          // diode rectifier + B+ nodes
    rbtube::PowerAmpEL34 power;                                            // 4x EL34 push-pull (~100W)
    rbtube::ToneStackYeh tone;                                            // real Marshall JCM2000 TMB
    float inScale = 1.2f, toneMk = 13.0f;
    float sag = 0.0f;
    float lastPowerLoad = 0.0f;
    float lastScreenLoad = 0.0f;
    float lastPreampLoad = 0.0f;

    void setupTubes()
    {
        // shared input + per-channel cascade: standard 12AX7 (250k grid-leak, 250V, /40).
        // fck climbs along the cascade (later stages tighter low end). Self-bias solved.
        v1.set(sampleRate, 1, 250.0f, 40.0f, 22.0f, 1500.0f);   // shared input stage
        vClean.set(sampleRate, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
        vCrunch.set(sampleRate, 1, 250.0f, 40.0f, 35.0f, 1500.0f);
        vUltraA.set(sampleRate, 1, 250.0f, 40.0f, 45.0f, 1500.0f);
        vUltraB.set(sampleRate, 1, 250.0f, 40.0f, 60.0f, 1500.0f);
        vCascade.set(sampleRate, 1, 250.0f, 40.0f, 55.0f, 1500.0f);
        v1Miller.set(sampleRate,       68000.0f, 55.0f, 8.0f);
        cleanMiller.set(sampleRate,   180000.0f, 52.0f, 8.0f);
        crunchMiller.set(sampleRate,  180000.0f, 52.0f, 8.0f);
        ultraAMiller.set(sampleRate,  150000.0f, 55.0f, 8.0f);
        ultraBMiller.set(sampleRate,  150000.0f, 55.0f, 8.0f);
        cascadeMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
    }

    void updateFilters()
    {
        chS = smoothstep(channel);
        // Channel/drive morph. Classic side sweeps clean->crunch via Classic
        // Gain (+ a touch from Crunch mode); Ultra side sweeps OD1->OD2 via
        // Ultra Gain (+ OD2 mode). the game drives `channel`, so a low Gain
        // lands in the clean Classic region and a high Gain in the hot Ultra.
        const float classicM = 0.03f + 0.46f * classicGain + 0.06f * classicMode;   // ~0.03..0.55
        const float ultraM   = 0.55f + 0.40f * ultraGain   + 0.05f * ultraMode;     // ~0.55..1.00
        m = clamp01(classicM * (1.0f - chS) + ultraM * chS);
        chVol = classicVol * (1.0f - chS) + ultraVol * chS;

        crunchA = smoothstepRange(0.26f, 0.62f, m);
        ultraA  = smoothstepRange(0.56f, 0.96f, m);
        deep = smoothstep(resonance);
        // Tone Shift: a real front-panel mid-scoop switch (deeper on the hotter
        // voices). A hair of low-mid is pulled even part-on for smoothness.
        ts = clamp01(toneShiftSw) * (0.70f + 0.30f * ultraA);

        inputHp.setHighPass(sampleRate, 54.0f + 58.0f * ultraA + 28.0f * (1.0f - bass), 0.70f);
        inputLp.setLowPass(sampleRate, 14800.0f - 3100.0f * ultraA + 1100.0f * treble, 0.64f);
        brightShelf.setHighShelf(sampleRate, 1050.0f + 1150.0f * treble, 0.70f,
                                 -0.6f + 5.4f * treble + 1.8f * presence + 1.0f * crunchA);
        cleanBody.setPeaking(sampleRate, 410.0f + 130.0f * mid, 0.76f,
                             -0.8f + 2.6f * mid + 1.4f * bass);
        crunchBody.setPeaking(sampleRate, 760.0f + 220.0f * mid, 0.82f,
                              -1.6f + 4.8f * mid + 1.9f * crunchA);
        ultraTight.setLowShelf(sampleRate, 145.0f + 30.0f * bass, 0.76f,
                               -3.8f * ultraA + 3.2f * bass + 1.2f * deep);
        ultraBite.setPeaking(sampleRate, 1850.0f + 620.0f * treble, 0.82f,
                             0.4f + 3.4f * treble + 2.2f * ultraA + 1.0f * presence);
        interHp.setHighPass(sampleRate, 70.0f + 86.0f * ultraA + 34.0f * (1.0f - bass), 0.71f);
        interLp.setLowPass(sampleRate, 11000.0f + 1200.0f * treble - 1300.0f * ultraA, 0.64f);

        // Marshall JCM2000 (DSL) tone stack — CIRCUIT-REAL (Yeh model, real R/C from
        // the JCM2000 stack): Treble 250k/500pF · Bass 1M/22nF · Mid 25k/22nF · slope 56k.
        tone.setComponents(250e3, 1.0e6, 25e3, 56e3, 500e-12, 22e-9, 22e-9);
        tone.update(sampleRate, treble, mid, bass);
        // Tone Shift = real front-panel mid-scoop switch (post-stack voicing).
        toneShiftMid.setPeaking(sampleRate, 650.0f + 180.0f * treble, 1.10f, -8.5f * ts);
        toneShiftBite.setPeaking(sampleRate, 2550.0f + 530.0f * treble, 0.82f,
                                 2.4f * ts + 0.6f * ultraA);
        // DSL100 is diode-rectified and fairly stiff. Low output intentionally
        // lets the supply move more; High/100W stays tighter and punchier.
        const float lowPow = 1.0f - clamp01(output);
        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 220000.0f,
                       0.14f, 0.55f, 1.20f);
        phaseInverter.setMarshall(sampleRate, 1.00f + 1.65f * m + 0.95f * ultraA, 0.90f);
        supply.set(sampleRate,
                   18.0f, 100.0f,
                   1000.0f, 50.0f,
                   10000.0f, 22.0f,
                   0.06f + 0.08f * lowPow + 0.02f * ultraA,
                   0.05f + 0.07f * lowPow + 0.02f * ultraA,
                   0.03f + 0.03f * ultraA,
                   0.14f);
        // 4x EL34 power amp drive (morph + Ultra). The DSL lead channel needs
        // a hotter PI/power feed than the earlier placeholder, or OD1 reads too polite.
        // Low floor so a clean/low-morph signal leaves the power tubes clean; it climbs
        // steeply with the morph + Ultra (the cranked lead voice slams the EL34s).
        power.set(sampleRate, 0.75f + 12.8f * m + 6.8f * ultraA, -40.0f,
                  0.10f + 0.22f * lowPow, 55.0f, 11200.0f);
        power.out = 0.011f;

        // Power-amp NFB: Presence (high) + Resonance (low) + speaker.
        phaseHp.setHighPass(sampleRate, 76.0f + 32.0f * ultraA, 0.72f);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1400.0f * treble + 700.0f * presence - 2000.0f * ultraA, 0.65f);
        presenceShelf.setHighShelf(sampleRate, 2700.0f + 850.0f * presence, 0.78f,
                                   -4.2f + 8.7f * presence + 1.3f * treble);
        resonanceShelf.setLowShelf(sampleRate, 95.0f + 38.0f * resonance, 0.78f,
                                   -2.2f + 7.4f * deep + 1.8f * ultraA);
        resonancePeak.setPeaking(sampleRate, 118.0f + 28.0f * resonance, 0.92f,
                                 0.4f + 4.8f * deep + 1.4f * bass);

        speakerHp.setHighPass(sampleRate, 76.0f + 10.0f * ultraA, 0.72f);
        speakerThump.setPeaking(sampleRate, 125.0f + 20.0f * resonance, 0.88f,
                                0.8f + 2.3f * bass + 2.2f * deep);
        speakerLowMid.setPeaking(sampleRate, 415.0f + 155.0f * mid, 0.76f,
                                 0.5f + 2.5f * mid - 2.2f * ts);
        speakerBite.setPeaking(sampleRate, 2850.0f + 620.0f * treble, 0.78f,
                               2.2f + 2.4f * treble + 1.9f * presence + 0.3f * ultraA);
        // a real 4x12 ROLLS OFF the top (no +9 dB fizz shelf — that inflates crest
        // without distorting); gentle HF cut + LP.
        speakerFizzNotch.setHighShelf(sampleRate, 4700.0f, 0.70f,
                                      -1.8f + 2.0f * treble + 2.0f * presence - 1.4f * ultraA);
        speakerLp.setLowPass(sampleRate, 13000.0f + 1800.0f * treble + 850.0f * presence - 2300.0f * ultraA, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset(); brightShelf.reset();
        cleanBody.reset(); crunchBody.reset(); ultraTight.reset(); ultraBite.reset();
        interHp.reset(); interLp.reset();
        toneBass.reset(); toneMid.reset(); toneTreble.reset(); toneShiftMid.reset(); toneShiftBite.reset();
        phaseHp.reset(); phaseLp.reset(); presenceShelf.reset(); resonanceShelf.reset(); resonancePeak.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset();
        speakerFizzNotch.reset(); speakerLp.reset(); dcBlock.reset();
        reverb.clear();
        v1Miller.reset(); cleanMiller.reset(); crunchMiller.reset();
        ultraAMiller.reset(); ultraBMiller.reset(); cascadeMiller.reset();
        v1.reset(); vClean.reset(); vCrunch.reset(); vUltraA.reset(); vUltraB.reset(); vCascade.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset(); tone.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
        sag = 0.0f;
        setupTubes();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reverb.setSampleRate(sampleRate);
        reset();
    }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kChannel:      channel = v; break;
            case kClassicGain:  classicGain = v; break;
            case kClassicVol:   classicVol = v; break;
            case kClassicMode:  classicMode = v; break;
            case kUltraGain:    ultraGain = v; break;
            case kUltraVol:     ultraVol = v; break;
            case kUltraMode:    ultraMode = v; break;
            case kBass:         bass = v; break;
            case kMid:          mid = v; break;
            case kTreble:       treble = v; break;
            case kToneShift:    toneShiftSw = v; break;
            case kResonance:    resonance = v; break;
            case kPresence:     presence = v; break;
            case kRevClassic:   revClassic = v; break;
            case kRevUltra:     revUltra = v; break;
            case kMaster1:      master1 = v; break;
            case kMaster2:      master2 = v; break;
            case kMasterSelect: masterSelect = v; break;
            case kOutput:       output = v; break;
            case kCabSim:       cabSim = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults()
    {
        for (int i = 0; i < kParamCount; ++i)
            setParam(i, kDSL100Def[i]);
    }

    float process(float in)
    {
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        const float cleanW = 1.0f - smoothstepRange(0.22f, 0.50f, m);
        const float ultraW = smoothstepRange(0.58f, 0.96f, m);
        float crunchW = 1.0f - cleanW - ultraW;
        if (crunchW < 0.0f)
            crunchW = 0.0f;
        const float sum = cleanW + crunchW + ultraW + 1.0e-6f;
        const float cleanMix = cleanW / sum;
        const float crunchMix = crunchW / sum;
        const float ultraMix = ultraW / sum;

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = brightShelf.process(x);
        x = v1.process(v1Miller.process(x) * inScale * bplus.preamp);   // V1 shared input stage (real 12AX7 + Miller)

        // Three voicings off the shared input, blended by the morph (the RS Gain
        // knob sweeps Classic clean -> Crunch -> Ultra). Each is a real 12AX7 cascade.
        float clean = cleanBody.process(x);
        clean = vClean.process(cleanMiller.process(clean) * (0.7f + 0.8f * m) * bplus.preamp);

        float crunch = crunchBody.process(x);
        crunch = vCrunch.process(crunchMiller.process(crunch) * (1.8f + 6.1f * m) * bplus.preamp);

        float ultra = ultraTight.process(x);
        ultra = ultraBite.process(ultra);
        ultra = vUltraA.process(ultraAMiller.process(ultra) * (3.1f + 9.6f * m) * bplus.preamp);
        ultra = vUltraB.process(ultraBMiller.process(ultra) * (1.9f + 6.2f * m) * bplus.preamp);

        float y = clean * cleanMix + crunch * crunchMix + ultra * ultraMix;
        y = interHp.process(y);
        y = interLp.process(y);

        // extra cascade stage for the hottest Ultra region
        const float extraCascade = smoothstepRange(0.48f, 0.90f, m);
        const float cascaded = vCascade.process(cascadeMiller.process(y) *
                                                (1.5f + 5.2f * m + 3.4f * ultraMix) * bplus.preamp);
        y = y * (1.0f - 0.55f * extraCascade) + cascaded * (0.55f * extraCascade);

        // real Marshall JCM2000 tone stack (Yeh) + insertion-loss makeup + Tone Shift
        y = tone.process(y) * toneMk;
        y = toneShiftMid.process(y);
        y = toneShiftBite.process(y);
        y = phaseHp.process(y);
        y = phaseLp.process(y);

        // Channel volume sets how hard the preamp drives the power amp.
        const float chDrive = 0.66f + 0.78f * chVol;
        y *= chDrive;
        y = coupleToPi.process(y, 1.0f + 0.18f * ultraA);
        lastPreampLoad = 0.12f * std::fabs(y) + 0.05f * m;
        y = phaseInverter.process(y * bplus.preamp);
        lastPowerLoad = 0.90f * std::fabs(y) + 0.24f * ultraA;
        lastScreenLoad = 0.55f * std::fabs(y) + 0.12f * m;

        // 4x EL34 power amp — REAL pentode table + OT. The diode supply/filter
        // response is injected via the B+ scales above. NFB is approximated by
        // Presence/Resonance shelves below.
        y = power.process(y * bplus.power * bplus.screen);

        y = presenceShelf.process(y);
        y = resonanceShelf.process(y);
        y = resonancePeak.process(y);
        y = dcBlock.process(y);

        float cab = speakerHp.process(y);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizzNotch.process(cab);
        cab = speakerLp.process(cab);
        y += cabSim * (cab - y);

        // Per-channel digital reverb (parallel). Active send crossfades with
        // the channel; modest depth so it stays a tail, not a wash.
        const float revSend = revClassic * (1.0f - chS) + revUltra * chS;
        if (revSend > 0.0005f)
        {
            const float wet = reverb.process(y);
            y += wet * revSend * 0.55f;
        }

        // Loudness normalization (keeps multitone RMS ~constant across the gain
        // range so the shared kLvl output stage stays calibrated) + channel
        // volume + Low/High output headroom trim.
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.011f * std::fabs((presence - 0.5f) * 16.0f)
            + 0.010f * std::fabs((resonance - 0.5f) * 16.0f);
        // No exponential cleanMakeup: it raises low-gain tones after the real
        // stages and can slam the safety clip while the tube path is still clean.
        const float level = (0.82f + 0.10f * (1.0f - m)) /
            ((1.0f + 0.32f * m + 0.64f * ultraMix) * toneEnergy * chDrive);

        // Master volume (selected 1/2). Centred at 0.5 = unity so RS songs that
        // leave it at the musical default keep the calibrated loudness.
        const float masterSel = masterSelect < 0.5f ? master1 : master2;
        const float masterGain = 0.55f + 0.90f * masterSel;

        // loudness flattening vs the Classic->Ultra morph (clean post-output makeup; ~0 dB at 0.5)
        float gcDb = -0.277f + 6.753f * channel - 10.261f * channel * channel;
        if (gcDb > 12.0f) gcDb = 12.0f; else if (gcDb < -12.0f) gcDb = -12.0f;
        return softClip(y * level * masterGain * std::pow(10.0f, 0.05f * gcDb)) * 0.97f;
    }
};

class DSL100Plugin : public Plugin
{
    DSL100Core left;
    DSL100Core right;
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
    DSL100Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDSL100Def[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "DSL100"; }
    const char* getDescription() const override { return "Marshall JCM2000 DSL100 style amp (2 channels)"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', '1', '0', '0'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDSL100Names[index];
        parameter.symbol = kDSL100Symbols[index];
        parameter.ranges.min = kDSL100Min[index];
        parameter.ranges.max = kDSL100Max[index];
        parameter.ranges.def = kDSL100Def[index];
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
                ubL[k] = rbAmpLvl(0.550f * left.process(ubL[k]));
                ubR[k] = rbAmpLvl(0.550f * right.process(ubR[k]));
            }
            outL[i] = osL.downsample(ubL);
            outR[i] = osR.downsample(ubR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSL100Plugin)
};

Plugin* createPlugin()
{
    return new DSL100Plugin();
}

END_NAMESPACE_DISTRHO
