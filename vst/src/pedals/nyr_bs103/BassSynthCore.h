#ifndef NYR_BS103_CORE_H
#define NYR_BS103_CORE_H

// Pure-DSP core for NYR BS103 (no DPF dependency) so it can be unit-tested
// offline. See README.md for the signal flow.
//
// Pitch tracking uses AUTOCORRELATION (a YIN-style cumulative-mean-normalised
// difference function) on a decimated copy of the input — far more robust on a
// real bass (harmonics, pick transients, fret noise) than zero-crossing, which
// is what was detuning notes. Synthesis: main oscillator + Voice (detuned
// unison + octave) + square sub-octave → resonant 4-pole LP with a per-note
// filter envelope and an optional LFO → Mod chorus → dry/synth mix.
#include "BassSynthParams.h"
#include <cmath>

namespace bs103 {

static constexpr float kPi = 3.14159265359f;
static constexpr float kTwoPi = 6.28318530718f;

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static inline float clampFreq(float hz, float sr)
{
    const float nyq = sr * 0.45f;
    return std::fmax(18.0f, std::fmin(hz, nyq));
}

static inline float softClipT(float x) { return std::tanh(x); }

static inline float onePoleCoeffMs(float ms, float sr)
{
    ms = std::fmax(0.02f, ms);
    return 1.0f - std::exp(-1.0f / (0.001f * ms * sr));
}

// PolyBLEP residual for band-limiting saw/square discontinuities.
static inline float polyBlep(float t, float dt)
{
    if (dt <= 0.0f) return 0.0f;
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// State-variable filter (TPT) — one 2-pole section. Two cascaded = 4-pole.
class Svf
{
    float ic1eq = 0.0f, ic2eq = 0.0f;
public:
    void reset() { ic1eq = ic2eq = 0.0f; }
    float lowPass(float x, float sr, float hz, float q)
    {
        hz = clampFreq(hz, sr);
        q = std::fmax(0.5f, std::fmin(q, 16.0f));
        const float g = std::tan(kPi * hz / sr);
        const float r = 1.0f / (2.0f * q);
        const float h = 1.0f / (1.0f + 2.0f * r * g + g * g);
        const float v3 = x - ic2eq;
        const float v1 = h * (g * v3 + ic1eq);
        const float v2 = ic2eq + g * v1;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        return v2;
    }
};

class BassSynthCore
{
    static constexpr int   kDsBuf = 768;     // decimated ring buffer
    static constexpr float kFMin = 28.0f;    // ~ low B
    static constexpr float kFMax = 460.0f;
    static constexpr float kGateClose = 0.008f;

    float sampleRate = 48000.0f;

    // knob state (0..1)
    float mix = kBassSynthDef[kMix];
    float subLevel = kBassSynthDef[kSub];
    float cutoff = kBassSynthDef[kCutoff];
    float resonance = kBassSynthDef[kResonance];
    float envelope = kBassSynthDef[kEnvelope];
    float shape = kBassSynthDef[kShape];
    float voiceAmt = kBassSynthDef[kVoice];
    float modAmt = kBassSynthDef[kMod];
    float level = kBassSynthDef[kLevel];

    // input conditioning + envelopes
    float dcz = 0.0f;
    float ampEnv = 0.0f, ampAtkA = 0.0f, ampRelA = 0.0f;     // VCA
    float gateEnv = 0.0f, gEnvAtkA = 0.0f, gEnvRelA = 0.0f;  // fast detector
    float gateGain = 0.0f, gateAtkA = 0.0f, gateRelA = 0.0f; // gate ramp
    bool  prevGateOpen = false;

    // pitch tracker (autocorrelation on a decimated copy)
    int   decim = 8;            // decimation factor (sr -> ~6 kHz)
    float fsd = 6000.0f;        // decimated sample rate
    float aa1 = 0.0f, aa2 = 0.0f, aaCoeff = 0.0f;   // anti-alias LP pre-decimation
    int   decCount = 0;
    float ds[kDsBuf];           // decimated ring buffer
    int   dsW = 0;
    int   hopCount = 0, hop = 48;
    int   tauMin = 13, tauMax = 214, win = 214;
    float lockedFreq = 80.0f, freq = 80.0f, glideA = 0.0f;
    bool  haveLock = false;

    // oscillators
    float phase = 0.0f, subPhase = 0.0f, vDetPhase = 0.0f, vOctPhase = 0.0f;
    float subLp1 = 0.0f, subLp2 = 0.0f, cSubLp = 0.0f;
    int   shapeSel = 1;

    // filter + per-note filter envelope
    Svf f1, f2;
    float filtEnvVal = 0.0f, feDecay = 0.0f;

    // modulation: filter LFO + chorus
    float lfoPhase = 0.0f, lfoInc = 0.0f;
    static constexpr int kChorusBuf = 2048;
    float chBuf[kChorusBuf];
    int   chW = 0;

    void updateRates()
    {
        ampAtkA = onePoleCoeffMs(2.0f, sampleRate);
        ampRelA = onePoleCoeffMs(180.0f, sampleRate);
        gEnvAtkA = onePoleCoeffMs(1.0f, sampleRate);
        gEnvRelA = onePoleCoeffMs(45.0f, sampleRate);
        gateAtkA = onePoleCoeffMs(4.0f, sampleRate);
        gateRelA = onePoleCoeffMs(35.0f, sampleRate);
        glideA = onePoleCoeffMs(7.0f, sampleRate);
        cSubLp = 1.0f - std::exp(-kTwoPi * 320.0f / sampleRate);

        // Decimate to ~6 kHz for the pitch detector (cheap + low aliasing for
        // bass). Anti-alias LP just under the decimated Nyquist.
        decim = (int)std::lround(sampleRate / 6000.0f);
        if (decim < 1) decim = 1;
        fsd = sampleRate / (float)decim;
        aaCoeff = 1.0f - std::exp(-kTwoPi * (0.40f * fsd) / sampleRate);
        tauMin = (int)(fsd / kFMax); if (tauMin < 4) tauMin = 4;
        tauMax = (int)(fsd / kFMin) + 1;
        if (tauMax > kDsBuf / 2 - 2) tauMax = kDsBuf / 2 - 2;
        win = tauMax;                       // analysis window ~ one low-note period
        hop = (int)(fsd * 0.008f);          // ~8 ms between pitch estimates
        if (hop < 16) hop = 16;

        const float decMs = 60.0f + 1140.0f * (envelope * envelope);
        feDecay = std::exp(-1.0f / (0.001f * decMs * sampleRate));

        lfoInc = 0.55f / sampleRate;        // ~0.55 Hz chorus/filter LFO
    }

    void updateShape() { shapeSel = shape < 0.34f ? 0 : (shape < 0.67f ? 1 : 2); }

    float oscShape(float ph, float dt) const
    {
        if (shapeSel == 0) return 2.0f * std::fabs(2.0f * ph - 1.0f) - 1.0f;   // tri
        if (shapeSel == 1) return (2.0f * ph - 1.0f) - polyBlep(ph, dt);        // saw
        float v = ph < 0.5f ? 1.0f : -1.0f;                                    // square
        v += polyBlep(ph, dt);
        float ph2 = ph + 0.5f; if (ph2 >= 1.0f) ph2 -= 1.0f;
        v -= polyBlep(ph2, dt);
        return v;
    }

    // YIN autocorrelation pitch estimate over the decimated buffer (standard
    // cumulative-mean-normalised difference function + absolute threshold +
    // descend-to-local-min + parabolic interpolation). Returns f0 (Hz) and sets
    // `conf` (the dp value at the chosen lag; lower = more clearly periodic).
    float estimatePitch(float& conf)
    {
        const int N = win + tauMax;
        float seg[kDsBuf];
        int start = dsW - N; while (start < 0) start += kDsBuf;
        for (int k = 0; k < N; ++k) seg[k] = ds[(start + k) % kDsBuf];

        float dp[kDsBuf / 2];
        dp[0] = 1.0f;
        float runSum = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float d = 0.0f;
            for (int j = 0; j < win; ++j) { const float diff = seg[j] - seg[j + tau]; d += diff * diff; }
            runSum += d;
            dp[tau] = (runSum > 1e-9f) ? d * (float)tau / runSum : 1.0f;
        }

        const float TH = 0.20f;
        int tau = -1;
        for (int t = tauMin; t <= tauMax; ++t)
        {
            if (dp[t] < TH)
            {
                while (t + 1 <= tauMax && dp[t + 1] < dp[t]) ++t;  // descend to the dip
                tau = t;
                break;
            }
        }
        if (tau < 0)
        {
            float best = 1e9f; int bt = tauMin;
            for (int t = tauMin; t <= tauMax; ++t) if (dp[t] < best) { best = dp[t]; bt = t; }
            tau = bt;
        }

        conf = dp[tau];
        float period = (float)tau;
        if (tau > tauMin && tau < tauMax)
        {
            const float a = dp[tau - 1], b = dp[tau], c = dp[tau + 1];
            const float den = a - 2.0f * b + c;
            if (std::fabs(den) > 1e-9f) period += 0.5f * (a - c) / den;
        }
        return fsd / std::fmax(1.0f, period);
    }

public:
    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }

    void reset()
    {
        dcz = ampEnv = gateEnv = gateGain = 0.0f;
        prevGateOpen = false;
        aa1 = aa2 = 0.0f; decCount = 0; dsW = 0; hopCount = 0;
        for (int i = 0; i < kDsBuf; ++i) ds[i] = 0.0f;
        lockedFreq = freq = 80.0f; haveLock = false;
        phase = subPhase = vDetPhase = vOctPhase = 0.0f;
        subLp1 = subLp2 = 0.0f;
        f1.reset(); f2.reset();
        filtEnvVal = 0.0f;
        lfoPhase = 0.0f;
        for (int i = 0; i < kChorusBuf; ++i) chBuf[i] = 0.0f;
        chW = 0;
        updateRates(); updateShape();
    }

    void setMix(float v)       { mix = clamp01(v); }
    void setSub(float v)       { subLevel = clamp01(v); }
    void setCutoff(float v)    { cutoff = clamp01(v); }
    void setResonance(float v) { resonance = clamp01(v); }
    void setEnvelope(float v)  { envelope = clamp01(v); updateRates(); }
    void setShape(float v)     { shape = clamp01(v); updateShape(); }
    void setVoice(float v)     { voiceAmt = clamp01(v); }
    void setMod(float v)       { modAmt = clamp01(v); }
    void setLevel(float v)     { level = clamp01(v); }

    float debugFreq() const { return freq; }
    bool  debugNoteActive() const { return prevGateOpen; }

    float process(float in)
    {
        // --- input conditioning ---
        dcz += 0.0008f * (in - dcz);
        const float x = in - dcz;
        const float a = std::fabs(x);
        ampEnv += (a > ampEnv ? ampAtkA : ampRelA) * (a - ampEnv);
        gateEnv += (a > gateEnv ? gEnvAtkA : gEnvRelA) * (a - gateEnv);

        // --- decimate into the pitch buffer + run YIN every hop ---
        aa1 += aaCoeff * (x - aa1);
        aa2 += aaCoeff * (aa1 - aa2);
        if (++decCount >= decim)
        {
            decCount = 0;
            ds[dsW] = aa2; dsW = (dsW + 1) % kDsBuf;
            if (++hopCount >= hop)
            {
                hopCount = 0;
                if (gateEnv > kGateClose)
                {
                    float conf = 1.0f;
                    float f0 = estimatePitch(conf);
                    if (conf < 0.30f && f0 >= kFMin && f0 <= kFMax)
                    {
                        lockedFreq = f0;        // YIN is robust → accept directly
                        haveLock = true;
                    }
                }
            }
        }

        // --- note gate + onset (retrigger filter env on a fresh note) ---
        const bool gateOpen = gateEnv > kGateClose;
        if (gateOpen && !prevGateOpen) filtEnvVal = 1.0f;
        prevGateOpen = gateOpen;
        const float gateTarget = gateOpen ? 1.0f : 0.0f;
        gateGain += (gateTarget > gateGain ? gateAtkA : gateRelA) * (gateTarget - gateGain);

        // --- glide the oscillator to the locked pitch ---
        freq += glideA * (lockedFreq - freq);
        const float dt = freq / sampleRate;

        // --- oscillators: main + Voice (detuned unison + octave) ---
        phase += dt; if (phase >= 1.0f) phase -= 1.0f;
        float voice = oscShape(phase, dt);

        const float dtDet = freq * 1.0023f / sampleRate;         // +~4 cents (subtle)
        vDetPhase += dtDet; if (vDetPhase >= 1.0f) vDetPhase -= 1.0f;
        const float dtOct = 2.0f * dt;                            // octave up
        vOctPhase += dtOct; if (vOctPhase >= 1.0f) vOctPhase -= 1.0f;
        const float voiceSig = oscShape(vDetPhase, dtDet) * 0.6f + oscShape(vOctPhase, dtOct) * 0.35f;
        voice = voice + voiceAmt * voiceSig;
        voice *= 1.0f / (1.0f + 0.55f * voiceAmt);               // keep level even

        // --- square sub-octave ---
        const float dts = 0.5f * dt;
        subPhase += dts; if (subPhase >= 1.0f) subPhase -= 1.0f;
        float subSq = subPhase < 0.5f ? 1.0f : -1.0f;
        subSq += polyBlep(subPhase, dts);
        float sp2 = subPhase + 0.5f; if (sp2 >= 1.0f) sp2 -= 1.0f;
        subSq -= polyBlep(sp2, dts);
        subLp1 += cSubLp * (subSq - subLp1);
        subLp2 += cSubLp * (subLp1 - subLp2);
        voice += subLp2 * (subLevel * 1.4f);

        // --- modulation LFO (filter sweep + chorus) ---
        lfoPhase += lfoInc; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        const float lfo = std::sin(kTwoPi * lfoPhase);

        // --- per-note filter envelope + LFO sweep the cutoff ---
        filtEnvVal *= feDecay;
        // Envelope knob scales BOTH the sweep depth and (via feDecay) its speed,
        // so at low settings the base Cutoff dominates instead of every note
        // blasting the filter wide open (which made Cutoff feel like it did
        // nothing). 0 = no sweep → Cutoff has full authority.
        const float envOctaves = 3.0f * envelope;
        float baseHz = 110.0f * std::pow(10.0f, 1.45f * cutoff);          // ~110..3100 Hz
        baseHz *= (1.0f + 0.25f * modAmt * lfo);                          // filter LFO
        const float fHz = baseHz * std::pow(2.0f, envOctaves * filtEnvVal);
        const float q = 0.62f + 7.5f * (resonance * resonance);
        float fy = f1.lowPass(voice, sampleRate, fHz, q);
        fy = f2.lowPass(fy, sampleRate, fHz, std::fmax(0.6f, q * 0.55f));

        // --- VCA: dynamics + gate ---
        float synth = fy * ampEnv * gateGain * 3.0f;

        // --- chorus (Mod): modulated short delay blended back in ---
        if (modAmt > 0.001f)
        {
            chBuf[chW] = synth;
            const float baseD = 0.012f * sampleRate;                     // ~12 ms
            const float depthD = 0.004f * sampleRate;                    // ~4 ms
            float dly = baseD + depthD * (0.5f + 0.5f * lfo);
            float rp = (float)chW - dly;
            while (rp < 0.0f) rp += kChorusBuf;
            const int i0 = (int)rp; const float fr = rp - (float)i0;
            const float s0 = chBuf[i0 % kChorusBuf];
            const float s1 = chBuf[(i0 + 1) % kChorusBuf];
            const float wet = s0 + (s1 - s0) * fr;
            synth += (wet - synth) * (0.5f * modAmt);
            chW = (chW + 1) % kChorusBuf;
        }

        // --- dry/synth mix, output level, safety clip ---
        // Equal-power crossfade with the dry boosted to roughly match the synth
        // level, so Mix changes the dry/synth TIMBRE at ~constant loudness rather
        // than just the volume (a volume-only Mix gets cancelled by the chain
        // leveler, which is why it "did nothing").
        const float m = mix * 1.5707963f;          // 0..π/2
        const float out = (x * 3.0f) * std::cos(m) + synth * std::sin(m);
        const float g = 0.25f + 1.75f * level;
        return softClipT(out * g) * 0.98f;
    }
};

} // namespace bs103

#endif // NYR_BS103_CORE_H
