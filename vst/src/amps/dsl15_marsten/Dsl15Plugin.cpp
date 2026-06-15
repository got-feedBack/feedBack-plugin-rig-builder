/*
 * MARSTEN DSL15 - Marshall DSL15H (15 W head) for the game's Amp_MarshallDSL15H.
 * Parody brand "Marsten" (matches the GM-2 / UV-1 Marshall-copy pedals and the
 * larger Marsten DSL100); the in-app face must never read "Marshall".
 *
 * Local reference (full circuit, component-by-component):
 *   amps/Marshall DSL15/Marshall_DSL15_60_02_v04.pdf   ("15W MAIN BOARD" iss4)
 *     sheet 1: 4x ECC83 preamp (V1A/B V2A/B V3A/B), Classic/Ultra channel relays
 *              (RL1), shared Marshall TMB tone stack (VR5 B200K TREBLE, VR6 B20K
 *              MIDDLE, VR7 BASS), per-channel GAIN/VOLUME pots, op-amp reverb
 *              (combo only - the head has none).
 *     sheet 2: 2x 6V6 power amp (V5A/V6A) into OTX TXOP-91001 (~15 W), DEEP switch
 *              (SW1B + R94 100K / C68 100n / C69 22n low-end resonance), Presence
 *              (VR8 C10K NFB), Output Power switch (full / ~7.5 W half).
 *
 * Simplified DSL panel (vs. the DSL100): NO Resonance pot, NO reverb on the head,
 * single Master (folded into the active channel Volume). Two footswitchable
 * channels off the shared preamp:
 *   CLASSIC GAIN : Clean -> Crunch     (Gain + Volume)
 *   ULTRA GAIN   : high-gain OD         (Gain + Volume)
 *   shared EQ    : Bass / Middle / Treble + Tone Shift (mid-scoop switch)
 *   DEEP switch  : low-frequency power-amp resonance boost
 *   power amp    : Presence (high NFB)
 *
 * the game: RS Gain -> ULTRA GAIN (Channel pinned Ultra). Bass/Mid/Treble ->
 * tone stack, Pres -> Presence. See rs_knob_to_vst_param.json.
 */
#include "DistrhoPlugin.hpp"
#include "Dsl15Params.h"
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

static inline float asymTube(float x, float drive, float bias)
{
    const float pushed = x * drive + bias;
    const float y = std::tanh(pushed);
    const float correction = std::tanh(bias);
    return (y - correction) / (1.0f - 0.30f * std::fabs(correction));
}

static inline float eqDb(float v, float rangeDb)
{
    return (clamp01(v) - 0.5f) * 2.0f * rangeDb;
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

} // namespace

class Dsl15Core
{
    float sampleRate = 48000.0f;
    // panel params
    float channel      = kDsl15Def[kChannel];
    float classicGain  = kDsl15Def[kClassicGain];
    float classicVol   = kDsl15Def[kClassicVolume];
    float ultraGain    = kDsl15Def[kUltraGain];
    float ultraVol     = kDsl15Def[kUltraVolume];
    float presence     = kDsl15Def[kPresence];
    float bass         = kDsl15Def[kBass];
    float mid          = kDsl15Def[kMiddle];
    float treble       = kDsl15Def[kTreble];
    float deepSw       = kDsl15Def[kDeep];
    float toneShiftSw  = kDsl15Def[kToneShift];

    // derived (recomputed in updateFilters)
    float m = 0.7f;        // channel/drive morph: 0 = Classic clean .. 1 = Ultra max
    float chS = 1.0f;      // smoothed channel position (0 Classic .. 1 Ultra)
    float chVol = 0.5f;    // active channel volume
    float ts = 0.0f;       // effective Tone Shift depth
    float crunchA = 0.0f;  // crunch amount
    float ultraA = 0.0f;   // ultra amount
    float deep = 0.0f;     // Deep switch resonance amount

    Biquad inputHp, inputLp, brightShelf;
    Biquad cleanBody, crunchBody, ultraTight, ultraBite;
    Biquad interHp, interLp;
    Biquad toneBass, toneMid, toneTreble, toneShiftMid, toneShiftBite;
    Biquad phaseHp, phaseLp, presenceShelf, deepShelf, deepPeak;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizzNotch, speakerLp;
    DcBlock dcBlock;
    float sag = 0.0f;

    void updateFilters()
    {
        chS = smoothstep(channel);
        // Channel/drive morph. Classic side sweeps clean->crunch via Classic
        // Gain; Ultra side is the high-gain OD via Ultra Gain. the game drives
        // the Ultra channel (Channel pinned 1), so a low Ultra Gain lands in the
        // hot edge and a high one in the searing DSL15 lead.
        const float classicM = 0.03f + 0.48f * classicGain;   // ~0.03..0.51
        const float ultraM   = 0.54f + 0.42f * ultraGain;     // ~0.54..0.96
        m = clamp01(classicM * (1.0f - chS) + ultraM * chS);
        chVol = classicVol * (1.0f - chS) + ultraVol * chS;

        crunchA = smoothstepRange(0.24f, 0.60f, m);
        ultraA  = smoothstepRange(0.54f, 0.95f, m);
        // DEEP: a real front-panel low-end resonance switch (SW1B + R94/C68/C69),
        // deeper on the hotter Ultra voice. Smoothstep so toggling is gentle.
        deep = clamp01(deepSw);
        // Tone Shift: the DSL mid-scoop switch (deeper on the hotter voice). A
        // hair of low-mid is pulled even part-on for smoothness.
        ts = clamp01(toneShiftSw) * (0.70f + 0.30f * ultraA);

        inputHp.setHighPass(sampleRate, 56.0f + 60.0f * ultraA + 28.0f * (1.0f - bass), 0.70f);
        inputLp.setLowPass(sampleRate, 14600.0f - 3000.0f * ultraA + 1100.0f * treble, 0.64f);
        brightShelf.setHighShelf(sampleRate, 1080.0f + 1150.0f * treble, 0.70f,
                                 -1.8f + 5.0f * treble + 1.6f * presence + 1.0f * crunchA);
        cleanBody.setPeaking(sampleRate, 420.0f + 130.0f * mid, 0.76f,
                             -0.8f + 2.6f * mid + 1.4f * bass);
        crunchBody.setPeaking(sampleRate, 780.0f + 220.0f * mid, 0.82f,
                              -1.6f + 4.8f * mid + 1.9f * crunchA);
        // 6V6 little-DSL: a bit tighter / less low-shelf scoop than the EL34 DSL100.
        ultraTight.setLowShelf(sampleRate, 150.0f + 30.0f * bass, 0.76f,
                               -3.4f * ultraA + 3.2f * bass + 1.2f * deep);
        ultraBite.setPeaking(sampleRate, 1900.0f + 620.0f * treble, 0.82f,
                             0.4f + 3.4f * treble + 2.2f * ultraA + 1.0f * presence);
        interHp.setHighPass(sampleRate, 72.0f + 88.0f * ultraA + 34.0f * (1.0f - bass), 0.71f);
        interLp.setLowPass(sampleRate, 9500.0f + 1200.0f * treble - 1700.0f * ultraA, 0.64f);

        // Marshall TMB tone stack (VR5/VR6/VR7) + Tone Shift mid-scoop.
        toneBass.setLowShelf(sampleRate, 120.0f + 42.0f * bass, 0.72f,
                             eqDb(bass, 7.0f) - 1.4f * ultraA + 2.0f * deep);
        toneMid.setPeaking(sampleRate, 620.0f + 310.0f * mid, 0.70f + 0.28f * ts,
                           eqDb(mid, 7.4f) + 1.2f * crunchA - 5.8f * ts);
        toneTreble.setHighShelf(sampleRate, 1950.0f + 1050.0f * treble, 0.74f,
                                eqDb(treble, 7.2f) + 1.0f * ultraA);
        toneShiftMid.setPeaking(sampleRate, 830.0f + 180.0f * treble, 1.08f, -7.0f * ts);
        toneShiftBite.setPeaking(sampleRate, 2600.0f + 530.0f * treble, 0.82f,
                                 2.4f * ts + 0.6f * ultraA);

        // Power-amp NFB: Presence (high) + Deep (low resonance) + speaker.
        phaseHp.setHighPass(sampleRate, 78.0f + 32.0f * ultraA, 0.72f);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1400.0f * treble + 700.0f * presence - 2000.0f * ultraA, 0.65f);
        presenceShelf.setHighShelf(sampleRate, 2750.0f + 850.0f * presence, 0.78f,
                                   -4.2f + 8.7f * presence + 1.3f * treble);
        // DEEP switch: a fixed low-end resonance bump when engaged.
        deepShelf.setLowShelf(sampleRate, 98.0f, 0.78f,
                              -0.4f + 6.4f * deep + 1.4f * ultraA);
        deepPeak.setPeaking(sampleRate, 116.0f, 0.92f,
                            0.4f + 4.4f * deep + 1.2f * bass);

        // 6V6 + 1x12 / 2x12 voicing (a touch boxier / less scooped than the 4x12).
        speakerHp.setHighPass(sampleRate, 80.0f + 10.0f * ultraA, 0.72f);
        speakerThump.setPeaking(sampleRate, 128.0f, 0.88f,
                                0.8f + 2.3f * bass + 2.0f * deep);
        speakerLowMid.setPeaking(sampleRate, 430.0f + 155.0f * mid, 0.76f,
                                 0.6f + 2.5f * mid - 2.2f * ts);
        speakerBite.setPeaking(sampleRate, 2900.0f + 620.0f * treble, 0.78f,
                               2.2f + 2.4f * treble + 1.9f * presence + 0.3f * ultraA);
        speakerFizzNotch.setHighShelf(sampleRate, 4700.0f, 0.70f,
                                      9.5f + 2.0f * treble + 2.0f * presence - 4.5f * ultraA);
        speakerLp.setLowPass(sampleRate, 14500.0f + 2050.0f * treble + 850.0f * presence - 3500.0f * ultraA, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset(); brightShelf.reset();
        cleanBody.reset(); crunchBody.reset(); ultraTight.reset(); ultraBite.reset();
        interHp.reset(); interLp.reset();
        toneBass.reset(); toneMid.reset(); toneTreble.reset(); toneShiftMid.reset(); toneShiftBite.reset();
        phaseHp.reset(); phaseLp.reset(); presenceShelf.reset(); deepShelf.reset(); deepPeak.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset();
        speakerFizzNotch.reset(); speakerLp.reset(); dcBlock.reset();
        sag = 0.0f;
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
            case kChannel:       channel = v; break;
            case kClassicGain:   classicGain = v; break;
            case kClassicVolume: classicVol = v; break;
            case kUltraGain:     ultraGain = v; break;
            case kUltraVolume:   ultraVol = v; break;
            case kPresence:      presence = v; break;
            case kBass:          bass = v; break;
            case kMiddle:        mid = v; break;
            case kTreble:        treble = v; break;
            case kDeep:          deepSw = v; break;
            case kToneShift:     toneShiftSw = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults()
    {
        for (int i = 0; i < kParamCount; ++i)
            setParam(i, kDsl15Def[i]);
    }

    float process(float in)
    {
        const float cleanW = 1.0f - smoothstepRange(0.20f, 0.48f, m);
        const float ultraW = smoothstepRange(0.56f, 0.95f, m);
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
        x = softClip(x * (1.05f + 0.18f * m + 0.15f * ultraMix));

        float clean = cleanBody.process(x);
        clean = 0.58f * clean + 0.42f * asymTube(clean, 0.86f + 1.05f * m, 0.004f);

        float crunch = crunchBody.process(x);
        crunch = asymTube(crunch, 1.28f + 3.05f * m, 0.012f + 0.010f * m);
        crunch = 0.78f * crunch + 0.22f * softClip(crunch * (1.7f + 1.0f * m));

        float ultra = ultraTight.process(x);
        ultra = ultraBite.process(ultra);
        ultra = asymTube(ultra, 1.85f + 5.10f * m, 0.018f + 0.012f * presence);
        ultra = asymTube(ultra, 1.25f + 3.90f * m, -0.014f - 0.008f * m);
        ultra = 0.70f * ultra + 0.30f * softClip(ultra * (2.0f + 1.9f * m));

        float y = clean * cleanMix + crunch * crunchMix + ultra * ultraMix;
        y = interHp.process(y);
        y = interLp.process(y);

        const float extraCascade = smoothstepRange(0.46f, 0.90f, m);
        const float cascaded = asymTube(y, 1.02f + 2.15f * m + 2.15f * ultraMix,
                                        -0.006f - 0.010f * ultraMix);
        y = y * (1.0f - 0.56f * extraCascade) + cascaded * (0.56f * extraCascade);

        y = toneBass.process(y);
        y = toneMid.process(y);
        y = toneTreble.process(y);
        y = toneShiftMid.process(y);
        y = toneShiftBite.process(y);
        y = phaseHp.process(y);
        y = phaseLp.process(y);

        // Channel volume sets how hard the preamp drives the power amp.
        const float chDrive = 0.66f + 0.78f * chVol;
        y *= chDrive;

        // 2x 6V6 power amp (~15 W) + sag. Smaller iron than the DSL100's EL34
        // quartet, so it sags earlier and breaks up a hair sooner.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0042f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.115f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.50f + 1.15f * m + 0.90f * ultraMix));

        const float powerDrive = (0.98f + 1.65f * m + 2.00f * ultraMix) * sagDrop;
        y = asymTube(y, powerDrive, 0.004f + 0.014f * (presence - bass) + 0.008f * deep);
        y = 0.80f * y + 0.20f * softClip(y * (1.85f + 1.30f * ultraMix));
        y *= 0.98f - 0.08f * sag;

        y = presenceShelf.process(y);
        y = deepShelf.process(y);
        y = deepPeak.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizzNotch.process(y);
        y = speakerLp.process(y);

        // Loudness normalization (keeps multitone RMS ~constant across the gain
        // range so the shared kLvl output stage stays calibrated) + channel
        // volume trim.
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.011f * std::fabs((presence - 0.5f) * 16.0f);
        // The clean / low-gain region barely saturates, so without help it sits
        // well below the cranked Ultra voice. cleanMakeup lifts it so the whole
        // RS Gain sweep stays within a couple dB (one kLvl calibration fits all).
        const float cleanMakeup = 1.0f + 5.6f * std::exp(-m / 0.24f);
        const float level = (0.66f + 0.10f * (1.0f - m) + 0.22f * ultraMix) * cleanMakeup /
            ((1.0f + 0.32f * m + 0.50f * ultraMix) * toneEnergy * chDrive);

        return softClip(y * level) * 0.97f;
    }
};

class Dsl15Plugin : public Plugin
{
    Dsl15Core left;
    Dsl15Core right;
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
    Dsl15Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kDsl15Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MarstenDSL15"; }
    const char* getDescription() const override { return "Marsten DSL15 style amp (15W, 2 channels)"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('D', 's', '1', '5'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kDsl15Names[index];
        parameter.symbol = kDsl15Symbols[index];
        parameter.ranges.min = kDsl15Min[index];
        parameter.ranges.max = kDsl15Max[index];
        parameter.ranges.def = kDsl15Def[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dsl15Plugin)
};

Plugin* createPlugin()
{
    return new Dsl15Plugin();
}

END_NAMESPACE_DISTRHO
