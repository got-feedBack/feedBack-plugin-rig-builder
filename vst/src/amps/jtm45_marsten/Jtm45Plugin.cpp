/*
 * MARSTEN JTM45 - Marshall JTM45 (~30W, 2x 5881/6L6 + GZ34 tube rectifier) for
 * the game's Amp_MarshallJTM45. Parody brand "Marsten" (same as the Plexi /
 * DSL100); the in-app face must never read "Marshall".
 *
 * CIRCUIT-REAL DSP, schematic-first (JTM45.DGM issue 7,
 *   amps/Marshall JTM45/Marshall_jtm45_readable.pdf).
 * The nonlinear stages are the real Koren tube models (tube_stage.hpp), NOT
 * tanh stand-ins; the tone stack is the double-precision Yeh transfer function
 * (3rd-order FMV, float NaNs at 192k); the power amp is the real 5881/6L6-family
  * push-pull pentode model; and the whole nonlinear core runs at 4x oversample.
 * Only the input/speaker voicing biquads remain as EQ (pre-cab color).
 *
 * REAL STAGES MODELLED (from the schematic, component-by-component):
 *   - V1a  12AX7 (ECC83) HIGH-TREBLE/"bright" channel input: 100k plate, 820R
 *          cathode + 250uF bypass (fully bypassed), 1M grid-leak, 68k stoppers.
 *          The JTM45 swaps the Bassman's 12AY7 inputs for 12AX7 -> the higher-mu
 *          tube is THE defining Marshall change (more gain, earlier breakup).
 *   - V1b  12AX7 NORMAL channel input (same network, darker channel body).
 *   - the two LOUDNESS pots (1M log) mix the channels; the High-Treble channel
 *          carries a 500pF bright cap across its pot (vs the Plexi's 5000pF, so
 *          the JTM45 is darker/less sparkly).
 *   - V2a  12AX7 recovery/mixer (100k plate, 820R cathode) -> the grind stage.
 *   - V2b  12AX7 cathode follower driving the tone stack (Bassman/Marshall CF).
 *   - FMV TONE STACK (ToneStackYeh, double): Treble 250k (270pF) / Bass 1M (.02uF)
 *          / Middle 25k (.02uF) / 56k slope resistor. The early-JTM45/Bassman
 *          values (vs the Plexi's 33k slope / 500pF) = softer treble, warmer mid.
 *   - V3   12AX7 long-tailed-pair phase inverter (470R shared cathode + 1M tail).
 *   - 2x 5881/6L6 push-pull (~30W), fixed bias plus 470R screen resistors -> warmer,
 *          hotter idle and earlier compressing breakup than the later EL34 Plexi.
 *          PowerAmp5881 is generated from Tung-Sol 5881 data. GZ34 (5AR4) TUBE
 *          rectifier -> MUCH more supply sag than the Plexi. NFB tapped from the
 *          16-ohm OT winding (27k) with the 5k PRESENCE pot in the loop.
 *
 * the game: no gain knob, so RS Gain -> LOUDNESS 1 (clean->crunch->roar).
 * Treble/Bass/Mid -> tone stack, Pres -> Presence. kInput = Bright(0) /
 * Both-jumpered(0.5) / Normal(1).
 */
#include "DistrhoPlugin.hpp"
#include "Jtm45Params.h"
#include "../../_shared/tube_stage.hpp"   // real 12AX7 + 5881/6L6 circuit models
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts
// never hard-clip. See AMP_LOUDNESS.md.
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
    const float nyquist = sr * 0.45f;
    return std::fmax(20.0f, std::fmin(hz, nyquist));
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
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0;
        b1 = nb1 * invA0;
        b2 = nb2 * invA0;
        a1 = na1 * invA0;
        a2 = na2 * invA0;
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
    void reset() { x1 = y1 = 0.0f; }
    float process(float x)
    {
        const float y = x - x1 + 0.995f * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

} // namespace

class Jtm45Core
{
    float sampleRate = 48000.0f;
    float pres   = kJtm45Def[kPresence];
    float bass   = kJtm45Def[kBass];
    float mid    = kJtm45Def[kMiddle];
    float treble = kJtm45Def[kTreble];
    float loud1  = kJtm45Def[kLoudness1];
    float loud2  = kJtm45Def[kLoudness2];
    float input  = kJtm45Def[kInput];
    float cabSim = kJtm45Def[kCabSim];

    // derived: channel gating from the input cable + the loudness-as-gain proxy.
    float brightG = 1.0f, normalG = 1.0f;
    float effDrive = 0.5f;

    Biquad inputHp;
    Biquad pickupLoad;
    Biquad brightCapShelf;
    Biquad brightBody;
    Biquad normalBody;
    Biquad interstageHp;
    Biquad cathodeFollowerLp;
    rbtube::ToneStackYeh toneStack;        // FMV stack, REAL (double, no NaN at 192k)
    Biquad stackMakeupLow;
    Biquad stackMakeupBody;
    Biquad phaseLowPass;
    Biquad presenceShelf;
    Biquad speakerHp;
    Biquad speakerThump;
    Biquad speakerLowMid;
    Biquad speakerBite;
    Biquad speakerFizzNotch;
    Biquad speakerLp;
    DcBlock dcBlock;

    // ── REAL tube stages (Koren circuit models) — the JTM45 runs all 12AX7 (ECC83)
    //    in the preamp (vs the Bassman's 12AY7 inputs), with a 5881/6L6-family power pair.
    rbtube::TubeStage   brightTube, normalTube;   // V1a/V1b 12AX7 channel inputs
    rbtube::TubeStage   recoveryTube;             // V2a 12AX7 recovery/grind into the stack
    rbtube::Miller12AX7 brightMiller, normalMiller, recoveryMiller;
    rbtube::CouplingCapGridLeak brightCoupleToRecovery;
    rbtube::CouplingCapGridLeak normalCoupleToRecovery;
    rbtube::CouplingCapGridLeak coupleToPi;
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;
    rbtube::PowerAmp5881 power;                    // 2x 5881/6L6 push-pull (~30W), GZ34 sag
    float lastPowerLoad = 0.0f, lastScreenLoad = 0.0f, lastPreampLoad = 0.0f;

    static float eqDb(float normalized, float rangeDb)
    {
        return (clamp01(normalized) - 0.5f) * 2.0f * rangeDb;
    }

    void updateFilters()
    {
        // Input cable: Bright(<0.25) / Both(jumpered, 0.25-0.75) / Normal(>=0.75).
        brightG = (input < 0.75f) ? 1.0f : 0.0f;
        normalG = (input >= 0.25f) ? 1.0f : 0.0f;
        // The Loudness pots are the gain; jumpered (both channels) drives the amp
        // harder. The bright (High Treble) channel is the primary voice.
        effDrive = clamp01(brightG * loud1 + normalG * loud2 * 0.80f);

        const float g = smoothstep(effDrive);
        const float pushed = smoothstepRange(0.34f, 0.86f, effDrive);  // 5881 + GZ34 sag break up earlier than the Plexi
        // The 500pF bright cap bleeds treble across Loudness 1, most at low
        // settings; plus base sparkle from Treble/Presence. The JTM45 is a touch
        // darker than the Plexi (smaller bright cap, 5881/6L6 top), so less bright.
        const float bright = clamp01(0.32f * treble + 0.18f * pres + 0.48f * (1.0f - loud1));

        // ── real 12AX7 / 5881 circuit stages (cathode-biased, self-bias solved) ──
        // V1a/V1b: 820R cathode + 250uF bypass -> fully bypassed, fck=1/(2pi*820*250u)
        // ~= 0.78 Hz (effectively DC, full gain). V2a recovery: 820R, treat similarly.
        brightTube.set(sampleRate, 1, 250.0f, 40.0f, 1.0f, 820.0f);   // V1a 12AX7 High-Treble
        normalTube.set(sampleRate, 1, 250.0f, 40.0f, 1.0f, 820.0f);   // V1b 12AX7 Normal
        recoveryTube.set(sampleRate, 1, 250.0f, 40.0f, 30.0f, 820.0f);// V2a 12AX7 recovery/grind
        brightMiller.set(sampleRate, 68000.0f, 55.0f, 8.0f);
        normalMiller.set(sampleRate, 68000.0f, 55.0f, 8.0f);
        recoveryMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        // V1 coupling caps + Loudness pots/mixing resistors feeding V2a. This is
        // where a cranked JTM45 starts to "bloom": the caps recover after grid
        // current instead of acting like a static high-pass.
        brightCoupleToRecovery.set(sampleRate, 1000000.0f, 22.0e-9f, 270000.0f,
                                   0.30f, 0.06f, 0.18f);
        normalCoupleToRecovery.set(sampleRate, 1000000.0f, 22.0e-9f, 270000.0f,
                                   0.30f, 0.05f, 0.16f);
        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 100000.0f, 0.30f, 0.06f, 0.22f);
        phaseInverter.setMarshall(sampleRate, 0.95f + 1.50f * effDrive + 0.75f * pushed, 0.88f);
        supply.set(sampleRate,
                   115.0f, 32.0f,          // GZ34 + reservoir
                   8200.0f, 32.0f,         // screen/PI node
                   10000.0f, 16.0f,        // preamp node
                   0.22f + 0.09f * pushed,
                   0.16f + 0.06f * pushed,
                   0.08f + 0.03f * effDrive,
                   0.20f);
        // 2x 5881/6L6 push-pull (~30W), JTM45 fixed-bias power with hotter idle
        // (less-negative bias) + earlier breakup than the later EL34 Plexi.
        // GZ34 sag is modelled in the B+ nodes above; local power sag stays moderate.
        // NFB is approximated by the presence shelf.
        power.set(sampleRate, 6.2f + 8.0f * effDrive + 7.8f * pushed, -38.0f, 0.24f, 48.0f, 11200.0f);
        power.out = 0.0100f;

        inputHp.setHighPass(sampleRate, 42.0f + 52.0f * g + 28.0f * pushed, 0.70f);
        pickupLoad.setLowPass(sampleRate, 12200.0f - 1700.0f * pushed + 850.0f * treble, 0.64f);
        brightCapShelf.setHighShelf(sampleRate, 1500.0f + 1050.0f * treble, 0.70f,
                                    -1.2f + 5.4f * bright + 1.7f * pres);
        brightBody.setPeaking(sampleRate, 680.0f + 410.0f * mid, 0.80f,
                              -0.7f + 3.0f * mid + 1.0f * bright);
        normalBody.setPeaking(sampleRate, 175.0f + 55.0f * bass, 0.72f,
                              0.8f + 2.6f * bass - 1.0f * pushed);

        interstageHp.setHighPass(sampleRate, 58.0f + 76.0f * pushed + 38.0f * (1.0f - bass), 0.70f);
        cathodeFollowerLp.setLowPass(sampleRate, 8200.0f + 1500.0f * treble - 1500.0f * pushed, 0.64f);
        // FMV stack values straight off the JTM45 schematic: Treble 250k (270pF),
        // Bass 1M (.02uF=22nF), Middle 25k (.02uF), 56k slope resistor.
        toneStack.setComponents(250.0e3, 1.0e6, 25.0e3, 56.0e3, 270.0e-12, 22.0e-9, 22.0e-9);
        toneStack.update(sampleRate, treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f + 30.0f * bass, 0.72f,
                                   eqDb(bass, 4.8f) - 1.2f * pushed);
        stackMakeupBody.setPeaking(sampleRate, 500.0f + 180.0f * mid, 0.66f,
                                   -0.8f + 5.2f * mid + 1.5f * pushed);  // JTM45 warm mid
        phaseLowPass.setLowPass(sampleRate, 10500.0f + 1300.0f * treble + 1000.0f * pres
                                            - 2000.0f * pushed, 0.64f);
        presenceShelf.setHighShelf(sampleRate, 2500.0f + 850.0f * pres, 0.78f,
                                   -4.0f + 8.4f * pres + 0.9f * treble);

        // Marshall/Bluesbreaker-era cab (greenback/G12-ish but darker than the
        // 100W Plexi): tight HP, low thump, warm mid bite, gentle top air + earlier
        // top rolloff (5881/6L6 voice). The cab IR adds the real speaker; this is the
        // soft pre-cab color only.
        speakerHp.setHighPass(sampleRate, 80.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 112.0f, 0.84f, 0.9f + 2.4f * bass);
        speakerLowMid.setPeaking(sampleRate, 370.0f + 90.0f * mid, 0.78f,
                                 0.8f + 2.0f * mid - 0.7f * pushed);
        speakerBite.setPeaking(sampleRate, 2550.0f + 480.0f * treble, 0.74f,
                               2.5f + 2.0f * treble + 1.1f * pres - 0.5f * pushed);   // softer than Plexi
        speakerFizzNotch.setHighShelf(sampleRate, 4700.0f, 0.70f,
                                      -2.0f + 1.6f * treble + 1.2f * pres - 3.5f * pushed);
        speakerLp.setLowPass(sampleRate, 12500.0f + 1500.0f * treble + 700.0f * pres
                                         - 3000.0f * pushed, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); pickupLoad.reset(); brightCapShelf.reset();
        brightBody.reset(); normalBody.reset();
        interstageHp.reset(); cathodeFollowerLp.reset();
        toneStack.reset(); stackMakeupLow.reset(); stackMakeupBody.reset();
        phaseLowPass.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset();
        speakerBite.reset(); speakerFizzNotch.reset(); speakerLp.reset();
        dcBlock.reset();
        brightMiller.reset(); normalMiller.reset(); recoveryMiller.reset();
        brightCoupleToRecovery.reset(); normalCoupleToRecovery.reset(); coupleToPi.reset();
        brightTube.reset(); normalTube.reset(); recoveryTube.reset();
        phaseInverter.reset(); supply.reset(); power.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
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
            case kPresence:  pres = v; break;
            case kBass:      bass = v; break;
            case kMiddle:    mid = v; break;
            case kTreble:    treble = v; break;
            case kLoudness1: loud1 = std::fmax(0.26f, v); break;
            case kLoudness2: loud2 = std::fmax(0.24f, v); break;
            case kInput:     input = v; break;
            case kCabSim:    cabSim = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults()
    {
        for (int i = 0; i < kParamCount; ++i)
            setParam(i, kJtm45Def[i]);
    }

    float process(float in)
    {
        const float pushed = smoothstepRange(0.34f, 0.86f, effDrive);
        const rbtube::SupplyScales bplus = supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = softClip(x * (1.05f + 0.10f * pushed)) * (0.95f - 0.04f * pushed);

        // HIGH TREBLE (bright) channel: 500pF bright cap + body, REAL 12AX7.
        // Loudness sits after V1, so V1 grid drive is fixed.
        float bch = brightCapShelf.process(brightBody.process(x));
        bch = brightTube.process(brightMiller.process(bch) * 4.60f * bplus.preamp);
        bch = brightCoupleToRecovery.process(bch * loud1, 0.82f + 2.45f * loud1);
        // NORMAL channel: darker body, REAL 12AX7.
        float nch = normalBody.process(x);
        nch = normalTube.process(normalMiller.process(nch) * 4.00f * bplus.preamp);
        nch = normalCoupleToRecovery.process(nch * loud2, 0.72f + 2.05f * loud2);

        // Jumpered mix: channel outputs are already scaled by their Loudness pots
        // before the coupling/grid-leak state.
        float y = brightG * bch + normalG * 0.92f * nch;

        // V2a 12AX7 recovery / V2b cathode follower into the tone stack — REAL.
        y = interstageHp.process(y);
        y = recoveryTube.process(recoveryMiller.process(y) * (10.5f + 2.0f * effDrive) * bplus.preamp);
        y = cathodeFollowerLp.process(y);

        y = toneStack.process(y) * 2.25f;
        y = stackMakeupLow.process(y);
        y = stackMakeupBody.process(y);
        y = phaseLowPass.process(y);

        y = coupleToPi.process(y, 1.0f);
        lastPreampLoad = std::fabs(y) * (0.20f + 0.45f * effDrive);
        y = phaseInverter.process(y * bplus.screen);
        lastScreenLoad = std::fabs(y) * (0.35f + 0.65f * effDrive);

        // 2x 5881/6L6 push-pull (~30W) — real 5881 table + GZ34 B+ sag + presence
        // (NFB-approx). Warmer, softer and earlier-compressing than the 100W EL34 Plexi.
        y = power.process(y * bplus.power * bplus.screen);
        lastPowerLoad = std::fabs(y) * (0.55f + 0.95f * effDrive);
        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        float cab = speakerHp.process(y);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizzNotch.process(cab);
        cab = speakerLp.process(cab);
        y += cabSim * (cab - y);

        // Loudness normalization: the Loudness-as-gain means a low setting is much
        // quieter than a cranked one. NO cleanMakeup (it inverts the crest curve
        // with the real tubes). A gentle final softClip(y*level) = OT saturation.
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.010f * std::fabs((pres - 0.5f) * 16.0f);
        const float cleanPotLift = 1.0f + 3.2f * (1.0f - smoothstepRange(0.28f, 0.58f, loud1));
        const float level = cleanPotLift * (0.74f + 0.16f * (1.0f - effDrive)) /
            ((1.0f + 0.30f * effDrive + 0.16f * pushed) * toneEnergy);
        // Final OT clip: drive harder as the amp is cranked so a cranked JTM45
        // genuinely squashes its peaks (crest collapses) like the OT/rectifier do.
        // Renormalized by tanh(finalDrive) so the loudness makeup stays calibrated.
        const float finalDrive = 1.0f + 1.2f * pushed * pushed;
        return softClip(y * level * finalDrive) / std::tanh(finalDrive) * 0.97f;
    }
};

class Jtm45Plugin : public Plugin
{
    Jtm45Core left;
    Jtm45Core right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;            // anti-alias around the nonlinear chain
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
    Jtm45Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kJtm45Def[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarstenJTM45"; }
    const char* getDescription() const override { return "Marsten JTM45 style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('J', 't', '4', '5'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kJtm45Names[index];
        parameter.symbol = kJtm45Symbols[index];
        parameter.ranges.min = kJtm45Min[index];
        parameter.ranges.max = kJtm45Max[index];
        parameter.ranges.def = kJtm45Def[index];
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
        osL.reset(); osR.reset();
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
            float ub[kOS];
            osL.upsample(3.2f * inL[i], ub);
            for (int k = 0; k < kOS; ++k) ub[k] = rbAmpLvl(0.450f * left.process(ub[k]));
            outL[i] = osL.downsample(ub);
            osR.upsample(3.2f * inR[i], ub);
            for (int k = 0; k < kOS; ++k) ub[k] = rbAmpLvl(0.450f * right.process(ub[k]));
            outR[i] = osR.downsample(ub);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Jtm45Plugin)
};

Plugin* createPlugin()
{
    return new Jtm45Plugin();
}

END_NAMESPACE_DISTRHO
