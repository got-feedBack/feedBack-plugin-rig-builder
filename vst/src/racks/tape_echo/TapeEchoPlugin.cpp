/*
 * TapeEcho - Roland RE-201 Space Echo style rack tape echo.
 *
 * Reference: racks/Roland RE-101 & RE-201 Service Manual.pdf.  The RE-201 block
 * diagram is a mono tape machine: input preamps -> meter/drive amp -> recording
 * amp + bias oscillator -> tape loop -> three playback heads -> mode selector
 * -> echo volume/tone/bass/treble -> output mixer.  The service manual also
 * shows the DC brushless motor/repeat-rate circuit, erase/record/PB heads and
 * OP-14B echo board transistor stages.
 *
 * the game exposes only Time, Feedback, Filter, Stereo and Mix.  This plugin
 * keeps those public parameters but maps them onto the RE-201 behavior:
 *   Time     = repeat-rate / tape speed, preserving RS ms/700 mapping
 *   Feedback = intensity / regeneration into the record amp
 *   Filter   = combined bass/treble/tone coloration of the echo path
 *   Stereo   = artificial spread of the three mono PB heads
 *   Mix      = echo volume / wet-dry mixer
 */
#include "DistrhoPlugin.hpp"
#include "TapeEchoParams.h"
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

static inline float onePoleCoeffMs(float ms, float sr)
{
    const float safeSr = sr > 1000.0f ? sr : 48000.0f;
    return 1.0f - std::exp(-1.0f / std::fmax(1.0f, ms * 0.001f * safeSr));
}

static inline float logInterp(float lo, float hi, float t)
{
    return lo * std::pow(hi / lo, clamp01(t));
}

static inline float tapeSoftLimit(float x)
{
    // Tape + transistor stages bend gradually and retain some punch.
    return 0.78f * std::tanh(1.18f * x) + 0.22f * std::tanh(0.36f * x);
}

static inline float timeParamToMs(float timeNorm)
{
    // Existing mapping is RS milliseconds divided by 700.  Preserve that so
    // older song values around 110..120 ms remain in that range.
    return rbmod::clamp(clamp01(timeNorm) * 700.0f, 55.0f, 700.0f);
}

struct TapeStereoOut
{
    float wetL;
    float wetR;
};

class SpaceEchoTransport
{
    float sampleRate = 48000.0f;
    float baseMs = 210.0f;
    float smoothBaseMs = 210.0f;
    float feedback = 0.34f;
    float filter = 0.45f;
    float stereo = 0.50f;

    float wowPhase = 0.0f;
    float flutterPhase = 0.0f;
    float scrapePhase = 0.0f;
    float biasPhase = 0.0f;
    float lastHeadMix = 0.0f;

    rbmod::DelayBuffer tape;
    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::LowPass recordPreLow;
    rbmod::LowPass tapeLoss1;
    rbmod::LowPass tapeLoss2;
    rbmod::HighPass playbackHp;
    rbmod::LowPass playbackToneLp;
    rbmod::LowPass outputDeemphasisL;
    rbmod::LowPass outputDeemphasisR;
    rbmod::NoiseSource noise;
    rbshared::OpAmpStage recordAmp;
    rbshared::OpAmpStage playbackAmp;

    void updateFilters()
    {
        const float f = smoothstep(filter);
        const float longDelay = rbmod::clamp((baseMs - 55.0f) / (700.0f - 55.0f), 0.0f, 1.0f);

        inputHp.setHz(24.0f, sampleRate);
        inputLp.setHz(9800.0f, sampleRate);
        recordPreLow.setHz(1350.0f + 650.0f * f, sampleRate);
        tapeLoss1.setHz(logInterp(1180.0f, 7600.0f, f) * (1.0f - 0.30f * longDelay), sampleRate);
        tapeLoss2.setHz(logInterp(980.0f, 5400.0f, f) * (1.0f - 0.22f * longDelay), sampleRate);
        playbackHp.setHz(70.0f - 34.0f * f, sampleRate);
        playbackToneLp.setHz(logInterp(980.0f, 9200.0f, f), sampleRate);
        outputDeemphasisL.setHz(4300.0f + 4200.0f * f, sampleRate);
        outputDeemphasisR.setHz(4300.0f + 4200.0f * f, sampleRate);
    }

    float playbackHead(float ratio, float level, float modMs)
    {
        const float delayMs = rbmod::clamp(smoothBaseMs * ratio + modMs, 12.0f, 1380.0f);
        float y = tape.read(delayMs * sampleRate * 0.001f);
        y = tapeLoss1.process(y);
        y = tapeLoss2.process(y);
        y = playbackHp.process(y);
        y = playbackToneLp.process(y);
        y = playbackAmp.process(y * level, 1.6f);
        return tapeSoftLimit(y);
    }

public:
    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        tape.resizeForMs(sampleRate, 1450.0f);
        recordAmp.setSpec(rbshared::ta7136apSpec());
        playbackAmp.setSpec(rbshared::ta7136apSpec());
        recordAmp.setSampleRate(sampleRate);
        playbackAmp.setSampleRate(sampleRate);
        noise.seed(0x201101u);
        reset();
    }

    void reset()
    {
        smoothBaseMs = baseMs;
        wowPhase = 0.0f;
        flutterPhase = 0.0f;
        scrapePhase = 0.0f;
        biasPhase = 0.0f;
        lastHeadMix = 0.0f;
        tape.reset();
        inputHp.reset();
        inputLp.reset();
        recordPreLow.reset();
        tapeLoss1.reset();
        tapeLoss2.reset();
        playbackHp.reset();
        playbackToneLp.reset();
        outputDeemphasisL.reset();
        outputDeemphasisR.reset();
        recordAmp.reset();
        playbackAmp.reset();
        updateFilters();
    }

    void setControls(float timeNorm, float feedbackNorm, float filterNorm, float stereoNorm)
    {
        baseMs = timeParamToMs(timeNorm);
        feedback = 0.04f + 0.78f * smoothstep(feedbackNorm);
        filter = clamp01(filterNorm);
        stereo = clamp01(stereoNorm);
        updateFilters();
    }

    TapeStereoOut process(float inL, float inR)
    {
        const float input = 0.5f * (inL + inR);
        const float smoothA = onePoleCoeffMs(24.0f, sampleRate);
        smoothBaseMs += smoothA * (baseMs - smoothBaseMs);

        const float speedN = rbmod::clamp((smoothBaseMs - 55.0f) / (700.0f - 55.0f), 0.0f, 1.0f);
        const float tapeWear = 0.55f + 0.45f * speedN;

        wowPhase += (0.44f + 0.10f * speedN) / sampleRate;
        flutterPhase += (5.8f + 1.8f * tapeWear) / sampleRate;
        scrapePhase += (17.0f + 5.0f * stereo) / sampleRate;
        biasPhase += 82000.0f / sampleRate;
        wowPhase -= std::floor(wowPhase);
        flutterPhase -= std::floor(flutterPhase);
        scrapePhase -= std::floor(scrapePhase);
        biasPhase -= std::floor(biasPhase);

        const float wowMs = (0.55f + 2.1f * speedN) * std::sin(rbmod::kTwoPi * wowPhase);
        const float flutterMs = (0.050f + 0.16f * tapeWear) * std::sin(rbmod::kTwoPi * flutterPhase);
        const float scrapeMs = (0.010f + 0.035f * tapeWear) * std::sin(rbmod::kTwoPi * scrapePhase);
        const float transportMs = wowMs + flutterMs + scrapeMs;

        // RE-201 has three PB heads.  Ratios are tuned so a 110 ms RS value
        // behaves like a short Space Echo slap and higher Time reaches long,
        // smeared multi-head repeats.
        float h1 = playbackHead(0.72f, 0.86f, transportMs);
        float h2 = playbackHead(1.00f, 1.00f, transportMs * 1.06f + 0.18f);
        float h3 = playbackHead(1.43f, 0.82f, transportMs * 0.92f + 0.31f);

        // The RE-201's three PB heads always sound together (the rhythmic multi-tap
        // pattern is fixed in hardware); Stereo must ONLY pan the outer heads L/R, it
        // must NOT gate the taps — otherwise at Stereo=0 the echo collapsed to a single
        // head (h2) and lost the signature multi-head rhythm in mono.
        const float mainHeads = h2 + 0.52f * h1 + 0.46f * h3;
        const float spreadL = mainHeads + stereo * (0.34f * h1 - 0.18f * h3);
        const float spreadR = mainHeads + stereo * (0.34f * h3 - 0.18f * h1);

        float wetL = outputDeemphasisL.process(tapeSoftLimit(spreadL * (0.78f + 0.10f * filter)));
        float wetR = outputDeemphasisR.process(tapeSoftLimit(spreadR * (0.78f + 0.10f * filter)));

        // Feedback returns through the record amp, roughly from the selected
        // playback bus before the final output tone network.
        lastHeadMix = tapeSoftLimit(0.52f * lastHeadMix + 0.48f * mainHeads);

        float recordIn = inputHp.process(input);
        recordIn = inputLp.process(recordIn);
        const float preLow = recordPreLow.process(recordIn);
        recordIn = recordIn + (0.34f + 0.16f * filter) * (recordIn - preLow);
        recordIn += lastHeadMix * feedback;

        const float drive = dbToGain(2.2f + 4.6f * feedback);
        float record = recordAmp.process(recordIn * drive, 2.1f);
        record = tapeSoftLimit(record);

        const float biasLeak = std::sin(rbmod::kTwoPi * biasPhase) * (0.000012f + 0.000026f * tapeWear);
        const float hiss = noise.next() * (0.000030f + 0.000075f * tapeWear);
        tape.write(tapeSoftLimit(record * (0.94f - 0.06f * tapeWear)) + biasLeak + hiss);

        return { wetL, wetR };
    }
};

} // namespace

class TapeEchoPlugin : public Plugin
{
    SpaceEchoTransport transport;
    float params[kParamCount];
    float mix = kTapeEchoDef[kMix];

    void applyAll()
    {
        transport.setControls(params[kTime], params[kFeedback], params[kFilter], params[kStereo]);
        mix = clamp01(params[kMix]);
    }

public:
    TapeEchoPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i)
            params[i] = kTapeEchoDef[i];

        transport.setSampleRate(48000.0f);
        applyAll();
    }

protected:
    const char* getLabel() const override { return "TapeEcho"; }
    const char* getDescription() const override { return "Roland RE-201 Space Echo style tape delay"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('R', 'T', 'e', '1'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount)
            return;

        parameter.hints = kParameterIsAutomatable;
        parameter.name = kTapeEchoNames[index];
        parameter.symbol = kTapeEchoSymbols[index];
        parameter.ranges.min = kTapeEchoMin[index];
        parameter.ranges.max = kTapeEchoMax[index];
        parameter.ranges.def = kTapeEchoDef[index];
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
        transport.setSampleRate((float)newSampleRate);
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float m = smoothstep(mix);
        const float dryGain = 1.0f - 0.22f * m;
        const float wetGain = 1.02f * m;

        for (uint32_t i = 0; i < frames; ++i)
        {
            const rbmod::StereoInputPair feed = rbmod::stereoPedalFeeds(inputs[0][i], inputs[1][i]);
            const TapeStereoOut wet = transport.process(feed.left, feed.right);
            outputs[0][i] = feed.left * dryGain + wet.wetL * wetGain;
            outputs[1][i] = feed.right * dryGain + wet.wetR * wetGain;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoPlugin)
};

Plugin* createPlugin()
{
    return new TapeEchoPlugin();
}

END_NAMESPACE_DISTRHO
