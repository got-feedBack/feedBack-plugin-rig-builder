/*
 * MARSTEN PLEXI - Marshall 1959 Super Lead 100W (Plexi/JMP) for the game's
 * Amp_MarshallPlexi. Parody brand "Marsten" (same as the DSL100 / GM-2 / UV-1);
 * the in-app face must never read "Marshall".
 *
 * Local reference (modelled component-by-component):
 *   amps/Marshall Plexi/1959-01-60-02.pdf  (1959SLP preamp + power schematics)
 *
 * Full 1959 Super Lead front panel, 1:1 (see PlexiParams.h): a NON-MASTER amp
 * whose two Loudness pots ARE the gain. The HIGH TREBLE ("bright") channel runs
 * a 5000pF bright cap across Loudness I; the NORMAL channel is darker. Both
 * channels mix (the jumpered plexi voice) into a 12AX7 recovery + cathode
 * follower -> the Marshall tone stack (Treble 250K / Bass 1M / Middle 25K, 33K
 * slope, 500pF treble cap) -> long-tail PI -> 4x EL34 (~100W, hot midrange
 * grind, early compressing breakup) -> output transformer. PRESENCE taps the
 * power-amp NFB.
 *
 * the game: no gain knob, so RS Gain -> Loudness I (clean->crunch->roar);
 * Treble/Bass/Mid -> tone stack, Pres -> Presence. See rs_knob_to_vst_param.json
 * (Loudness II pinned to a musical jumpered blend via _static).
 */
#include "DistrhoPlugin.hpp"
#include "PlexiParams.h"
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

// The Marshall 1959 passive tone stack — same FMV topology as the Bassman it
// descends from, but with the Marshall component values: Treble 250K, Bass 1M,
// Middle 25K, slope R 33K, C 500pF / 22nF / 22nF (off the 1959SLP schematic).
class PlexiToneStack
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

        const float R1 = 250.0e3f;   // Treble pot
        const float R2 = 1.0e6f;     // Bass pot
        const float R3 = 25.0e3f;    // Middle pot
        const float R4 = 33.0e3f;    // slope resistor (Marshall: 33K, vs 56K Bassman)
        const float C1 = 500.0e-12f; // Treble cap (Marshall: 500pF, vs 250pF Bassman)
        const float C2 = 22.0e-9f;   // Bass cap
        const float C3 = 22.0e-9f;   // Middle cap

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

class PlexiCore
{
    float sampleRate = 48000.0f;
    float pres   = kPlexiDef[kPresence];
    float bass   = kPlexiDef[kBass];
    float mid    = kPlexiDef[kMiddle];
    float treble = kPlexiDef[kTreble];
    float loud1  = kPlexiDef[kLoudness1];
    float loud2  = kPlexiDef[kLoudness2];
    float input  = kPlexiDef[kInput];

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
    PlexiToneStack toneStack;
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
        // The 5000pF bright cap bleeds a LOT of treble across Loudness I, most at
        // low settings; plus base sparkle from Treble/Presence (Marshall is bright).
        const float bright = clamp01(0.34f * treble + 0.20f * pres + 0.55f * (1.0f - loud1));

        inputHp.setHighPass(sampleRate, 44.0f + 56.0f * g + 30.0f * pushed, 0.70f);
        pickupLoad.setLowPass(sampleRate, 13200.0f - 1700.0f * pushed + 900.0f * treble, 0.64f);
        brightCapShelf.setHighShelf(sampleRate, 1400.0f + 1150.0f * treble, 0.70f,
                                    -1.0f + 6.6f * bright + 1.9f * pres);
        brightBody.setPeaking(sampleRate, 700.0f + 430.0f * mid, 0.80f,
                              -0.8f + 3.1f * mid + 1.0f * bright);
        normalBody.setPeaking(sampleRate, 180.0f + 55.0f * bass, 0.72f,
                              0.7f + 2.6f * bass - 1.1f * pushed);

        interstageHp.setHighPass(sampleRate, 62.0f + 80.0f * pushed + 40.0f * (1.0f - bass), 0.70f);
        cathodeFollowerLp.setLowPass(sampleRate, 9000.0f + 1700.0f * treble - 1500.0f * pushed, 0.64f);
        toneStack.update(treble, mid, bass);
        stackMakeupLow.setLowShelf(sampleRate, 120.0f + 30.0f * bass, 0.72f,
                                   eqDb(bass, 4.6f) - 1.3f * pushed);
        stackMakeupBody.setPeaking(sampleRate, 520.0f + 180.0f * mid, 0.66f,
                                   -1.0f + 5.0f * mid + 1.5f * pushed);  // Marshall mid grind
        phaseLowPass.setLowPass(sampleRate, 15500.0f + 1700.0f * treble + 1200.0f * pres
                                            - 500.0f * pushed, 0.64f);
        presenceShelf.setHighShelf(sampleRate, 2600.0f + 900.0f * pres, 0.78f,
                                   -4.0f + 9.2f * pres + 1.0f * treble);

        // Marshall 4x12 (greenback-ish): tight HP, low thump, upper-mid bite, fizz
        // notch + ~5.5kHz rolloff.
        speakerHp.setHighPass(sampleRate, 82.0f, 0.72f);
        speakerThump.setPeaking(sampleRate, 115.0f, 0.84f, 0.8f + 2.3f * bass);
        speakerLowMid.setPeaking(sampleRate, 380.0f + 90.0f * mid, 0.78f,
                                 0.7f + 1.9f * mid - 0.7f * pushed);
        speakerBite.setPeaking(sampleRate, 2750.0f + 520.0f * treble, 0.74f,
                               2.4f + 2.4f * treble + 1.4f * pres - 0.6f * pushed);   // the Marshall crunch bite
        // Was a fizz NOTCH (top cut, made it dark). Now an AIR high-shelf: lifts the
        // 4x12 top, retreats with gain (de-fizz on crank). Member name kept.
        speakerFizzNotch.setHighShelf(sampleRate, 5400.0f, 0.70f,
                                      8.8f + 2.0f * treble + 2.0f * pres - 2.6f * pushed);
        // Speaker LP opened from ~5.8k (too dark) to ~14.5k (miked 4x12), eases on crank.
        speakerLp.setLowPass(sampleRate, 19500.0f + 2000.0f * treble + 1200.0f * pres
                                         - 800.0f * pushed, 0.66f);
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
            setParam(i, kPlexiDef[i]);
    }

    float process(float in)
    {
        const float g = smoothstep(effDrive);
        const float pushed = smoothstepRange(0.40f, 0.92f, effDrive);

        float x = inputHp.process(in);
        x = pickupLoad.process(x);
        x = softClip(x * (1.05f + 0.10f * pushed)) * (0.95f - 0.04f * pushed);

        // HIGH TREBLE (bright) channel: 5000pF bright cap + body, its own 12AX7.
        float bch = brightCapShelf.process(brightBody.process(x));
        bch = asymTube(bch, 1.10f + 3.40f * effDrive + 3.2f * g, 0.014f + 0.018f * effDrive);
        // NORMAL channel: darker, its own triode.
        float nch = normalBody.process(x);
        nch = asymTube(nch, 0.95f + 2.6f * effDrive + 2.4f * g, 0.011f + 0.015f * effDrive);

        // Jumpered mix: each channel scaled by its Loudness pot, gated by the cable.
        float y = brightG * loud1 * bch + normalG * loud2 * 0.92f * nch;
        // A little clean leak at low drive (the plexi stays articulate when quiet).
        const float cleanLeak = 0.30f * (1.0f - smoothstepRange(0.26f, 0.74f, effDrive));
        y = y * (1.0f - cleanLeak) + x * cleanLeak * (brightG * loud1 + normalG * loud2 * 0.5f);

        // 12AX7 recovery / cathode follower into the tone stack — the grind stage.
        y = interstageHp.process(y);
        y = asymTube(y, 0.95f + 2.0f * effDrive + 2.6f * pushed, -0.007f - 0.012f * effDrive);
        y = cathodeFollowerLp.process(y);

        y = toneStack.process(y) * 1.70f;
        y = stackMakeupLow.process(y);
        y = stackMakeupBody.process(y);
        y = phaseLowPass.process(y);

        // 4x EL34 push-pull (~100W) — EL34s break up earlier and compress more
        // than the Bassman's 6L6/5881, with aggressive midrange grind.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0055f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.130f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.40f + 0.95f * effDrive + 0.55f * pushed));

        const float powerDrive = (1.05f + 1.90f * effDrive + 3.0f * pushed) * sagDrop;
        y = asymTube(y, powerDrive, 0.008f + 0.016f * (treble - bass) + 0.012f * pres);
        y = 0.82f * y + 0.18f * softClip(y * (1.80f + 1.50f * pushed));
        y *= 0.97f - 0.09f * sag;

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
        // Gain (-> Loudness I) sweep stays within a couple dB and the shared kLvl
        // stage stays calibrated (~-14 dBFS reference).
        const float toneEnergy = 1.0f
            + 0.011f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.012f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.012f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.010f * std::fabs((pres - 0.5f) * 16.0f);
        const float cleanMakeup = 1.0f + 12.0f * std::exp(-effDrive / 0.185f);
        const float level = (0.585f + 0.15f * (1.0f - effDrive)) * cleanMakeup /
            ((1.0f + 0.30f * effDrive + 0.16f * pushed) * toneEnergy);
        return softClip(y * level) * 0.97f;
    }
};

class PlexiPlugin : public Plugin
{
    PlexiCore left;
    PlexiCore right;
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
    PlexiPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kPlexiDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "Plexi"; }
    const char* getDescription() const override { return "Marshall 1959 Super Lead Plexi style amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('P', 'l', '5', '9'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kPlexiNames[index];
        parameter.symbol = kPlexiSymbols[index];
        parameter.ranges.min = kPlexiMin[index];
        parameter.ranges.max = kPlexiMax[index];
        parameter.ranges.def = kPlexiDef[index];
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
            outL[i] = rbAmpLvl(0.550f * left.process(3.2f * inL[i]));
            outR[i] = rbAmpLvl(0.550f * right.process(3.2f * inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlexiPlugin)
};

Plugin* createPlugin()
{
    return new PlexiPlugin();
}

END_NAMESPACE_DISTRHO
