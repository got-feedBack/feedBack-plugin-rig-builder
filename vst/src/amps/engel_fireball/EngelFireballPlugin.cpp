/*
 * ENGEL FIREBALL - ENGL Fireball (EN-50) for the game's Amp_EN50. Parody brand
 * "Engel" (ENGL -> Engel); the in-app face must never read "ENGL".
 *
 * Local reference (modelled block-by-block):
 *   amps/ENGL Fireball (EN-50)/engl-fireball-amplifier-schematic_new.pdf
 *       ("ENGL Gerätebau GmbH 625")
 *   amps/ENGL Fireball (EN-50)/1.jpg, 2.jpg   (the front panel, for the canvas)
 *
 * CIRCUIT-REAL port (was tanh/asymTube). Per the schematic: a cascade of ECC83
 * (12AX7) stages — V1A input (Gain + Bright), V1B/V2A more gain with the Ultra
 * relay selecting CLEAN vs the high-gain LEAD path, a passive TMB tone stack
 * (Treble 250k / Bass 1M / Middle 20k scooped), V2B/V3A post-stack recovery ->
 * Master, V4 phase inverter, 6L6GC push-pull power amp (~100W) + Presence NFB.
 *
 * Real engine (see REAL_TUBE_AMP_GUIDE.md): rbtube::TubeStage (12AX7, Koren
 * tables + physical cathode loop) for every gain stage, rbtube::ToneStackYeh
 * (double, ENGL TMB R/C) for the stack, rbtube::PowerAmp6L6GC for the power amp,
 * 2x oversampling around the nonlinear chain. No reference render -> calibrated
 * by character (clean headroomy -> lead tight/aggressive, monotonic crest).
 *
 * the game: RS Gain -> LEAD GAIN (Channel pinned LEAD); Bass/Mid/Treble -> tone
 * stack; Pres -> Presence. See rs_knob_to_vst_param.json.
 */
#include "DistrhoPlugin.hpp"
#include "EngelFireballParams.h"
#include "../../_shared/tube_stage.hpp"   // real 12AX7 stages + 6L6 PP + Yeh tone stack
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
    float cabSim     = kEngelFireballDef[kCabSim];

    // derived (recomputed in updateFilters)
    float chS = 1.0f;       // smoothed channel position (0 Clean .. 1 Lead)
    float drv = 0.65f;      // active drive amount (clean gain or lead gain)
    float chVol = 0.5f;     // active channel volume (clean fixed-ish / lead vol)
    float leadA = 1.0f;     // lead-voice amount (cascade intensity)

    Biquad inputHp, inputLp, brightShelf;
    Biquad cleanBody, leadTight, leadBite;
    Biquad interHp, interLp;
    Biquad midBoostPeak, bottomShelf, brightVoice;
    Biquad phaseHp, phaseLp, presenceShelf;
    Biquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerFizzNotch, speakerLp;
    DcBlock dcBlock;
    // ── real circuit (Koren tube tables) replacing the tanh asymTube ──
    rbtube::TubeStage    v1, vClean, vLead1, vLead2, vLead3, vPost;  // 12AX7 stages
    rbtube::Miller12AX7  v1Miller, cleanMiller, lead1Miller, lead2Miller, lead3Miller, postMiller;
    rbtube::CouplingCapGridLeak coupleToPi;                          // master -> V4 PI grid
    rbtube::PhaseInverterLTP12AX7 phaseInverter;                      // ENGL ECC83 long-tail pair
    rbtube::MultiNodeBPlus supply;                                    // stiff silicon B+ nodes
    rbtube::PowerAmp6L6GC power;                                     // 4x 6L6GC PP (~100W)
    rbtube::ToneStackYeh tone;                                       // real ENGL passive TMB
    float inScale = 1.2f, toneMk = 13.0f;

    void setupTubes()
    {
        // shared input + cascade: standard 12AX7 (250V, /40). fck climbs along the
        // cascade (later stages tighter low end). Self-bias solved per stage.
        v1.set(sampleRate, 1, 250.0f, 40.0f, 22.0f, 1500.0f);   // V1A shared input
        vClean.set(sampleRate, 1, 250.0f, 40.0f, 30.0f, 1500.0f);
        vLead1.set(sampleRate, 1, 250.0f, 40.0f, 38.0f, 1500.0f);
        vLead2.set(sampleRate, 1, 250.0f, 40.0f, 50.0f, 1500.0f);
        vLead3.set(sampleRate, 1, 250.0f, 40.0f, 62.0f, 1500.0f);
        vPost.set(sampleRate, 1, 250.0f, 40.0f, 45.0f, 1500.0f);  // post-stack recovery
        v1Miller.set(sampleRate,     68000.0f, 55.0f, 8.0f);
        cleanMiller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        lead1Miller.set(sampleRate, 180000.0f, 55.0f, 8.0f);
        lead2Miller.set(sampleRate, 150000.0f, 55.0f, 8.0f);
        lead3Miller.set(sampleRate, 150000.0f, 55.0f, 8.0f);
        postMiller.set(sampleRate,  180000.0f, 52.0f, 8.0f);
    }

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

        const float brOn  = (brightSw   >= 0.5f) ? 1.0f : 0.0f;   // BRIGHT treble-boost
        const float botOn = (bottomSw   >= 0.5f) ? 1.0f : 0.0f;   // BOTTOM low-end boost
        const float mbOn  = (midBoostSw >= 0.5f) ? 1.0f : 0.0f;   // MID BOOST mid push

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

        // ENGL passive TMB tone stack — CIRCUIT-REAL (Yeh, double). Marshall-derived
        // but with the ENGL scooped 20k Middle pot (the modern metal voice). slope 47k.
        tone.setComponents(250e3, 1.0e6, 20e3, 47e3, 470e-12, 22e-9, 22e-9);
        tone.update(sampleRate, treble, mid, bass);
        // MID BOOST / BOTTOM / BRIGHT post-stack voicing switches.
        midBoostPeak.setPeaking(sampleRate, 720.0f + 120.0f * mid, 0.90f, 6.0f * mbOn);
        bottomShelf.setLowShelf(sampleRate, 95.0f + 30.0f * bass, 0.78f, 4.5f * botOn);
        brightVoice.setHighShelf(sampleRate, 3000.0f, 0.74f, 3.2f * brOn);

        // 4x 6L6GC power amp drive (morph + lead) + sag; tight modern supply (low sag).
        coupleToPi.set(sampleRate, 1000000.0f, 22.0e-9f, 47000.0f,
                       0.10f, 0.46f + 0.18f * leadA, 1.6f + 0.6f * leadA);
        phaseInverter.setFenderAB763(sampleRate,
                                     0.76f + 1.20f * master + 0.64f * leadA,
                                     0.82f + 0.12f * presence);
        supply.set(sampleRate,
                   42.0f, 220.0f,
                   470.0f, 100.0f,
                   10000.0f, 47.0f,
                   0.075f + 0.025f * leadA,
                   0.055f + 0.018f * leadA,
                   0.026f + 0.012f * leadA,
                   0.120f);
        power.set(sampleRate, 0.86f + 2.10f * master + 1.10f * leadA, -40.0f, 0.09f, 55.0f, 11000.0f);
        power.out = 0.011f;

        // Power-amp NFB: Presence (high) + speaker voicing.
        phaseHp.setHighPass(sampleRate, 72.0f + 30.0f * leadA - 20.0f * botOn, 0.72f);
        phaseLp.setLowPass(sampleRate, 10500.0f + 1400.0f * treble + 700.0f * presence - 2000.0f * leadA, 0.65f);
        presenceShelf.setHighShelf(sampleRate, 2700.0f + 850.0f * presence, 0.78f,
                                   -4.0f + 8.4f * presence + 1.2f * treble + 2.0f * brOn);

        // 4x12 cab — tight, scooped-capable modern voicing.
        speakerHp.setHighPass(sampleRate, 74.0f + 10.0f * leadA - 12.0f * botOn, 0.72f);
        speakerThump.setPeaking(sampleRate, 122.0f + 20.0f * bass, 0.88f,
                                0.8f + 2.3f * bass + 2.4f * botOn);
        speakerLowMid.setPeaking(sampleRate, 430.0f + 150.0f * mid, 0.76f,
                                 0.4f + 2.4f * mid + 2.0f * mbOn);
        speakerBite.setPeaking(sampleRate, 2900.0f + 620.0f * treble, 0.78f,
                               2.2f + 2.4f * treble + 1.9f * presence + 0.8f * leadA + 1.2f * brOn);
        // a real 4x12 ROLLS OFF the top (no fizz shelf — that inflates crest without
        // distorting); gentle HF cut + LP, eases on crank.
        speakerFizzNotch.setHighShelf(sampleRate, 4700.0f, 0.70f,
                                      -3.0f + 2.0f * treble + 2.0f * presence - 2.0f * leadA);
        speakerLp.setLowPass(sampleRate, 11500.0f + 1800.0f * treble + 850.0f * presence - 3000.0f * leadA, 0.66f);
    }

public:
    void reset()
    {
        inputHp.reset(); inputLp.reset(); brightShelf.reset();
        cleanBody.reset(); leadTight.reset(); leadBite.reset();
        interHp.reset(); interLp.reset();
        midBoostPeak.reset(); bottomShelf.reset(); brightVoice.reset();
        phaseHp.reset(); phaseLp.reset(); presenceShelf.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset(); speakerBite.reset();
        speakerFizzNotch.reset(); speakerLp.reset(); dcBlock.reset();
        v1Miller.reset(); cleanMiller.reset(); lead1Miller.reset();
        lead2Miller.reset(); lead3Miller.reset(); postMiller.reset();
        v1.reset(); vClean.reset(); vLead1.reset(); vLead2.reset(); vLead3.reset(); vPost.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset(); tone.reset();
        setupTubes();
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
            case kCabSim:     cabSim = v; break;
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
        x = v1.process(v1Miller.process(x) * inScale);   // V1A shared input (real 12AX7 + Miller)

        // CLEAN voice: a single, mild gain stage — headroomy and fat.
        float clean = cleanBody.process(x);
        clean = vClean.process(cleanMiller.process(clean) * (0.7f + 0.9f * drv));

        // LEAD voice: the Ultra high-gain cascade — three real 12AX7 stages, tight
        // low end, MORE gain than a JCM800.
        float lead = leadTight.process(x);
        lead = leadBite.process(lead);
        lead = vLead1.process(lead1Miller.process(lead) * (1.3f + 13.0f * drv));
        lead = vLead2.process(lead2Miller.process(lead) * (1.0f + 9.5f * drv));
        lead = vLead3.process(lead3Miller.process(lead) * (0.8f + 7.0f * drv));

        float y = clean * cleanW + lead * leadW;
        y = interLp.process(y);

        // Post-stack recovery stage (V2B/V3A) — drives harder in the lead region.
        y = vPost.process(postMiller.process(y) * (1.0f + 1.6f * drv + 1.2f * leadW));

        // Real ENGL TMB tone stack (Yeh) + insertion-loss makeup, then voicing switches.
        y = tone.process(y) * toneMk;
        y = midBoostPeak.process(y);
        y = bottomShelf.process(y);
        y = brightVoice.process(y);
        y = phaseHp.process(y);
        y = phaseLp.process(y);

        // Channel volume sets how hard the preamp drives the power amp.
        const float chDrive = 0.66f + 0.78f * chVol;
        y *= chDrive;

        // 4x 6L6GC power amp through the real LTP PI and stiff silicon B+ nodes.
        const float pLoad = std::fabs(y) * (0.72f + 0.82f * master + 0.55f * leadA);
        const rbtube::SupplyScales bplus = supply.process(pLoad, pLoad * 0.50f, pLoad * 0.18f);
        y *= 0.92f + 0.08f * bplus.preamp;
        y = coupleToPi.process(y * bplus.screen, 1.0f + 0.18f * leadA);
        y = phaseInverter.process(y) * bplus.screen;
        y = power.process(y * bplus.power);

        y = presenceShelf.process(y);
        y = dcBlock.process(y);

        const float ampOnly = y;
        float cab = speakerHp.process(ampOnly);
        cab = speakerThump.process(cab);
        cab = speakerLowMid.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizzNotch.process(cab);
        cab = speakerLp.process(cab);
        y = ampOnly + cabSim * (cab - ampOnly);

        // Loudness normalization (keeps multitone RMS ~constant across the Lead
        // Gain sweep so the shared kLvl output stage stays calibrated). The clean
        // / low-gain region barely saturates, so cleanMakeup lifts it.
        const float toneEnergy = 1.0f
            + 0.012f * std::fabs((bass - 0.5f) * 15.0f)
            + 0.013f * std::fabs((mid - 0.5f) * 17.0f)
            + 0.013f * std::fabs((treble - 0.5f) * 17.0f)
            + 0.011f * std::fabs((presence - 0.5f) * 16.0f);
        const float cleanMakeup = 1.0f + 2.4f * std::exp(-drv / 0.26f);
        const float level = (0.72f + 0.12f * (1.0f - drv)) * cleanMakeup /
            ((1.0f + 0.34f * drv + 0.62f * leadA) * toneEnergy * chDrive);

        // Master volume. Centred at 0.5 = unity so RS songs that leave it at the
        // musical default keep the calibrated loudness.
        const float masterGain = 0.55f + 0.90f * master;

        // Final soft saturation = the output-transformer/OT clip; also caps the
        // post-distortion edge transients (without it the cascade's square edges
        // pass as spikes). Mirrors the DSL100 reference.
        return softClip(y * level * masterGain) * 0.97f;
    }
};

class EngelFireballPlugin : public Plugin
{
    EngelFireballCore left;
    EngelFireballCore right;
    float params[kParamCount];
    rbshared::Oversampler4x osL, osR;          // anti-alias around the nonlinear chain
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
    EngelFireballPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kEngelFireballDef[i];
        left.setSampleRate(kOS * (float)getSampleRate());
        right.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "EngelFireball"; }
    const char* getDescription() const override { return "ENGL Fireball style amp (2 channels, high gain)"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
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
                ubL[k] = rbAmpLvl(0.620f * left.process(ubL[k]));
                ubR[k] = rbAmpLvl(0.620f * right.process(ubR[k]));
            }
            outL[i] = osL.downsample(ubL);
            outR[i] = osR.downsample(ubR);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngelFireballPlugin)
};

Plugin* createPlugin()
{
    return new EngelFireballPlugin();
}

END_NAMESPACE_DISTRHO
