/*
 * RONALD JC-120 - Roland JC-120 "Jazz Chorus" for the game's Amp_CS120. Parody
 * brand "Ronald" (Roland -> Ronald); the in-app face must never read "Roland".
 *
 * Local reference (modelled from the service manual):
 *   amps/Roland JC-120 (CS-120)/JC-120, JC-160 (79-5) Jazz Chorus Service Manual.pdf
 *
 * SOLID-STATE 120W (NJM4558/M5218-class op-amps + 2SD736A transistor power amp,
 * NO tubes), 2x 30cm (12") speakers. The Effect channel: a clean, high-headroom
 * preamp with a diode-clipping DISTORTION, a passive tone stack (+ BRIGHT), a
 * spring REVERB (Z-3F), and the signature analogue BBD (MN3007) STEREO CHORUS /
 * Vibrato — the dry feeds one speaker and the pitch-modulated wet the other, so
 * the chorus opens up wide. See JC120Params.h.
 *
 * the game: Gain -> Distortion (clean at 0). Treble/Mid/Bass -> tone stack.
 * Reverb/Chorus OFF for songs (RS adds those separately).
 */
#include "DistrhoPlugin.hpp"
#include "JC120Params.h"
#include "../../_shared/oversampler.hpp"
#include <cmath>
#include <cstring>

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
static inline float eqDb(float v, float rangeDb) { return (clamp01(v) - 0.5f) * 2.0f * rangeDb; }

class Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f) na0 = 1.0f;
        const float inv = 1.0f / na0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv; a1 = na1 * inv; a2 = na2 * inv;
    }
public:
    void reset() { z1 = z2 = 0.0f; }
    float process(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void setHighPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr); const float w0 = 2.0f * kPi * hz / sr, c = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f, 1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }
    void setLowPass(float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr); const float w0 = 2.0f * kPi * hz / sr, c = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f, 1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }
    void setPeaking(float sr, float hz, float q, float gainDb)
    {
        hz = clampFreq(hz, sr); const float a = std::pow(10.0f, gainDb / 40.0f), w0 = 2.0f * kPi * hz / sr, c = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
        set(1.0f + alpha * a, -2.0f * c, 1.0f - alpha * a, 1.0f + alpha / a, -2.0f * c, 1.0f - alpha / a);
    }
    void setHighShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr); const float a = std::pow(10.0f, gainDb / 40.0f), w0 = 2.0f * kPi * hz / sr, c = std::cos(w0), s = std::sin(w0), rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
        set(a * ((a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha), -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c),
            a * ((a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha), (a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha,
            2.0f * ((a - 1.0f) - (a + 1.0f) * c), (a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
    void setLowShelf(float sr, float hz, float slope, float gainDb)
    {
        hz = clampFreq(hz, sr); const float a = std::pow(10.0f, gainDb / 40.0f), w0 = 2.0f * kPi * hz / sr, c = std::cos(w0), s = std::sin(w0), rootA = std::sqrt(a);
        const float alpha = s * 0.5f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
        set(a * ((a + 1.0f) - (a - 1.0f) * c + 2.0f * rootA * alpha), 2.0f * a * ((a - 1.0f) - (a + 1.0f) * c),
            a * ((a + 1.0f) - (a - 1.0f) * c - 2.0f * rootA * alpha), (a + 1.0f) + (a - 1.0f) * c + 2.0f * rootA * alpha,
            -2.0f * ((a - 1.0f) + (a + 1.0f) * c), (a + 1.0f) + (a - 1.0f) * c - 2.0f * rootA * alpha);
    }
};

class DcBlock
{
    float x1 = 0.0f, y1 = 0.0f;
public:
    void reset() { x1 = y1 = 0.0f; }
    float process(float x) { const float y = x - x1 + 0.995f * y1; x1 = x; y1 = y; return y; }
};

// Real anti-parallel silicon diode-pair clipper (Shockley model). A series R drives
// a shunt pair of back-to-back diodes to ground; the node voltage v (= the output)
// solves the node KCL by Newton (the physical-solve style, NOT a tanh fit):
//     (vin - v)/R = 2*Is*sinh(v / (n*Vt))            [two diodes, anti-parallel]
// Below ~0.45 V the diodes are off and v ≈ vin (the JC stays clean); above it they
// clamp with the exponential knee — a sharper, buzzier, odd-harmonic solid-state edge.
struct DiodeClipper
{
    static constexpr float Is  = 2.52e-9f;            // 1N4148-class saturation current
    static constexpr float nVt = 1.752f * 0.02585f;   // emission coeff * thermal voltage (~45 mV)
    float R = 10000.0f;                               // real R37 = 10k series into the shunt diode pair
    float v = 0.0f;                                   // warm-start from the previous sample
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
            if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;   // bound -> sinh can't overflow
        }
        return v;
    }
};

// --- spring reverb (3 allpass diffusers + 2 damped combs), band-limited ---
class SpringReverb
{
    float ap0[1024], ap1[1024], ap2[1024];
    float cb0[3600], cb1[3600];
    int p0 = 0, p1 = 0, p2 = 0, c0 = 0, c1 = 0;
    int n0 = 225, n1 = 341, n2 = 441, nc0 = 1617, nc1 = 1991;
    float damp0 = 0.0f, damp1 = 0.0f;
    Biquad inHp, inLp;
    static inline float apStep(float* buf, int& p, int n, float in, float g)
    { const float bo = buf[p]; const float v = in + bo * g; buf[p] = v; if (++p >= n) p = 0; return bo - v * g; }
public:
    void setSampleRate(float sr)
    {
        const float s = (sr > 1000.0f ? sr : 48000.0f) / 48000.0f;
        n0 = (int)(225 * s); n1 = (int)(341 * s); n2 = (int)(441 * s);
        if (n0 > 1023) n0 = 1023; if (n1 > 1023) n1 = 1023; if (n2 > 1023) n2 = 1023;  // ap[1024] bound (192k)
        nc0 = (int)(1617 * s); nc1 = (int)(1991 * s);
        if (nc0 > 3599) nc0 = 3599; if (nc1 > 3599) nc1 = 3599;
        inHp.setHighPass(sr, 240.0f, 0.7f); inLp.setLowPass(sr, 3800.0f, 0.7f);
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
        x = apStep(ap0, p0, n0, x, 0.6f); x = apStep(ap1, p1, n1, x, 0.6f); x = apStep(ap2, p2, n2, x, 0.6f);
        const float o0 = cb0[c0]; damp0 += 0.42f * (o0 - damp0); cb0[c0] = x + damp0 * 0.70f; if (++c0 >= nc0) c0 = 0;
        const float o1 = cb1[c1]; damp1 += 0.42f * (o1 - damp1); cb1[c1] = x + damp1 * 0.68f; if (++c1 >= nc1) c1 = 0;
        return (o0 + o1) * 0.5f;
    }
};

// --- analogue (BBD-style, MN3007) stereo chorus: one modulated delay read at
//     two LFO phases (opposite) so the two outputs spread wide, like the JC. ---
class Chorus
{
    float buf[8192]; int w = 0; float fs = 48000.0f;
    float lfo = 0.0f, inc = 0.0f;
    Biquad wetLp;   // BBD bandwidth roll-off
    static inline int wrap(int i) { return i & 8191; }
    inline float readFrac(float delaySamp)
    {
        float rp = (float)w - delaySamp; while (rp < 0.f) rp += 8192.f;
        int i0 = (int)rp; float fr = rp - (float)i0; int i1 = i0 + 1;
        return buf[wrap(i0)] + fr * (buf[wrap(i1)] - buf[wrap(i0)]);
    }
public:
    void setSampleRate(float s) { fs = s > 1000.0f ? s : 48000.0f; wetLp.setLowPass(fs, 6500.0f, 0.7f); clear(); }
    void clear() { std::memset(buf, 0, sizeof(buf)); w = 0; lfo = 0.0f; wetLp.reset(); }
    void setRate(float rate01) { const float hz = 0.10f + 7.0f * clamp01(rate01); inc = 2.0f * kPi * hz / fs; }
    // depth01 -> excursion; vibrato passes deeper. Returns the two wet voices.
    void process(float x, float depthMs, float& wetL, float& wetR)
    {
        buf[w] = x;
        const float base = 0.0080f * fs;             // ~8 ms base delay
        const float mod  = depthMs * 0.001f * fs;
        const float s = std::sin(lfo);
        wetL = wetLp.process(readFrac(base + mod * (0.5f + 0.5f * s)));
        // second voice 180 deg out so the stereo image opens
        wetR = readFrac(base + mod * (0.5f - 0.5f * s));
        lfo += inc; if (lfo > 2.0f * kPi) lfo -= 2.0f * kPi;
        if (++w >= 8192) w = 0;
    }
};

} // namespace

class JC120Core
{
    float sampleRate = 48000.0f;
    float volume     = kJC120Def[kVolume];
    float treble     = kJC120Def[kTreble];
    float mid        = kJC120Def[kMiddle];
    float bass       = kJC120Def[kBass];
    float distortion = kJC120Def[kDistortion];
    float reverb     = kJC120Def[kReverb];
    float speed      = kJC120Def[kSpeed];
    float depth      = kJC120Def[kDepth];
    float chorusMode = kJC120Def[kChorus];
    float cabSim     = kJC120Def[kCabSim];

    Biquad inputHp, inputLp, distPre, distPost;
    Biquad toneBass, toneMid, toneTreble, brightShelf;
    Biquad speakerLp, speakerThump, speakerBite;
    DcBlock dcBlock;
    SpringReverb spring;
    Chorus chorus;
    DiodeClipper diode;             // real Shockley diode-pair distortion clipper
    rbshared::Oversampler4x osDiode;   // 4x OS around the only nonlinearity (the diode)
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void updateFilters()
    {
        inputHp.setHighPass(sampleRate, 38.0f, 0.70f);
        inputLp.setLowPass(sampleRate, 15000.0f, 0.64f);
        // distortion voicing: tighten lows + de-fizz as it's pushed (the JC
        // diode distortion is gritty/buzzy but still solid-state-clean otherwise)
        distPre.setHighPass(sampleRate, 90.0f + 170.0f * distortion, 0.70f);
        distPost.setLowPass(sampleRate, 7400.0f - 1100.0f * distortion, 0.66f);
        // passive tone stack — ranges from the spec (TREBLE 17dB@10k, MIDDLE
        // 13dB@350Hz, BASS 14dB@50Hz) + the fixed BRIGHT (5dB@10kHz) shelf.
        // CIRCUIT-REAL tone stack (service manual): TREBLE 250k @10kHz ±17dB, MIDDLE 10k @350Hz ±13dB,
        // BASS 250k @60Hz ±14dB. The TREBLE is a 10kHz shelf (not 2.6k) — the glassy JC top is real
        // 10kHz air + the speaker rolloff below, NOT a wide presence boost.
        toneBass.setLowShelf(sampleRate, 60.0f, 0.72f, eqDb(bass, 14.0f));
        toneMid.setPeaking(sampleRate, 350.0f, 0.72f, eqDb(mid, 13.0f));
        toneTreble.setHighShelf(sampleRate, 10000.0f, 0.74f, eqDb(treble, 17.0f));
        // BRIGHT: real = a +5dB @10kHz treble-bleed shelf across the 1M Volume pot. Baked on.
        brightShelf.setHighShelf(sampleRate, 10000.0f, 0.80f, 5.0f);
        // solid-state combo speaker (2x12): gentle thump + top roll-off (opened, miked-cab top)
        speakerThump.setPeaking(sampleRate, 105.0f, 0.85f, 1.0f);
        // The Roland JC speaker/voice is BRIGHT (the amp-only ref top extends to 12.5k). Presence shelf
        // tuned to the ref — stronger when CLEAN (no diode harmonics to brighten it) than when distorted.
        // Presence/brilliance to the bright JC reference. Capped at a moderate lift — the clean DI has
        // little real >5k content, so a bigger shelf would just amplify hiss on this quiet solid-state amp.
        speakerBite.setHighShelf(sampleRate, 2300.0f, 0.72f, 8.5f - 4.0f * distortion);
        speakerLp.setLowPass(sampleRate, 13500.0f + 1500.0f * treble - 1500.0f * distortion, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset(); distPre.reset(); distPost.reset();
        toneBass.reset(); toneMid.reset(); toneTreble.reset(); brightShelf.reset();
        speakerLp.reset(); speakerThump.reset(); speakerBite.reset(); dcBlock.reset(); diode.reset(); osDiode.reset();
        spring.clear(); chorus.clear();
        updateFilters();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        spring.setSampleRate(sampleRate);
        chorus.setSampleRate(sampleRate);
        chorus.setRate(speed);
        reset();
    }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx)
        {
            case kVolume:     volume = v; break;
            case kTreble:     treble = v; break;
            case kMiddle:     mid = v; break;
            case kBass:       bass = v; break;
            case kDistortion: distortion = v; break;
            case kReverb:     reverb = v; break;
            case kSpeed:      speed = v; chorus.setRate(speed); break;
            case kDepth:      depth = v; break;
            case kChorus:     chorusMode = v; break;
            case kCabSim:     cabSim = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults() { for (int i = 0; i < kParamCount; ++i) setParam(i, kJC120Def[i]); }

    // Mono preamp -> stereo chorus split. inL/inR summed to the (mono) amp; the
    // chorus opens the L/R image at the output.
    void process(float inL, float inR, float& outL, float& outR)
    {
        float x = 0.5f * (inL + inR);
        x = inputHp.process(x);
        x = inputLp.process(x);

        // DISTORTION — diode clipper. At 0 the amp is dead clean (the JC clean);
        // rising = more drive into a gritty clip, blended back over the clean.
        const float clean = x;
        float d = distPre.process(x);
        // Drive the signal up to diode-conduction voltage, then through the REAL
        // Shockley diode-pair clipper (Newton-solved). Below threshold it passes
        // clean; above, the diodes clamp with their exponential knee = the gritty,
        // odd-harmonic solid-state JC edge (no tanh fit).
        // DISTORTION pot is a reverse-log (C) 10k — most of the gain is in the top of the sweep.
        const float distC = distortion * distortion;
        const float drive = 1.0f + 60.0f * distC;
        // 4x-oversample ONLY the diode clip (the alias source) — the tone stack,
        // reverb and chorus stay at base rate so their delay lines keep timing.
        float ubd[kOS];
        osDiode.upsample(d * drive, ubd);
        for (int k = 0; k < kOS; ++k) ubd[k] = diode.process(ubd[k]);
        d = osDiode.downsample(ubd) * 1.9f;          // diodes clamp ~±0.5V -> makeup ~unity
        d = distPost.process(d);
        // clean->distorted blend with makeup so the distorted level ~tracks clean
        const float w = distortion;
        float y = clean * (1.0f - w) + d * w * (0.82f + 0.14f * (1.0f - w));

        // tone stack + bright
        y = toneBass.process(y);
        y = toneMid.process(y);
        y = toneTreble.process(y);
        y = brightShelf.process(y);
        y = dcBlock.process(y);

        // VOLUME (clean, high-headroom solid-state — no power saturation)
        const float vol = 0.30f + 1.10f * volume;
        y *= vol;

        // solid-state combo fallback speaker (bypassable for external cab/IR)
        const float ampOnly = y;
        float cab = speakerThump.process(ampOnly);
        cab = speakerBite.process(cab);
        cab = speakerLp.process(cab);
        y = ampOnly + cabSim * (cab - ampOnly);

        // loudness normalization: keep multitone RMS ~constant vs Distortion +
        // Volume so the shared kLvl stage stays calibrated.
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 17.0f);
        // The clean JC has little gain while the diode clipper gets much hotter
        // with Distortion, so autoGain boosts the clean end and compensates the
        // drive -> multitone RMS stays ~flat (~-14 dBFS) across the Distortion sweep.
        const float autoGain = std::exp(-3.5f * distortion + 1.1f * distortion * distortion);
        const float level = (3.42f * autoGain) / ((0.55f + 0.95f * volume) * toneEnergy);
        // loudness flattening vs Distortion (clean makeup; ~0 dB at distortion 0.5)
        y *= level * std::pow(10.0f, 0.05f * (-1.979f + 2.727f * distortion + 2.466f * distortion * distortion));

        // spring REVERB (parallel send/return)
        if (reverb > 0.0005f)
            y += spring.process(y) * reverb * 0.6f;

        // STEREO CHORUS / Vibrato — the real 3-position VIB/CHORUS lever:
        // OFF(0) = dry mono; CHORUS(~0.5) = dry + wet spread L/R (the JC
        // shimmer); VIBRATO(1) = full wet pitch mod.
        float wL, wR;
        const bool vibrato = chorusMode > 0.75f;
        const float depthMs = (vibrato ? 5.5f : 3.2f) * depth;   // vibrato runs deeper
        chorus.process(y, depthMs, wL, wR);
        if (chorusMode < 0.25f) {           // OFF — dry both sides
            outL = y; outR = y;
        } else if (chorusMode < 0.75f) {    // CHORUS — dry + wet, opened L/R
            outL = 0.65f * y + 0.55f * wL;
            outR = 0.65f * y + 0.55f * wR;
        } else {                            // VIBRATO — 100% wet
            outL = wL; outR = wR;
        }
    }
};

class JC120Plugin : public Plugin
{
    JC120Core core;
    float params[kParamCount];

    void applyAll() { for (int i = 0; i < kParamCount; ++i) core.setParam(i, params[i]); }

public:
    JC120Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kJC120Def[i];
        core.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "RonaldJC120"; }
    const char* getDescription() const override { return "Ronald JC-120 Jazz Chorus style solid-state amp (stereo chorus)"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1,0,1); }
    int64_t getUniqueId() const override { return d_cconst('J', 'c', '1', '2'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kJC120Names[index];
        parameter.symbol = kJC120Symbols[index];
        parameter.ranges.min = kJC120Min[index];
        parameter.ranges.max = kJC120Max[index];
        parameter.ranges.def = kJC120Def[index];
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < (uint32_t)kParamCount ? params[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount) return;
        params[index] = clamp01(value);
        core.setParam((int)index, params[index]);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        core.setSampleRate((float)newSampleRate);
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
            float oL, oR;
            core.process(3.2f * inL[i], 3.2f * inR[i], oL, oR);
            outL[i] = rbAmpLvl(0.205f * oL);
            outR[i] = rbAmpLvl(0.205f * oR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JC120Plugin)
};

Plugin* createPlugin() { return new JC120Plugin(); }

END_NAMESPACE_DISTRHO
