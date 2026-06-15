/*
 * ENGEL FIREBALL - ENGL Fireball 100 (EN-50) for the game's Amp_EN50. Parody
 * brand "Engel" (ENGL -> Engel); the in-app face must never read "ENGL".
 *
 * Local reference (modelled block-by-block):
 *   amps/ENGL Fireball (EN-50)/engl-fireball-amplifier-schematic_new.pdf
 *       ("ENGL Gerätebau GmbH 625")
 *   amps/ENGL Fireball (EN-50)/1.jpg, 2.jpg   (the front panel, for the canvas)
 *
 * A 2-channel high-gain modern metal head (~100W). Per the schematic: V1A input
 * stage (Gain pot + Bright switch), V1B/V2A/V2B cascaded gain with the Ultra
 * (RE1A/RE1B) relay selecting the CLEAN vs the high-gain LEAD path, a passive
 * TMB tone stack (Treble 470k / Bass 1MA / Middle 20kB; caps 220nF/1nF/15nF/22nF),
 * V3A/V3B post-stack stages -> Master A / Master B, V4 phase inverter, tube power
 * amp (~100W) + Presence (250kA/2k) NFB into the 16/8R output transformer.
 *
 * Two voices off the shared tone stack: CLEAN (low gain, headroomy) and LEAD (the
 * Ultra cascade — tight, aggressive, scooped-capable, MORE gain than a JCM800),
 * picked by the channel relay. BRIGHT = treble-boost switch, BOTTOM = low-end
 * boost, MID BOOST = a mid push. Dual master.
 *
 * the game: RS Gain -> LEAD GAIN (Channel pinned LEAD); Bass/Mid/Treble -> tone
 * stack; Pres -> Presence. See rs_knob_to_vst_param.json.
 */
#include "DistrhoPlugin.hpp"
#include "EngelFireballParams.h"
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

class EngelFireballCore
{
    float sampleRate = 48000.0f;
    // panel params
    float cleanGain  = kEngelFireballDef[kCleanGain];
    float leadGain   = kEngelFireballDef[kLeadGain];
    float bass       = kEngelFireballDef[kBass];
    float mid        = kEngelFireballDef[kMiddle];
    float treble     = kEngelFireballDef[kTreble];
    float leadVolume = kEngelFireballDef[kLeadVolume];
    float master     = kEngelFireballDef[kMaster];
    float presence   = kEngelFireballDef[kPresence];
    float channel    = kEngelFireballDef[kChannel];
    float brightSw   = kEngelFireballDef[kBright];
    float bottomSw   = kEngelFireballDef[kBottom];
    float midBoostSw = kEngelFireballDef[kMidBoost];

    // derived (recomputed in updateFilters)
    float chS = 1.0f;       // smoothed channel position (0 Clean .. 1 Lead)
    float drv = 0.65f;      // active drive amount (clean gain or lead gain)
    float chVol = 0.5f;     // active channel volume (clean fixed-ish / lead vol)
    float leadA = 1.0f;     // lead-voice amount (cascade intensity)
    float pushed = 0.5f;    // smoothed high-drive amount

    Biquad inputHp, inputLp, brightShelf;
    Biquad cleanBody, leadTight, leadBite;
    Biquad interHp, interLp, cathodeLp;
    Biquad toneBass, toneMid, toneTreble, midBoostPeak, bottomShelf, brightVoice;
    Biquad phaseHp, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizzNotch, speakerLp;
    DcBlock dcBlock;
    float sag = 0.0f;

    void updateFilters()
    {
        chS = smoothstep(channel);                       // 0 = Clean, 1 = Lead
        // Active drive: Clean is a headroomy low-gain voice; Lead is the Ultra
        // high-gain cascade. RS pins the channel to Lead and drives Lead Gain.
        const float cleanM = 0.02f + 0.34f * cleanGain;  // ~0.02..0.36
        const float leadM  = 0.50f + 0.50f * leadGain;   // ~0.50..1.00 (hotter than JCM800)
        drv = clamp01(cleanM * (1.0f - chS) + leadM * chS);
        // Clean channel runs near unity; Lead channel volume sets preamp drive.
        chVol = (0.55f + 0.30f * cleanGain) * (1.0f - chS) + leadVolume * chS;

        leadA  = smoothstepRange(0.46f, 0.96f, drv);
        pushed = smoothstepRange(0.40f, 0.92f, drv);

        // Bright switch (treble-bypass cap across the gain pot) + brilliance.
        const float brOn = (brightSw >= 0.5f) ? 1.0f : 0.0f;
        // Bottom switch: a low-end boost (the Fireball "Bottom" voicing).
        const float botOn = (bottomSw >= 0.5f) ? 1.0f : 0.0f;
        // Mid Boost: a mid push (the cocked-wah-ish mid lift). >=0.5 = on.
        const float mbOn = (midBoostSw >= 0.5f) ? 1.0f : 0.0f;

        inputHp.setHighPass(sampleRate, 50.0f + 60.0f * leadA + 26.0f * (1.0f - bass) - 18.0f * botOn, 0.70f);
        inputLp.setLowPass(sampleRate, 14600.0f - 3000.0f * leadA + 1100.0f * treble, 0.64f);
        brightShelf.setHighShelf(sampleRate, 1100.0f + 1150.0f * treble, 0.70f,
                                 -1.6f + 4.6f * treble + 1.4f * presence + 1.0f * leadA + 4.0f * brOn);

        // Clean voice body (low-gain, headroomy, fat).
        cleanBody.setPeaking(sampleRate, 430.0f + 130.0f * mid, 0.76f,
                             -0.6f + 2.4f * mid + 1.4f * bass);
        // Lead voice: tight low end + aggressive upper-mid bite (the Fireball
        // signature). Tightens hard as the cascade opens up.
        leadTight.setHighPass(sampleRate, 90.0f + 70.0f * leadA - 30.0f * botOn, 0.72f);
        leadBite.setPeaking(sampleRate, 1950.0f + 640.0f * treble, 0.84f,
                            0.6f + 3.4f * treble + 2.4f * leadA + 1.0f * presence);

        interHp.setHighPass(sampleRate, 76.0f + 90.0f * leadA + 30.0f * (1.0f - bass) - 26.0f * botOn, 0.71f);
        interLp.setLowPass(sampleRate, 9400.0f + 1200.0f * treble - 1700.0f * leadA, 0.64f);
        cathodeLp.setLowPass(sampleRate, 9200.0f + 1400.0f * treble - 1500.0f * leadA, 0.64f);

        // Marshall-ish passive TMB tone stack (shared by both channels) + the
        // BRIGHT / BOTTOM / MID BOOST voicing switches.
        toneBass.setLowShelf(sampleRate, 116.0f + 42.0f * bass, 0.72f,
                             eqDb(bass, 7.0f) - 1.4f * leadA + 4.6f * botOn);
        toneMid.setPeaking(sampleRate, 620.0f + 300.0f * mid, 0.72f,
                           eqDb(mid, 7.0f) + 1.0f * leadA);
        toneTreble.setHighShelf(sampleRate, 2000.0f + 1050.0f * treble, 0.74f,
                                eqDb(treble, 7.0f) + 0.9f * leadA + 2.4f * brOn);
        // MID BOOST: a focused mid push around 720 Hz (on when >=0.5).
        midBoostPeak.setPeaking(sampleRate, 720.0f + 120.0f * mid, 0.90f, 6.0f * mbOn);
        // BOTTOM: an extra low shelf for the chunk.
        bottomShelf.setLowShelf(sampleRate, 95.0f + 30.0f * bass, 0.78f, 4.5f * botOn);
        // BRIGHT: an upper-treble lift on the way out of the stack.
        brightVoice.setHighShelf(sampleRate, 3000.0f, 0.74f, 3.2f * brOn);

        // Power-amp NFB: Presence (high) + speaker voicing.
        phaseHp.setHighPass(sampleRate, 72.0f + 30.0f * leadA - 20.0f * botOn, 0.72f);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1400.0f * treble + 700.0f * presence - 2000.0f * leadA, 0.65f);
        presenceShelf.setHighShelf(sampleRate, 2700.0f + 850.0f * presence, 0.78f,
                                   -4.0f + 8.4f * presence + 1.2f * treble + 2.0f * brOn);

        // 4x EL34 power amp into a 4x12 — tight, scooped-capable modern voicing.
        speakerHp.setHighPass(sampleRate, 74.0f + 10.0f * leadA - 12.0f * botOn, 0.72f);
        speakerThump.setPeaking(sampleRate, 122.0f + 20.0f * bass, 0.88f,
                                0.8f + 2.3f * bass + 2.4f * botOn);
        speakerLowMid.setPeaking(sampleRate, 430.0f + 150.0f * mid, 0.76f,
                                 0.4f + 2.4f * mid + 2.0f * mbOn);
        speakerBite.setPeaking(sampleRate, 2900.0f + 620.0f * treble, 0.78f,
                               2.2f + 2.4f * treble + 1.9f * presence + 0.8f * leadA + 1.2f * brOn);
        // Was a fizz NOTCH (top cut, made it dark). Now an AIR high-shelf: lifts the
        // 4x12 top, retreats hard with gain (de-fizz on high-gain crank). Name kept.
        speakerFizzNotch.setHighShelf(sampleRate, 4700.0f, 0.70f,
                                      9.5f + 2.0f * treble + 2.0f * presence - 4.5f * leadA);
        // Speaker LP opened from ~6.4k (too dark) to ~16k (miked 4x12), eases hard on crank.
        speakerLp.setLowPass(sampleRate, 16000.0f + 2000.0f * treble + 850.0f * presence - 3500.0f * leadA, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset(); brightShelf.reset();
        cleanBody.reset(); leadTight.reset(); leadBite.reset();
        interHp.reset(); interLp.reset(); cathodeLp.reset();
        toneBass.reset(); toneMid.reset(); toneTreble.reset();
        midBoostPeak.reset(); bottomShelf.reset(); brightVoice.reset();
        phaseHp.reset(); phaseLp.reset(); presenceShelf.reset();
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
            case kCleanGain:  cleanGain = v; break;
            case kLeadGain:   leadGain = v; break;
            case kBass:       bass = v; break;
            case kMiddle:     mid = v; break;
            case kTreble:     treble = v; break;
            case kLeadVolume: leadVolume = v; break;
            case kMaster:     master = v; break;
            case kPresence:   presence = v; break;
            case kChannel:    channel = v; break;
            case kBright:     brightSw = v; break;
            case kBottom:     bottomSw = v; break;
            case kMidBoost:   midBoostSw = v; break;
            default: break;
        }
        updateFilters();
    }

    void initDefaults()
    {
        for (int i = 0; i < kParamCount; ++i)
            setParam(i, kEngelFireballDef[i]);
    }

    float process(float in)
    {
        const float cleanW = 1.0f - chS;
        const float leadW = chS;

        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = brightShelf.process(x);
        x = softClip(x * (1.05f + 0.20f * drv + 0.15f * leadW));

        // CLEAN voice: a single, mild gain stage — headroomy and fat.
        float clean = cleanBody.process(x);
        clean = 0.62f * clean + 0.38f * asymTube(clean, 0.84f + 1.20f * drv, 0.004f);

        // LEAD voice: the Ultra high-gain cascade — three saturating stages,
        // tight low end, MORE gain than a JCM800.
        float lead = leadTight.process(x);
        lead = leadBite.process(lead);
        lead = asymTube(lead, 1.95f + 5.60f * drv, 0.018f + 0.012f * presence);
        lead = interHp.process(lead);
        lead = asymTube(lead, 1.35f + 4.40f * drv, -0.014f - 0.010f * drv);
        lead = cathodeLp.process(lead);
        lead = asymTube(lead, 1.10f + 3.20f * drv, 0.010f);
        lead = 0.68f * lead + 0.32f * softClip(lead * (2.1f + 2.0f * drv));

        float y = clean * cleanW + lead * leadW;
        y = interLp.process(y);

        // Shared TMB tone stack + voicing switches (post-gain, like the schematic
        // places the stack between the gain block and the masters).
        y = toneBass.process(y);
        y = toneMid.process(y);
        y = toneTreble.process(y);
        y = midBoostPeak.process(y);
        y = bottomShelf.process(y);
        y = brightVoice.process(y);
        y = phaseHp.process(y);
        y = phaseLp.process(y);

        // Channel volume sets how hard the preamp drives the power amp.
        const float chDrive = 0.66f + 0.78f * chVol;
        y *= chDrive;

        // Tube power amp (~100W) + sag.
        const float env = std::fabs(y);
        const float attack = 1.0f - std::exp(-1.0f / (0.0050f * sampleRate));
        const float release = 1.0f - std::exp(-1.0f / (0.130f * sampleRate));
        sag += (env - sag) * (env > sag ? attack : release);
        const float sagDrop = 1.0f / (1.0f + sag * (0.42f + 1.10f * drv + 0.80f * leadA));

        const float powerDrive = (0.94f + 1.55f * drv + 1.80f * leadA) * sagDrop;
        y = asymTube(y, powerDrive, 0.004f + 0.014f * (presence - bass));
        y = 0.82f * y + 0.18f * softClip(y * (1.8f + 1.25f * leadA));
        y *= 0.98f - 0.07f * sag;

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerFizzNotch.process(y);
        y = speakerLp.process(y);

        // Loudness normalization (keeps multitone RMS ~constant across the Lead
        // Gain sweep so the shared kLvl output stage stays calibrated). The clean
        // / low-gain region barely saturates, so cleanMakeup lifts it.
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.011f * std::fabs((presence - 0.5f) * 16.0f);
        const float cleanMakeup = 1.0f + 5.6f * std::exp(-drv / 0.24f);
        const float level = (0.430f + 0.075f * (1.0f - drv)) * cleanMakeup /
            ((1.0f + 0.34f * drv + 0.62f * leadA) * toneEnergy * chDrive);

        // Master volume. Centred at 0.5 = unity so RS songs that leave it at the
        // musical default keep the calibrated loudness.
        const float masterGain = 0.55f + 0.90f * master;

        return softClip(y * level * masterGain) * 0.97f;
    }
};

class EngelFireballPlugin : public Plugin
{
    EngelFireballCore left;
    EngelFireballCore right;
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
    EngelFireballPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kEngelFireballDef[i];
        left.setSampleRate((float)getSampleRate());
        right.setSampleRate((float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "EngelFireball"; }
    const char* getDescription() const override { return "ENGL Fireball 100 style amp (2 channels, high gain)"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('E', 'g', 'f', 'b'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kEngelFireballNames[index];
        parameter.symbol = kEngelFireballSymbols[index];
        parameter.ranges.min = kEngelFireballMin[index];
        parameter.ranges.max = kEngelFireballMax[index];
        parameter.ranges.def = kEngelFireballDef[index];
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngelFireballPlugin)
};

Plugin* createPlugin()
{
    return new EngelFireballPlugin();
}

END_NAMESPACE_DISTRHO
