#ifndef TW26_CORE_H
#define TW26_CORE_H

/*
 * TW26Core - BENDER DELUXE / Fender '57 Deluxe (5E3 tweed) component model.
 *
 * White-box audio model: each audible block is driven by the 5E3 schematic
 * component values rather than a literal SPICE solve.
 *
 * Local reference:
 *   amps/Fender Deluxe (TW26)/Fender-57-Deluxe-Schematic.pdf
 *
 * 5E3 topology modelled here:
 *   Input 68k grid -> V1 12AY7 first stage (low-mu ~44 = warm, early, soft) ->
 *   interactive Volume -> single tweed Tone network (1M pot, C5 .0047uF,
 *   C4 500pF) -> V2-A 12AX7 recovery gain -> V2-B 12AX7 cathodyne (split-load)
 *   phase inverter -> 2x 6V6GT push-pull, CATHODE-biased (R23 250R), NO global
 *   NFB -> 5Y3GT tube rectifier (heavy, blooming sag) -> 1x12 tweed speaker.
 *
 * The 5E3 is mid-forward and woody (NOT scooped like a blackface), compresses
 * and "blooms" early because of the tube rectifier + cathode bias, and breaks
 * up into a loose tweed grind when pushed.
 *
 * the game exposes Gain, Bass, Mid, Treble, Pres. The real amp has only two
 * Volumes + one Tone, so: Gain drives clean->tweed-breakup; Bass/Mid/Treble are
 * a tweed-voiced 3-band expansion of the single Tone control (kept mid-rich);
 * Pres is a gentle top-end lift (the 5E3 has no presence pot).
 */

#include "TW26Params.h"
#include <cmath>

namespace tw26 {

static constexpr float kPi = 3.14159265359f;
static constexpr float kEpsilon = 1.0e-9f;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float clampFreq(float hz, float sr)
{
    const float nyquist = sr * 0.45f;
    return std::fmax(10.0f, std::fmin(hz, nyquist));
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

// --- smooth (C-infinity) tube transfer curves: asymmetric for even harmonics,
//     NO piecewise zero-crossing kink (that injects buzz even on clean tones).
static inline float triode12AY7(float x, float bias)
{
    // 12AY7: lower mu than a 12AX7 -> warmer, earlier, softer. Used at V1.
    const float g = x + bias;
    const float warped = 1.28f * g + 0.30f * g * std::fabs(g);
    const float idle   = 1.28f * bias + 0.30f * bias * std::fabs(bias);
    return std::tanh(warped) - std::tanh(idle);
}

static inline float triode12AX7(float x, float bias)
{
    const float g = x + bias;
    const float warped = 1.55f * g + 0.34f * g * std::fabs(g);
    const float idle   = 1.55f * bias + 0.34f * bias * std::fabs(bias);
    return std::tanh(warped) - std::tanh(idle);
}

static inline float sixV6Pair(float x, float bias)
{
    // Cathode-biased push-pull pair (smooth, symmetric-ish with a touch of even
    // harmonic from the shared-cathode bias shift).
    const float p = std::tanh(1.28f * (x + bias) + 0.05f * x * x);
    const float n = std::tanh(1.28f * (-x + bias) + 0.05f * x * x);
    const float idle = std::tanh(1.28f * bias);
    return 0.5f * ((p - idle) - (n - idle));
}

class RcHighPass
{
    float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
public:
    void reset() { x1 = y1 = 0.0f; }
    void setRC(float sr, float resistanceOhm, float capacitanceF)
    {
        const float dt = 1.0f / std::fmax(sr, 1000.0f);
        const float tau = std::fmax(resistanceOhm * capacitanceF, kEpsilon);
        a = tau / (tau + dt);
    }
    void setHz(float sr, float hz)
    {
        hz = clampFreq(hz, sr);
        const float tau = 1.0f / (2.0f * kPi * hz);
        const float dt = 1.0f / std::fmax(sr, 1000.0f);
        a = tau / (tau + dt);
    }
    float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x; y1 = y;
        return y;
    }
};

class RcLowPass
{
    float a = 1.0f, z = 0.0f;
public:
    void reset() { z = 0.0f; }
    void setRC(float sr, float resistanceOhm, float capacitanceF)
    {
        const float dt = 1.0f / std::fmax(sr, 1000.0f);
        const float tau = std::fmax(resistanceOhm * capacitanceF, kEpsilon);
        a = dt / (tau + dt);
    }
    void setHz(float sr, float hz)
    {
        hz = clampFreq(hz, sr);
        const float tau = 1.0f / (2.0f * kPi * hz);
        const float dt = 1.0f / std::fmax(sr, 1000.0f);
        a = dt / (tau + dt);
    }
    float process(float x) { z += a * (x - z); return z; }
};

class Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f) na0 = 1.0f;
        const float invA0 = 1.0f / na0;
        b0 = nb0 * invA0; b1 = nb1 * invA0; b2 = nb2 * invA0;
        a1 = na1 * invA0; a2 = na2 * invA0;
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
    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr, c = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f, 1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }
    void setHighPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        const float w0 = 2.0f * kPi * hz / sr, c = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f, 1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }
    void setPeaking(float sr, float hz, float q, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr, c = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a, 1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }
    void setLowShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr, c = std::cos(w0), s = std::sin(w0), rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
        set(a * ((a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha),
            2.0f * a * ((a - 1.0f) - (a + 1.0f) * c),
            a * ((a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha,
            -2.0f * ((a - 1.0f) + (a + 1.0f) * c),
            (a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
    void setHighShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr);
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * hz / sr, c = std::cos(w0), s = std::sin(w0), rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
        set(a * ((a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha),
            -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
            a * ((a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha),
            (a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha,
            2.0f * ((a - 1.0f) - (a + 1.0f) * c),
            (a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
};

class DcBlock
{
    float x1 = 0.0f, y1 = 0.0f;
public:
    void reset() { x1 = y1 = 0.0f; }
    float process(float x) { const float y = x - x1 + 0.995f * y1; x1 = x; y1 = y; return y; }
};

// 5Y3GT tube rectifier + cathode-biased 6V6 supply: deep, slow, blooming sag —
// the heart of the 5E3 "feel". Returns a 0..1 supply scale.
class Y3CathodeBiasedSupply
{
    float sr = 48000.0f, rectSag = 0.0f, cathodeRise = 0.0f;
    float sagAttack = 0.0f, sagRelease = 0.0f, cathodeCoeff = 0.0f;
public:
    void reset() { rectSag = 0.0f; cathodeRise = 0.0f; }
    void setSampleRate(float sampleRate)
    {
        sr = sampleRate > 1000.0f ? sampleRate : 48000.0f;
        // 5Y3 has high internal resistance + small filter caps -> quick dip,
        // slow bloom recovery. Slower/deeper than a solid-state rectifier.
        sagAttack = 1.0f - std::exp(-1.0f / (0.009f * sr));
        sagRelease = 1.0f - std::exp(-1.0f / (0.220f * sr));
        // 6V6 shared cathode R23 250R with 25u -> ~6 ms bias shift.
        const float tau = 250.0f * 25.0e-6f;
        cathodeCoeff = 1.0f - std::exp(-1.0f / (std::fmax(tau, 0.001f) * sr));
    }
    float process(float env, float gain, float hot)
    {
        rectSag += (env - rectSag) * (env > rectSag ? sagAttack : sagRelease);
        cathodeRise += (env - cathodeRise) * cathodeCoeff;
        const float bPlus = 1.0f / (1.0f + rectSag * (0.55f + 1.25f * gain + 0.55f * hot));
        const float cathodeBias = 1.0f / (1.0f + cathodeRise * (0.30f + 0.50f * gain));
        return bPlus * cathodeBias;
    }
};

class TW26Core
{
    float sampleRate = 48000.0f;
    float tone    = kTW26Def[kTone];
    float instVol = kTW26Def[kInstVol];   // Instrument Volume (= gain)
    float micVol  = kTW26Def[kMicVol];    // Mic Volume (jumpered body)
    float bright  = kTW26Def[kBright];    // Instrument bright input
    float bass    = kTW26Def[kBass];      // hidden low shelf
    float presK   = kTW26Def[kPresence];  // hidden power-amp top lift

    RcLowPass inputGrid;        // 68k grid into V1 Miller capacitance
    RcHighPass instBright;      // bright input cap (input 1 bright vs 2 normal)
    RcHighPass v1Coupling;      // C2 .1uF into the volume/tone
    RcLowPass v1Miller;
    RcHighPass micCoupling;     // Mic channel input coupling (jumpered second ch)
    RcLowPass micLp;            // Mic channel is darker (no bright cap)
    Biquad micBody;             // Mic channel low-mid body (the jumper fill)
    RcHighPass toneTrebleBleed; // C5 .0047uF treble path of the single Tone pot
    Biquad toneShelf;           // the single tweed Tone control (bright<->dark)
    RcHighPass v2Coupling;      // C7 .022uF into V2-A
    Biquad bassShelf;           // hidden RS Bass (5E3 has no bass pot)
    Biquad tweedBody;           // fixed woody low-mid hump (tweed is mid-forward)
    Biquad presenceShelf;       // hidden RS Pres (the 5E3 has no presence pot)
    RcHighPass piCoupling;      // C9/C10 to the 6V6 grids
    Y3CathodeBiasedSupply supply;
    Biquad transformerLow;      // output transformer low resonance
    Biquad speakerHp;
    Biquad speakerBody;         // 1x12 tweed cone low-mid bump
    Biquad speakerPresence;     // gentle upper-mid (tweed alnico, softer than V30)
    Biquad speakerAir;          // 4.5k air shelf toward the UAD Woodrow reference top
    Biquad speakerLp;           // tweed top rolloff (darker than a modern cab)
    DcBlock dcBlock;

    static float eqDb(float v, float rangeDb) { return (clamp01(v) - 0.5f) * 2.0f * rangeDb; }

    void updateComponentValues()
    {
        const float g = smoothstep(instVol);                  // 5E3 volume = gain
        const float hot = smoothstepRange(0.50f, 0.98f, instVol);

        inputGrid.setRC(sampleRate, 68000.0f, 50.0e-12f);     // ~47 kHz ceiling
        instBright.setHz(sampleRate, 1500.0f);                // bright-input treble cap
        v1Coupling.setRC(sampleRate, 1000000.0f, 0.1e-6f);    // C2 .1uF / 1M -> ~1.6 Hz
        v1Miller.setRC(sampleRate, 100000.0f, 90.0e-12f);     // plate/Miller rolloff

        // Mic channel (the jumpered second channel): darker, low-mid forward.
        micCoupling.setRC(sampleRate, 1000000.0f, 0.1e-6f);
        micLp.setHz(sampleRate, 3600.0f);
        micBody.setPeaking(sampleRate, 480.0f, 0.80f, 3.5f);

        // Single tweed Tone control (treble bleed + a shelf for real range). Its
        // corner lands in the presence band; the cap sees the network's effective
        // R, not the raw 1M pot (that would push the corner sub-audio).
        toneTrebleBleed.setRC(sampleRate, 1.0f / (2.0f * kPi * (2200.0f + 1800.0f * tone) * 0.0047e-6f), 0.0047e-6f);
        toneShelf.setHighShelf(sampleRate, 1900.0f + 1100.0f * tone, 0.66f, -8.0f + 18.0f * tone);
        v2Coupling.setRC(sampleRate, 470000.0f, 0.022e-6f);   // C7 .022uF -> ~15 Hz

        // --- tweed body + hidden RS Bass / Pres ---
        bassShelf.setLowShelf(sampleRate, 115.0f, 0.72f, eqDb(bass, 9.5f) + 2.4f);
        tweedBody.setPeaking(sampleRate, 340.0f, 0.85f, 1.2f + 1.1f * g);
        presenceShelf.setHighShelf(sampleRate, 3000.0f, 0.80f, -1.0f + 5.5f * presK);

        piCoupling.setRC(sampleRate, 220000.0f, 0.1e-6f);     // ~7 Hz into the 6V6 grids
        supply.setSampleRate(sampleRate);

        // --- output transformer + 1x12 tweed speaker (Jensen-style, warm) ---
        transformerLow.setPeaking(sampleRate, 95.0f, 0.72f, 2.0f + 1.8f * bass);
        speakerHp.setHighPass(sampleRate, 70.0f, 0.72f);
        speakerBody.setPeaking(sampleRate, 175.0f, 0.80f, 2.6f + 1.8f * bass - 0.5f * hot);
        // Less upper-mid honk: the old +2.4 dB bump at 2.1k sat ~3 dB above the UAD
        // Woodrow reference. Trimmed so the presence sits where the reference does.
        // Cut the 2 k honk: the reference is SCOOPED there (we sat ~3 dB over). Slight dip.
        speakerPresence.setPeaking(sampleRate, 2300.0f + 350.0f * tone, 0.85f, -3.2f + 1.0f * tone + 1.0f * presK);
        // AIR shelf placed HIGH (5.5 k) so it lifts 6-14 k (where the Woodrow has air) WITHOUT
        // re-boosting the 2-4 k presence. The reference top rises toward 14 k; LP opened to 16 k.
        speakerAir.setHighShelf(sampleRate, 4000.0f, 0.7f, 16.5f + 2.0f * tone + 3.0f * presK);
        speakerLp.setLowPass(sampleRate, 16000.0f + 2000.0f * presK + 1500.0f * tone, 0.66f);
    }

public:
    void reset()
    {
        inputGrid.reset(); instBright.reset(); v1Coupling.reset(); v1Miller.reset();
        micCoupling.reset(); micLp.reset(); micBody.reset();
        toneTrebleBleed.reset(); toneShelf.reset(); v2Coupling.reset();
        bassShelf.reset(); tweedBody.reset(); presenceShelf.reset();
        piCoupling.reset(); supply.reset();
        transformerLow.reset(); speakerHp.reset(); speakerBody.reset(); speakerPresence.reset(); speakerAir.reset(); speakerLp.reset();
        dcBlock.reset();
        updateComponentValues();
    }

    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }

    void setTone(float v)    { tone = clamp01(v);    updateComponentValues(); }
    void setInstVol(float v) { instVol = clamp01(v); updateComponentValues(); }
    void setMicVol(float v)  { micVol = clamp01(v);  updateComponentValues(); }
    void setBright(float v)  { bright = clamp01(v);  updateComponentValues(); }
    void setBass(float v)    { bass = clamp01(v);    updateComponentValues(); }
    void setPresence(float v){ presK = clamp01(v);   updateComponentValues(); }

    float process(float in)
    {
        const float g = smoothstep(instVol);                 // 5E3 volume = gain
        const float hot = smoothstepRange(0.50f, 0.98f, instVol);

        float x = inputGrid.process(in);

        // Instrument bright input: input 1 (bright cap) vs input 2 (normal/darker)
        float xi = x + instBright.process(x) * (0.45f * bright);

        // V1 12AY7 first stage — warm, low-mu, soft early compression.
        float inst = v1Coupling.process(xi);
        const float v1Drive = 0.80f + 1.55f * instVol + 0.55f * g;
        inst = triode12AY7(inst * (v1Drive * 0.72f), -0.020f - 0.020f * instVol);
        inst = v1Miller.process(inst);

        // Mic channel jumpered in (RS Mid): a darker, low-mid-forward parallel
        // 12AY7 voice that fills body underneath the Instrument channel.
        float y = inst;
        if (micVol > 0.001f)
        {
            float mic = micLp.process(micCoupling.process(x));
            mic = triode12AY7(mic * (0.7f + 1.1f * micVol), -0.020f);
            mic = micBody.process(mic);
            y += mic * (0.55f + 1.05f * micVol);
        }

        // interactive Volume + single tweed Tone control (treble bleed + shelf)
        const float volume = 0.30f + 1.35f * g + 0.55f * hot;
        y = (y + toneTrebleBleed.process(y) * (0.18f + 0.42f * tone)) * volume;
        y = toneShelf.process(y);

        // --- tweed body + hidden RS Bass ---
        y = bassShelf.process(y);
        y = tweedBody.process(y);

        // V2-A 12AX7 recovery/gain stage
        y = v2Coupling.process(y);
        const float v2Drive = 0.85f + 1.65f * instVol + 1.55f * hot;
        y = triode12AX7(y * (v2Drive * 0.78f), -0.012f - 0.018f * instVol);

        // --- V2-B cathodyne PI + 6V6 push-pull, cathode-biased, no NFB,
        //     5Y3 blooming sag ---
        y = piCoupling.process(y);
        const float env = std::fabs(y);
        const float supplyScale = supply.process(env, instVol, hot);
        const float powerDrive = (1.02f + 1.40f * instVol + 2.10f * hot) * supplyScale;
        y = sixV6Pair(y * powerDrive, 0.03f + 0.012f * (tone - bass));
        y = (0.92f * y + 0.08f * softClip(y * (1.5f + 0.9f * instVol))) * supplyScale;

        y = dcBlock.process(y);

        // --- output transformer + 1x12 tweed speaker + hidden presence ---
        y = transformerLow.process(y);
        y = speakerHp.process(y);
        y = speakerBody.process(y);
        y = speakerPresence.process(y);
        y = presenceShelf.process(y);
        y = speakerAir.process(y);
        y = speakerLp.process(y);

        // output makeup: hold perceived level ~constant across the Volume(=gain)
        // sweep AND the jumpered Mic channel, so it sits at the BOX DC30 level.
        const float micBlend = (micVol > 0.001f) ? micVol : 0.0f;
        const float toneEnergy = 1.0f
            + 0.013f * std::fabs((bass - 0.5f) * 20.0f)
            + 0.013f * std::fabs((tone - 0.5f) * 20.0f)
            + 0.06f * micBlend;
        // The heavy 5Y3/cathode-bias sag pulls the steady level DOWN as the volume
        // rises, so the makeup RISES with it to hold loudness ~constant while
        // keeping the sag bloom/compression dynamics intact.
        // RS Gain = distortion ONLY; the game holds the output VOLUME fixed. The base
        // makeup (rising with g) offsets the 5Y3 sag; the extra low-gain term normalizes
        // the clean/quiet end so LOW RS Gain plays at full volume too (was ~10 dB down).
        const float makeup = (1.50f + 0.40f * g + 0.70f * hot) * (1.0f + 2.0f / (1.0f + g * 36.0f));
        const float level = makeup / toneEnergy;
        return softClip(y * level) * 0.98f;
    }
};

} // namespace tw26

#endif // TW26_CORE_H
