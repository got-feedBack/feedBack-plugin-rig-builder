/*
 * MARSTEN JTM45 - Marshall JTM45 (~30W, 2x KT66 + GZ34 tube rectifier) for
 * the game's Amp_MarshallJTM45. Parody brand "Marsten" (same as the Plexi /
 * DSL100); the in-app face must never read "Marshall".
 *
 * Local reference (modelled component-by-component):
 *   amps/Marshall JTM45/Marshall_jtm45_readable.pdf  (JTM45.DGM issue 7)
 *
 * The JTM45 is the predecessor of the 1959 Plexi and the descendant of the
 * Fender Bassman 5F6-A: SAME jumper-input + dual Loudness (non-master) topology
 * and the Marshall/Bassman FMV tone stack. This model reuses the Plexi DSP
 * shape and changes the POWER amp + tone-stack values to the JTM45 schematic:
 *   - Tone stack: Treble 250K / Bass 1M / Middle 25K, 56K slope resistor,
 *     270pF treble cap, 22nF bass/mid caps (early JTM45 / Bassman values, vs
 *     the Plexi's 33K slope / 500pF treble cap).
 *   - Power amp: 2x KT66 push-pull (~30W) with a GZ34 (5AR4) TUBE rectifier ->
 *     warmer, softer, MUCH more sag, earlier breakup and a darker top than the
 *     100W 4x EL34 Plexi.
 *
 * Signal path: 4 inputs (2 per channel) -> V1 gain stages (HIGH TREBLE/"bright"
 * channel has a 500pF bright cap across its Loudness pot; NORMAL channel is
 * darker) -> the two Loudness pots mix (jumpered = both up) -> V2/V3 recovery +
 * cathode follower -> Marshall tone stack -> long-tail PI -> 2x KT66 (~30W) +
 * GZ34 sag -> output transformer. PRESENCE (5K) taps the power-amp NFB.
 *
 * the game: no gain knob, so RS Gain -> LOUDNESS 1 (clean->crunch->roar).
 * Treble/Bass/Mid -> tone stack, Pres -> Presence. kInput = Bright(0) /
 * Both-jumpered(0.5) / Normal(1).
 */
#include "DistrhoPlugin.hpp"
#include "Jtm45Params.h"
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

static inline float asymTube(float x, float drive, float bias)
{
    const float pushed = x * drive + bias;
    const float y = std::tanh(pushed);
    const float correction = std::tanh(bias);
    return (y - correction) / (1.0f - 0.32f * std::fabs(correction));
}

static inline float tonePot(float v)
{
    v = clamp01(v);
    if (v < 0.001f)
        return 0.001f;
    if (v > 0.999f)
        return 0.999f;
    return v;
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

// The JTM45 passive tone stack — the early Marshall/Bassman FMV topology with
// the JTM45 schematic values: Treble 250K, Bass 1M, Middle 25K, slope R 56K,
// C 270pF / 22nF / 22nF (off Marshall_jtm45_readable.pdf). Compared with the
// later Plexi (33K slope, 500pF treble cap) this gives a softer, warmer treble
// and a touch more midrange — the classic JTM45/Bassman voice.
class Jtm45ToneStack
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, x3 = 0.0f, y1 = 0.0f, y2 = 0.0f, y3 = 0.0f;
    float sampleRate = 48000.0f;

public:
    void reset() { x1 = x2 = x3 = y1 = y2 = y3 = 0.0f; }
    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; }

    void update(float treble, float mid, float bass)
    {
        const float t = tonePot(treble);
        const float m = tonePot(mid);
        const float l = tonePot(bass);

        const float R1 = 250.0e3f;   // Treble pot (250K)
        const float R2 = 1.0e6f;     // Bass pot (1M)
        const float R3 = 25.0e3f;    // Middle pot (25K)
        const float R4 = 56.0e3f;    // slope resistor (JTM45: 56K, vs Plexi 33K)
        const float C1 = 270.0e-12f; // Treble cap (JTM45: 270pF, vs Plexi 500pF)
        const float C2 = 22.0e-9f;   // Bass cap (.02uf)
        const float C3 = 22.0e-9f;   // Middle cap (.02uf)

        const float ab0 = 0.0f;
        const float ab1 = t*C1*R1 + m*C3*R3 + l*(C1*R2 + C2*R2) + (C1*R3 + C2*R3);
        const float ab2 = t*(C1*C2*R1*R4 + C1*C3*R1*R4)
                        - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                        + m*(C1*C3*R1*R3 + C1*C3*R3*R3 + C2*C3*R3*R3)
                        + l*(C1*C2*R1*R2 + C1*C2*R2*R4 + C1*C3*R2*R4)
                        + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                        + (C1*C2*R1*R3 + C1*C2*R3*R4 + C1*C3*R3*R4);
        const float ab3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                        - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                        + t*l*C1*C2*C3*R1*R2*R4;
        const float aa0 = 1.0f;
        const float aa1 = (C1*R1 + C1*R3 + C2*R3 + C2*R4 + C3*R4)
                        + m*C3*R3 + l*(C1*R2 + C2*R2);
        const float aa2 = m*(C1*C3*R1*R3 - C2*C3*R3*R4 + C1*C3*R3*R3 + C2*C3*R3*R3)
                        - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                        + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                        + l*(C1*C2*R2*R4 + C1*C2*R1*R2 + C1*C3*R2*R4 + C2*C3*R2*R4)
                        + (C1*C2*R1*R4 + C1*C3*R1*R4 + C1*C2*R3*R4
                           + C1*C2*R1*R3 + C1*C3*R3*R4 + C2*C3*R3*R4);
        const float aa3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                        - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + m*(C1*C2*C3*R3*R3*R4 + C1*C2*C3*R1*R3*R3
                             - C1*C2*C3*R1*R3*R4)
                        + l*(C1*C2*C3*R1*R2*R4) + C1*C2*C3*R1*R3*R4;

        const float c = 2.0f * sampleRate;
        const float c2 = c * c;
        const float c3 = c2 * c;
        const float nb0 = -ab0 - ab1*c - ab2*c2 - ab3*c3;
        const float nb1 = -3.0f*ab0 - ab1*c + ab2*c2 + 3.0f*ab3*c3;
        const float nb2 = -3.0f*ab0 + ab1*c + ab2*c2 - 3.0f*ab3*c3;
        const float nb3 = -ab0 + ab1*c - ab2*c2 + ab3*c3;
        const float na0 = -aa0 - aa1*c - aa2*c2 - aa3*c3;
        const float na1 = -3.0f*aa0 - aa1*c + aa2*c2 + 3.0f*aa3*c3;
        const float na2 = -3.0f*aa0 + aa1*c + aa2*c2 - 3.0f*aa3*c3;
        const float na3 = -aa0 + aa1*c - aa2*c2 + aa3*c3;

        if (std::fabs(na0) < 1.0e-30f)
        {
            b0 = 1.0f; b1 = b2 = b3 = a1 = a2 = a3 = 0.0f;
            return;
        }
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0; b1 = nb1 * invA0; b2 = nb2 * invA0; b3 = nb3 * invA0;
        a1 = na1 * invA0; a2 = na2 * invA0; a3 = na3 * invA0;
    }

    float process(float x)
    {
        const float y = b0*x + b1*x1 + b2*x2 + b3*x3 - a1*y1 - a2*y2 - a3*y3;
        x3 = x2; x2 = x1; x1 = x;
        y3 = y2; y2 = y1; y1 = y;
        return y;
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
    Jtm45ToneStack toneStack;
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

    float sag = 0.0f;

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
        const float pushed = smoothstepRange(0.40f, 0.92f, effDrive);
        // The 500pF bright cap bleeds treble across Loudness 1, most at low
        // settings; plus base sparkle from Treble/Presence. The JTM45 is a touch
        // darker than the Plexi (smaller bright cap, KT66 top), so less bright.
        const float bright = clamp01(0.32f * treble + 0.18f * pres + 0.48f * (1.0f - loud1));

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
        toneStack.update(treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f + 30.0f * bass, 0.72f,
                                   eqDb(bass, 4.8f) - 1.2f * pushed);
        stackMakeupBody.setPeaking(sampleRate, 500.0f + 180.0f * mid, 0.66f,
                                   -0.8f + 5.2f * mid + 1.5f * pushed);  // JTM45 warm mid
        phaseLowPass.setLowPass(sampleRate, 10500.0f + 1300.0f * treble + 1000.0f * pres
                                            - 2000.0f * pushed, 0.64f);
        presenceShelf.setHighShelf(sampleRate, 2500.0f + 850.0f * pres, 0.78f,
                                   -4.0f + 8.4f * pres + 0.9f * treble);

        // Marshall/Bluesbreaker-era cab (greenback/G12-ish but darker than the
        // 100W Plexi): tight HP, low thump, warm mid bite, fizz notch + earlier
        // top rolloff (KT66 voice).
        speakerHp.setHighPass(sampleRate, 80.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 112.0f, 0.84f, 0.9f + 2.4f * bass);
        speakerLowMid.setPeaking(sampleRate, 370.0f + 90.0f * mid, 0.78f,
                                 0.8f + 2.0f * mid - 0.7f * pushed);
        speakerBite.setPeaking(sampleRate, 2550.0f + 480.0f * treble, 0.74f,
                               2.5f + 2.0f * treble + 1.1f * pres - 0.5f * pushed);   // softer than Plexi
        speakerFizzNotch.setHighShelf(sampleRate, 4700.0f, 0.70f,
                                      9.5f + 2.0f * treble + 2.0f * pres - 4.5f * pushed);
        speakerLp.setLowPass(sampleRate, 14500.0f + 1800.0f * treble + 850.0f * pres
                                         - 3500.0f * pushed, 0.66f);   // brighter, miked-cab top
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
        sag = 0.0f;
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        toneStack.setSampleRate(sampleRate);
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
            case kLoudness1: loud1 = v; break;
            case kLoudness2: loud2 = v; break;
            case kInput:     input = v; break;
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
        const float g = smoothstep(effDrive);
        const float pushed = smoothstepRange(0.40f, 0.92f, effDrive);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = softClip(x * (1.05f + 0.10f * pushed)) * (0.95f - 0.04f * pushed);

        // HIGH TREBLE (bright) channel: 500pF bright cap + body, its own 12AX7.
        float bch = brightCapShelf.process(brightBody.process(x));
        bch = asymTube(bch, 1.05f + 3.10f * effDrive + 3.0f * g, 0.013f + 0.017f * effDrive);
        // NORMAL channel: darker, its own triode.
        float nch = normalBody.process(x);
        nch = asymTube(nch, 0.90f + 2.4f * effDrive + 2.2f * g, 0.010f + 0.014f * effDrive);

        // Jumpered mix: each channel scaled by its Loudness pot, gated by the cable.
        float y = brightG * loud1 * bch + normalG * loud2 * 0.92f * nch;
        // A little clean leak at low drive (the JTM45 stays articulate when quiet).
        const float cleanLeak = 0.32f * (1.0f - smoothstepRange(0.26f, 0.74f, effDrive));
        y = y * (1.0f - cleanLeak) + x * cleanLeak * (brightG * loud1 + normalG * loud2 * 0.5f);

        // 12AX7 recovery / cathode follower into the tone stack — the grind stage.
        y = interstageHp.process(y);
        y = asymTube(y, 0.90f + 1.9f * effDrive + 2.4f * pushed, -0.006f - 0.011f * effDrive);
        y = cathodeFollowerLp.process(y);

        y = toneStack.process(y) * 1.70f;
        y = stackMakeupLow.process(y);
        y = stackMakeupBody.process(y);
        y = phaseLowPass.process(y);

        // 2x KT66 push-pull (~30W) with a GZ34 (5AR4) TUBE rectifier. Vs the
        // Plexi's 4x EL34 / solid-state-ish supply: more compression, MUCH more
        // sag, softer/earlier breakup and a warmer, darker top.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0070f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.180f * sampleRate)); // slow GZ34 recovery
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.62f + 1.20f * effDrive + 0.70f * pushed));

        const float powerDrive = (0.95f + 1.75f * effDrive + 2.7f * pushed) * sagDrop;
        y = asymTube(y, powerDrive, 0.008f + 0.014f * (treble - bass) + 0.010f * pres);
        y = 0.84f * y + 0.16f * softClip(y * (1.65f + 1.40f * pushed));
        y *= 0.97f - 0.12f * sag;  // more sag droop than the Plexi

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizzNotch.process(y);
        y = speakerLp.process(y);

        // Loudness normalization: the Loudness-as-gain means a low setting is much
        // quieter than a cranked one. cleanMakeup lifts the quiet end so the RS
        // Gain (-> Loudness 1) sweep stays within a couple dB and the shared kLvl
        // stage stays calibrated (~-14 dBFS reference).
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.010f * std::fabs((pres - 0.5f) * 16.0f);
        const float cleanMakeup = 1.0f + 13.0f * std::exp(-effDrive / 0.190f);
        const float level = (0.350f + 0.09f * (1.0f - effDrive)) * cleanMakeup /
            ((1.0f + 0.30f * effDrive + 0.16f * pushed) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class Jtm45Plugin : public Plugin
{
    Jtm45Core left;
    Jtm45Core right;
    float params[kParamCount];

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
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Jtm45Plugin)
};

Plugin* createPlugin()
{
    return new Jtm45Plugin();
}

END_NAMESPACE_DISTRHO
