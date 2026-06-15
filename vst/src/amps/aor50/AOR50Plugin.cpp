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

    // derived
    float chS = 1.0f;        // 0 = Channel One .. 1 = AOR
    float m = 0.7f;          // effective drive/voicing morph
    float activeMaster = 0.5f;
    float activeBright = 0.0f;
    float crunchA = 0.0f, leadA = 0.0f, deepA = 0.0f, midB = 0.0f;

    Biquad inputHp, inputLp, brightShelf;
    Biquad ch1Body, aorTight, aorBite;
    Biquad interHp, interLp;
    Biquad toneBass, toneMid, toneTreble;
    Biquad phaseHp, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizzNotch, speakerLp;
    DcBlock dcBlock;
    float sag = 0.0f;

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

        // British TMB tone stack + Pull-Deep (low boost) + Pull-Boost (mid lift).
        toneBass.setLowShelf(sampleRate, 110.0f + 40.0f * bass, 0.72f,
                             eqDb(bass, 7.0f) - 1.4f * leadA + 5.5f * deepA);
        toneMid.setPeaking(sampleRate, 560.0f + 300.0f * mid, 0.70f,
                           eqDb(mid, 7.2f) + 1.2f * crunchA + 6.0f * midB);
        toneTreble.setHighShelf(sampleRate, 1950.0f + 1050.0f * treble, 0.74f,
                                eqDb(treble, 7.0f) + 1.0f * leadA);

        phaseHp.setHighPass(sampleRate, 74.0f + 30.0f * leadA, 0.72f);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1400.0f * treble + 700.0f * presence - 2000.0f * leadA, 0.65f);
        presenceShelf.setHighShelf(sampleRate, 2650.0f + 850.0f * presence, 0.78f,
                                   -4.0f + 8.4f * presence + 1.2f * treble);

        speakerHp.setHighPass(sampleRate, 78.0f + 8.0f * leadA, 0.72f);
        speakerThump.setPeaking(sampleRate, 124.0f, 0.86f, 0.8f + 2.2f * bass + 1.8f * deepA);
        speakerLowMid.setPeaking(sampleRate, 420.0f + 150.0f * mid, 0.76f, 0.6f + 2.4f * mid);
        speakerBite.setPeaking(sampleRate, 2750.0f + 600.0f * treble, 0.78f,
                               2.2f + 2.3f * treble + 1.8f * presence + 0.8f * leadA - 0.5f * leadA);
        speakerFizzNotch.setHighShelf(sampleRate, 4700.0f, 0.70f, 9.5f + 2.0f * treble + 2.0f * presence - 4.5f * leadA);
        speakerLp.setLowPass(sampleRate, 16000.0f + 2000.0f * treble + 850.0f * presence - 3500.0f * leadA, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset(); brightShelf.reset();
        ch1Body.reset(); aorTight.reset(); aorBite.reset();
        interHp.reset(); interLp.reset();
        toneBass.reset(); toneMid.reset(); toneTreble.reset();
        phaseHp.reset(); phaseLp.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset();
        speakerBite.reset(); speakerFizzNotch.reset(); speakerLp.reset();
        dcBlock.reset();
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

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = brightShelf.process(x);
        x = softClip(x * (1.05f + 0.16f * m + 0.14f * leadMix));

        // Channel One voicing: V1A/V1B, fuller low-mid, moderate gain.
        float ch1 = ch1Body.process(x);
        ch1 = 0.55f * ch1 + 0.45f * asymTube(ch1, 0.88f + 1.30f * m, 0.006f);

        // AOR voicing: tightened lows + bite, cascaded high-gain triodes.
        float aor = aorTight.process(x);
        aor = aorBite.process(aor);
        aor = asymTube(aor, 1.70f + 4.60f * m, 0.016f + 0.012f * presence);
        aor = asymTube(aor, 1.20f + 3.40f * m, -0.012f - 0.008f * m);
        aor = 0.72f * aor + 0.28f * softClip(aor * (1.9f + 1.8f * m));

        // Crunch is the in-between (Channel One pushed / AOR backed off).
        float crunch = ch1Body.process(x);
        crunch = asymTube(crunch, 1.25f + 2.85f * m, 0.010f + 0.010f * m);

        float y = ch1 * cleanMix + crunch * crunchMix + aor * leadMix;
        y = interHp.process(y);
        y = interLp.process(y);

        const float extraCascade = smoothstepRange(0.46f, 0.90f, m);
        const float cascaded = asymTube(y, 1.02f + 2.05f * m + 2.0f * leadMix, -0.006f - 0.010f * leadMix);
        y = y * (1.0f - 0.54f * extraCascade) + cascaded * (0.54f * extraCascade);

        y = toneBass.process(y);
        y = toneMid.process(y);
        y = toneTreble.process(y);
        y = phaseHp.process(y);
        y = phaseLp.process(y);

        // EL34 power amp + silicon rectifier (tight: only modest sag).
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0050f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.140f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.30f + 0.78f * m + 0.55f * leadMix));

        const float powerDrive = (0.94f + 1.50f * m + 1.95f * leadMix) * sagDrop;
        y = asymTube(y, powerDrive, 0.005f + 0.012f * (presence - bass));
        y = 0.84f * y + 0.16f * softClip(y * (1.7f + 1.25f * leadMix));
        y *= 0.98f - 0.05f * sag;

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizzNotch.process(y);
        y = speakerLp.process(y);

        // Loudness normalization across the Gain (channel) sweep: the clean
        // Channel One barely saturates, so cleanMakeup lifts it to keep the RS
        // sweep within a couple dB and the shared kLvl stage calibrated.
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.011f * std::fabs((presence - 0.5f) * 16.0f);
        const float cleanMakeup = 1.0f + 7.5f * std::exp(-m / 0.24f);
        const float level = (0.74f + 0.12f * (1.0f - m)) * cleanMakeup /
            ((1.0f + 0.30f * m + 0.60f * leadMix) * toneEnergy);

        // Master volume (selected channel). Centred at 0.5 = unity so RS songs
        // that leave it at the musical default keep the calibrated loudness.
        const float masterGain = 0.55f + 0.90f * activeMaster;

        return softClip(y * level * masterGain) * 0.97f;
    }
};

class AOR50Plugin : public Plugin
{
    AOR50Core left;
    AOR50Core right;
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
    AOR50Plugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kAOR50Def[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
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
            outL[i] = rbAmpLvl(0.600f * left.process(3.2f * inL[i]));
            outR[i] = rbAmpLvl(0.600f * right.process(3.2f * inR[i]));
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AOR50Plugin)
};

Plugin* createPlugin()
{
    return new AOR50Plugin();
}

END_NAMESPACE_DISTRHO
