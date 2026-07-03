/*
 * SynthFilter - Korg MS-20 External Signal Processor / VCF style filterbank.
 *
 * Reference: racks/Korg_MS_20_Service_Manual.pdf.  The MS-20 external signal
 * processor specifies low cut 50..2500 Hz, high cut 100..5000 Hz, threshold/CV
 * extraction and band-pass filtered out.  The main synth VCF uses separate
 * resonant VC HPF and VC LPF blocks (KORG35 in this service manual).
 *
 * the game exposes Sens, Attack, Release, FilterType and Mix, so this plugin
 * turns the guitar/bass input into an envelope CV and sweeps an MS-20-style
 * HPF->LPF pair. FilterType crossfades LP / ESP band-pass / HP outputs.
 */
#include "DistrhoPlugin.hpp"
#include "SynthFilterParams.h"
#include "../../pedals/_shared/ChorusComponents.h"
#include "../../pedals/_shared/opamp.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

static inline float clamp01(float v)
{
    return rbmod::clamp01(v);
}

static inline float smoothstep(float v)
{
    return rbmod::smoothstep(v);
}

static inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float audioTaper(float v, float curve)
{
    return std::pow(clamp01(v), curve);
}

static inline float onePoleCoeffHz(float hz, float sr)
{
    return rbmod::onePoleCoeffHz(hz, sr > 1000.0f ? sr : 48000.0f);
}

static inline float onePoleCoeffMs(float ms, float sr)
{
    const float safeSr = sr > 1000.0f ? sr : 48000.0f;
    return 1.0f - std::exp(-1.0f / std::fmax(1.0f, ms * 0.001f * safeSr));
}

static inline float logInterp(float lo, float hi, float t)
{
    return lo * std::pow(hi / lo, clamp01(t));
}

class HighPass
{
    float x1 = 0.0f;
    float y1 = 0.0f;
    float a = 0.0f;

public:
    void set(float sr, float hz)
    {
        const float safeSr = sr > 1000.0f ? sr : 48000.0f;
        const float dt = 1.0f / safeSr;
        const float rc = 1.0f / (2.0f * rbmod::kPi * rbmod::clamp(hz, 5.0f, safeSr * 0.42f));
        a = rc / (rc + dt);
    }

    void reset()
    {
        x1 = 0.0f;
        y1 = 0.0f;
    }

    float process(float x)
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = y;
        return y;
    }
};

class LowPass
{
    float y = 0.0f;
    float a = 0.0f;

public:
    void set(float sr, float hz)
    {
        a = onePoleCoeffHz(hz, sr);
    }

    void reset()
    {
        y = 0.0f;
    }

    float process(float x)
    {
        y += a * (x - y);
        return y;
    }
};

class Ms20Envelope
{
    float sampleRate = 48000.0f;
    float attack = kSynthFilterDef[kAttack];
    float release = kSynthFilterDef[kRelease];
    float sens = kSynthFilterDef[kSens];
    float env = 0.0f;
    float cv = 0.0f;
    float attackA = 0.0f;
    float releaseA = 0.0f;
    float cvA = 0.0f;

    void update()
    {
        // MS-20 EGs reach 10 seconds, but the external signal processor is used
        // as an audio envelope follower here. Keep musical rack ranges.
        attackA = onePoleCoeffMs(1.2f + 180.0f * audioTaper(attack, 1.55f), sampleRate);
        releaseA = onePoleCoeffMs(28.0f + 1120.0f * audioTaper(release, 1.25f), sampleRate);
        cvA = onePoleCoeffMs(4.0f, sampleRate);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        reset();
    }

    void reset()
    {
        env = 0.0f;
        cv = 0.0f;
        update();
    }

    void setControls(float sensNorm, float attackNorm, float releaseNorm)
    {
        sens = clamp01(sensNorm);
        attack = clamp01(attackNorm);
        release = clamp01(releaseNorm);
        update();
    }

    float process(float x)
    {
        // The ESP rectifier/comparator produces an ENV OUT and a trigger.  The
        // soft threshold keeps string noise from fully opening the filter, while
        // strong attacks still jump the CV.
        const float threshold = 0.010f + 0.034f * (1.0f - smoothstep(sens));
        const float rect = std::fmax(0.0f, std::fabs(x) - threshold);
        const float scaled = rbmod::clamp(rect * (7.5f + 28.0f * smoothstep(sens)), 0.0f, 1.7f);
        const float target = scaled / (1.0f + scaled);
        env += (target > env ? attackA : releaseA) * (target - env);

        const float shaped = smoothstep(std::pow(clamp01(env), 0.62f));
        cv += cvA * (shaped - cv);
        return cv;
    }
};

class Korg35Section
{
    float sampleRate = 48000.0f;
    bool highPass = false;
    float ic1 = 0.0f;
    float ic2 = 0.0f;
    float last = 0.0f;
    float bpPrev = 0.0f;

public:
    void setSampleRate(float sr, bool hp)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        highPass = hp;
        reset();
    }

    void reset()
    {
        ic1 = 0.0f;
        ic2 = 0.0f;
        last = 0.0f;
        bpPrev = 0.0f;
    }

    float process(float x, float cutoffHz, float peak)
    {
        const float sr = sampleRate;
        const float cutoff = rbmod::clamp(cutoffHz, 22.0f, sr * 0.42f);
        const float resonance = smoothstep(peak);

        // KORG35 behavior is not a clean textbook SVF: the feedback/drive path
        // clips and thins when peak is high. Use a TPT two-pole core with
        // saturating feedback and post-stage asymmetry to keep it dirty but sane.
        const float g = std::tan(rbmod::kPi * cutoff / sr);
        const float r = 1.18f - 0.86f * resonance;
        const float drive = 1.0f + 1.55f * resonance;
        // Resonance feedback source: the LP section feeds back its own (resonant,
        // falling-response) output => it screams at the cutoff, the MS-20 signature.
        // The HP section's output has a RISING response, so feeding it back made it
        // self-oscillate at NYQUIST (~24 kHz squeal); feed the HP loop from the
        // BANDPASS node instead, which peaks at the cutoff -> it screams at Fc too.
        const float fbSource = highPass ? bpPrev : last;
        const float feedback = rbmod::softClip(fbSource * (0.12f + 0.55f * resonance));
        const float input = rbmod::softClip((x - feedback) * drive);

        const float a1 = 1.0f / (1.0f + g * (g + r));
        const float a2 = g * a1;
        const float v3 = input - ic2;
        const float bp = a1 * ic1 + a2 * v3;
        const float lp = ic2 + a2 * ic1 + g * a2 * v3;
        const float hp = input - r * bp - lp;
        ic1 = 2.0f * bp - ic1;
        ic2 = 2.0f * lp - ic2;

        float y = highPass ? hp : lp;
        const float bite = bp * (0.10f + 0.30f * resonance);
        y = rbmod::softClip(y + (highPass ? bite : -bite));
        bpPrev = bp;
        last = y;
        return y;
    }
};

class Ms20SynthFilterCore
{
    float sampleRate = 48000.0f;
    float sens = kSynthFilterDef[kSens];
    float attack = kSynthFilterDef[kAttack];
    float release = kSynthFilterDef[kRelease];
    float type = kSynthFilterDef[kFilterType];
    float mix = kSynthFilterDef[kMix];

    rbshared::OpAmpStage inputAmp;
    rbshared::OpAmpStage outputAmp;
    Ms20Envelope envelope;
    HighPass espLowCut;
    LowPass espHighCut;
    LowPass cvLag;
    Korg35Section hpf;
    Korg35Section lpf;

    float cv = 0.0f;

    void updateFilters()
    {
        const float typeCurve = smoothstep(type);
        const float lowCut = logInterp(50.0f, 2500.0f, 0.22f + 0.48f * typeCurve);
        const float highCut = logInterp(5000.0f, 100.0f, 0.10f + 0.24f * (1.0f - typeCurve));
        espLowCut.set(sampleRate, lowCut);
        espHighCut.set(sampleRate, highCut);
        cvLag.set(sampleRate, 14.0f + 16.0f * release);
    }

    float modeMix(float dry, float lp, float bp, float hp) const
    {
        float wet;
        if (type < 0.5f)
        {
            const float t = smoothstep(type * 2.0f);
            wet = lp * (1.0f - t) + bp * t;
        }
        else
        {
            const float t = smoothstep((type - 0.5f) * 2.0f);
            wet = bp * (1.0f - t) + hp * t;
        }

        const float m = smoothstep(mix);
        return dry * (1.0f - 0.86f * m) + wet * (0.94f * m);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        inputAmp.setSpec(rbshared::jrc4558Spec());
        outputAmp.setSpec(rbshared::jrc4558Spec());
        inputAmp.setSampleRate(sampleRate);
        outputAmp.setSampleRate(sampleRate);
        envelope.setSampleRate(sampleRate);
        hpf.setSampleRate(sampleRate, true);
        lpf.setSampleRate(sampleRate, false);
        reset();
    }

    void reset()
    {
        cv = 0.0f;
        inputAmp.reset();
        outputAmp.reset();
        envelope.reset();
        espLowCut.reset();
        espHighCut.reset();
        cvLag.reset();
        hpf.reset();
        lpf.reset();
        updateFilters();
    }

    void setControls(float sensNorm, float attackNorm, float releaseNorm, float typeNorm, float mixNorm)
    {
        sens = clamp01(sensNorm);
        attack = clamp01(attackNorm);
        release = clamp01(releaseNorm);
        type = clamp01(typeNorm);
        mix = clamp01(mixNorm);
        envelope.setControls(sens, attack, release);
        updateFilters();
    }

    float process(float in)
    {
        // ESP input: auto-pad and 4558-ish conditioning. The MS-20 expects large
        // external signals; guitar DI gets a little pre-emphasis and soft limit.
        float x = inputAmp.process(in * dbToGain(5.2f), 1.8f);
        x = rbmod::softClip(x * (1.12f + 0.22f * smoothstep(sens)));

        const float esp = espHighCut.process(espLowCut.process(x));
        const float envCv = envelope.process(esp);
        cv = cvLag.process(envCv);

        // MS-20 main VCF nominal ranges from the manual: 50 Hz..15 kHz for both
        // VC HPF and VC LPF. External processor band-pass range is narrower, so
        // FilterType biases the base cutoffs toward ESP-style BP around center.
        const float typeCurve = smoothstep(type);
        const float cvDepth = 0.22f + 0.76f * smoothstep(sens);
        const float sweep = clamp01(0.13f + cv * cvDepth);
        const float lpBase = logInterp(160.0f, 1250.0f, 0.30f + 0.22f * (1.0f - typeCurve));
        const float hpBase = logInterp(38.0f, 620.0f, 0.16f + 0.62f * typeCurve);
        const float lpCutoff = rbmod::clamp(lpBase * std::pow(18.0f, sweep), 60.0f, 15000.0f);
        const float hpCutoff = rbmod::clamp(hpBase * std::pow(7.5f, sweep * (0.74f + 0.22f * typeCurve)), 45.0f, 9800.0f);

        // Resonance stays RESONANT (synthy vocal sweeps) but below the self-oscillation
        // onset, so the filter follows the guitar instead of whistling over it.
        const float peak = rbmod::clamp(0.15f + 0.30f * smoothstep(mix) + 0.14f * smoothstep(sens), 0.0f, 0.62f);
        const float hp = hpf.process(x, hpCutoff, peak * (0.86f + 0.10f * typeCurve));
        const float lp = lpf.process(hp, lpCutoff, peak);
        const float bp = rbmod::softClip((hp - lp) * (0.82f + 0.42f * peak));

        float y = modeMix(x, lp, bp, hp);
        const float trim = 0.46f - 0.05f * smoothstep(mix) + 0.04f * (1.0f - typeCurve);
        y = outputAmp.process(y * trim, 1.3f);
        return rbmod::softClip(y);
    }
};

} // namespace

class SynthFilterPlugin : public Plugin
{
    Ms20SynthFilterCore left;
    Ms20SynthFilterCore right;
    float params[kParamCount];

    void applyAll()
    {
        left.setControls(params[kSens], params[kAttack], params[kRelease], params[kFilterType], params[kMix]);
        right.setControls(params[kSens], params[kAttack], params[kRelease], params[kFilterType], params[kMix]);
    }

public:
    SynthFilterPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kSynthFilterDef[i];

        left.setSampleRate(48000.0f);
        right.setSampleRate(48000.0f);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "SynthFilter"; }
    const char* getDescription() const override { return "MS-20 ESP/KORG35 style synth filterbank"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'S', 'f', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kSynthFilterNames[index];
        parameter.symbol = kSynthFilterSymbols[index];
        parameter.ranges.min = kSynthFilterMin[index];
        parameter.ranges.max = kSynthFilterMax[index];
        parameter.ranges.def = kSynthFilterDef[index];
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
        applyAll();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        left.setSampleRate((float)newSampleRate);
        right.setSampleRate((float)newSampleRate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        float* outL = outputs[0];
        float* outR = outputs[1];

        for (uint32_t i = 0; i < frames; ++i)
        {
            const rbmod::StereoInputPair feed = rbmod::stereoPedalFeeds(inputs[0][i], inputs[1][i]);
            outL[i] = left.process(feed.left);
            outR[i] = right.process(feed.right);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthFilterPlugin)
};

Plugin* createPlugin()
{
    return new SynthFilterPlugin();
}

END_NAMESPACE_DISTRHO
