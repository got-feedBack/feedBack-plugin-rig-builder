#ifndef RB_PEDAL_DELAY_COMPONENTS_H
#define RB_PEDAL_DELAY_COMPONENTS_H

#include "ChorusComponents.h"
#include <cmath>

namespace rbdelay {

enum DelayCharacter
{
    kCharacterBbd = 0,
    kCharacterPt2399,
    kCharacterDigital,
    kCharacterOilCan,
    kCharacterDrum
};

struct DelayVoice
{
    DelayCharacter character;
    float minDelayMs;
    float maxDelayMs;
    bool reverseDelayKnob;
    bool logDelayTaper;
    bool rangeScales;   // DE7-style S/M/L RANGE rescales the delay window

    float inputHpHz;
    float inputLpHz;
    float loopHpHz;
    float loopLpHz;
    float outputLpHz;
    float toneDepth;

    float feedbackMax;
    float wetGain;
    float dryDucking;
    float driveMinDb;
    float driveMaxDb;
    float outputMinDb;
    float outputMaxDb;
    float companderAmount;

    float noiseBase;
    float noiseDelayScale;
    float clockBleed;
    float clockBuckets;
    float clockFactor;
    float wowMs;
    float flutterMs;
    float smearMs;
    float stereoSpreadMs;

    int headCount;
    float headRatio[4];
    float headGain[4];

    DelayVoice()
        : character(kCharacterBbd),
          minDelayMs(20.0f),
          maxDelayMs(600.0f),
          reverseDelayKnob(false),
          logDelayTaper(false),
          rangeScales(false),
          inputHpHz(28.0f),
          inputLpHz(7600.0f),
          loopHpHz(70.0f),
          loopLpHz(4200.0f),
          outputLpHz(6800.0f),
          toneDepth(0.35f),
          feedbackMax(0.78f),
          wetGain(1.0f),
          dryDucking(0.16f),
          driveMinDb(-2.0f),
          driveMaxDb(8.0f),
          outputMinDb(-8.0f),
          outputMaxDb(4.0f),
          companderAmount(0.45f),
          noiseBase(0.00004f),
          noiseDelayScale(0.00045f),
          clockBleed(0.00018f),
          clockBuckets(4096.0f),
          clockFactor(0.5f),
          wowMs(0.35f),
          flutterMs(0.08f),
          smearMs(0.0f),
          stereoSpreadMs(0.0f),
          headCount(1)
    {
        headRatio[0] = 1.0f;
        headGain[0] = 1.0f;
        for (int i = 1; i < 4; ++i)
        {
            headRatio[i] = 1.0f;
            headGain[i] = 0.0f;
        }
    }
};

struct DelayControls
{
    float delay;
    float feedback;
    float mix;
    float drive;
    float output;
    float tone;
    float rate;
    float depth;
    float mode;
    float range;
    float heads;
    float shape;      // LFO waveform 0..1: sine/tri/square/saw-up/saw-down/S&H (MF-104M panel)

    DelayControls()
        : delay(0.35f),
          feedback(0.25f),
          mix(0.30f),
          drive(0.35f),
          output(0.65f),
          tone(0.55f),
          rate(0.20f),
          depth(0.15f),
          shape(0.0f),
          mode(0.0f),
          range(0.5f),
          heads(1.0f)
    {}
};

struct StereoOut
{
    float left;
    float right;
};

static inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

static inline float lerp(float a, float b, float t)
{
    return a + (b - a) * rbmod::clamp01(t);
}

static inline float logLerp(float a, float b, float t)
{
    a = std::fmax(a, 0.001f);
    b = std::fmax(b, a + 0.001f);
    return std::exp(std::log(a) + (std::log(b) - std::log(a)) * rbmod::clamp01(t));
}

class ComponentDelayCore
{
    DelayVoice voice;
    DelayControls controls;
    rbmod::DelayBuffer line;
    rbmod::HighPass inputHp;
    rbmod::LowPass inputLp;
    rbmod::HighPass loopHp;
    rbmod::LowPass loopLp1;
    rbmod::LowPass loopLp2;
    rbmod::LowPass outputLpL;
    rbmod::LowPass outputLpR;
    rbmod::BbdCompander compander;
    rbmod::NoiseSource noise;

    float sampleRate = 48000.0f;
    float smoothedDelayMs = 120.0f;
    float wowPhase = 0.0f;
    float lastShPhase = 0.0f;
    float shValue = 0.0f;
    unsigned shSeed = 22222u;
    float flutterPhase = 0.0f;
    float clockPhase = 0.0f;
    float lastFeedback = 0.0f;

    float maxVoiceDelayMs() const
    {
        float maxMs = voice.maxDelayMs;
        for (int i = 0; i < voice.headCount && i < 4; ++i)
            maxMs = std::fmax(maxMs, voice.maxDelayMs * voice.headRatio[i]);
        if (voice.rangeScales) maxMs *= 1.75f;   // RANGE can scale the window up
        return maxMs + voice.stereoSpreadMs + voice.smearMs + 80.0f;
    }

    float currentDelayMs() const
    {
        float t = rbmod::clamp01(controls.delay);
        if (voice.reverseDelayKnob)
            t = 1.0f - t;
        float ms = voice.logDelayTaper ? logLerp(voice.minDelayMs, voice.maxDelayMs, t)
                                       : lerp(voice.minDelayMs, voice.maxDelayMs, t);
        // DE7-style S/M/L RANGE switch: rescale the whole delay-time window.
        // Neutral at range=0.5 (0.30+1.40*0.5=1.0) so voices that don't set
        // rangeScales — which pass the default range=0.5 — are unaffected.
        if (voice.rangeScales)
            ms *= 0.30f + 1.40f * rbmod::clamp01(controls.range);
        return ms;
    }

    void updateFilters()
    {
        const float tone = rbmod::clamp01(controls.tone);
        const float delayN = rbmod::clamp01((currentDelayMs() - voice.minDelayMs) /
                                            std::fmax(1.0f, voice.maxDelayMs - voice.minDelayMs));
        const float toneTilt = 0.48f + 0.86f * tone;
        const float longDelayLoss = 1.0f - 0.26f * delayN;

        inputHp.setHz(voice.inputHpHz, sampleRate);
        inputLp.setHz(voice.inputLpHz * (0.70f + 0.52f * tone), sampleRate);
        loopHp.setHz(voice.loopHpHz * (0.85f + 0.55f * controls.feedback), sampleRate);
        loopLp1.setHz(voice.loopLpHz * toneTilt * longDelayLoss, sampleRate);
        loopLp2.setHz(voice.loopLpHz * (0.72f + 0.42f * tone) * longDelayLoss, sampleRate);
        outputLpL.setHz(voice.outputLpHz * (0.62f + voice.toneDepth * tone), sampleRate);
        outputLpR.setHz(voice.outputLpHz * (0.62f + voice.toneDepth * tone), sampleRate);
        compander.setSampleRate(sampleRate, 16.0f + 30.0f * voice.companderAmount);
    }

    float characterDrive(float x, float driveGain) const
    {
        switch (voice.character)
        {
        case kCharacterDrum:
            return std::tanh(0.72f * x * driveGain + 0.045f) * 1.34f - 0.060f;
        case kCharacterOilCan:
            return 0.72f * std::tanh(x * driveGain) + 0.28f * std::tanh(x * driveGain * 0.42f);
        case kCharacterPt2399:
            return std::tanh(x * driveGain * 0.82f);
        case kCharacterDigital:
            return std::tanh(x * driveGain * 0.70f);
        case kCharacterBbd:
        default:
            return std::tanh(x * driveGain);
        }
    }

    float characterWet(float x, float delayMs)
    {
        const float delayN = rbmod::clamp01((delayMs - voice.minDelayMs) /
                                            std::fmax(1.0f, voice.maxDelayMs - voice.minDelayMs));
        float y = x;
        if (voice.character == kCharacterBbd)
        {
            y = compander.process(y, voice.companderAmount);
            const float seconds = std::fmax(0.001f, delayMs * 0.001f);
            const float clockHz = rbmod::clamp(voice.clockFactor * voice.clockBuckets / seconds,
                                               4500.0f, 180000.0f);
            clockPhase += clockHz / sampleRate;
            clockPhase -= std::floor(clockPhase);
            const float clockFade = rbmod::clamp((sampleRate * 0.46f - clockHz) / (sampleRate * 0.16f), 0.0f, 1.0f);
            y += std::sin(rbmod::kTwoPi * clockPhase) * voice.clockBleed * clockFade;
        }
        else if (voice.character == kCharacterPt2399)
        {
            const float grit = 0.18f + 0.82f * delayN;
            y = std::tanh(y * (1.0f + 0.20f * grit)) / (1.0f + 0.06f * grit);
            y += noise.next() * (0.00002f + 0.00018f * grit);
        }
        else if (voice.character == kCharacterOilCan)
        {
            y = 0.74f * std::tanh(y * 1.18f) + 0.26f * y;
            y += noise.next() * (0.00005f + 0.00010f * delayN);
        }
        else if (voice.character == kCharacterDrum)
        {
            y = 0.86f * std::tanh(y * 1.08f) + 0.14f * y;
        }

        return y + noise.next() * (voice.noiseBase + voice.noiseDelayScale * delayN);
    }

    float readHeads(float delayMs, float offsetMs, bool right)
    {
        const int maxHeads = voice.headCount < 1 ? 1 : (voice.headCount > 4 ? 4 : voice.headCount);
        const int activeHeads = maxHeads <= 1 ? 1 : 1 + (int)std::floor(rbmod::clamp01(controls.heads) * (float)(maxHeads - 1) + 0.5f);
        float sum = 0.0f;
        float gain = 0.0f;

        for (int i = 0; i < activeHeads; ++i)
        {
            float ms = delayMs * voice.headRatio[i] + offsetMs;
            if (right)
                ms += voice.stereoSpreadMs * (0.55f + 0.18f * (float)i);
            if (voice.smearMs > 0.0f)
            {
                const float s0 = line.read(std::fmax(1.0f, (ms - voice.smearMs * 0.55f) * sampleRate * 0.001f));
                const float s1 = line.read(std::fmax(1.0f, ms * sampleRate * 0.001f));
                const float s2 = line.read(std::fmax(1.0f, (ms + voice.smearMs * 0.70f) * sampleRate * 0.001f));
                sum += voice.headGain[i] * (0.24f * s0 + 0.52f * s1 + 0.24f * s2);
            }
            else
            {
                sum += voice.headGain[i] * line.read(std::fmax(1.0f, ms * sampleRate * 0.001f));
            }
            gain += std::fabs(voice.headGain[i]);
        }

        return gain > 0.001f ? sum / gain : 0.0f;
    }

public:
    void setVoice(const DelayVoice& v)
    {
        voice = v;
        line.resizeForMs(sampleRate, maxVoiceDelayMs());
        const unsigned int bucketSeed = (unsigned int)std::fmax(1.0f, voice.clockBuckets);
        noise.seed((bucketSeed * 2654435761u) ^ 0x6d2b79f5u);
        updateFilters();
        reset();
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr > 1000.0f ? sr : 48000.0f;
        line.resizeForMs(sampleRate, maxVoiceDelayMs());
        updateFilters();
        reset();
    }

    void reset()
    {
        line.reset();
        inputHp.reset();
        inputLp.reset();
        loopHp.reset();
        loopLp1.reset();
        loopLp2.reset();
        outputLpL.reset();
        outputLpR.reset();
        compander.reset();
        smoothedDelayMs = currentDelayMs();
        wowPhase = 0.0f;
        flutterPhase = 0.0f;
        clockPhase = 0.0f;
        lastFeedback = 0.0f;
    }

    void setControls(const DelayControls& c)
    {
        controls = c;
        controls.delay = rbmod::clamp01(controls.delay);
        controls.feedback = rbmod::clamp01(controls.feedback);
        controls.mix = rbmod::clamp01(controls.mix);
        controls.drive = rbmod::clamp01(controls.drive);
        controls.output = rbmod::clamp01(controls.output);
        controls.tone = rbmod::clamp01(controls.tone);
        controls.rate = rbmod::clamp01(controls.rate);
        controls.depth = rbmod::clamp01(controls.depth);
        controls.mode = rbmod::clamp01(controls.mode);
        controls.range = rbmod::clamp01(controls.range);
        controls.heads = rbmod::clamp01(controls.heads);
        updateFilters();
    }

    StereoOut process(float inL, float inR)
    {
        const rbmod::StereoInputPair feed = rbmod::stereoPedalFeeds(inL, inR);
        const float input = 0.5f * (feed.left + feed.right);

        const float targetMs = currentDelayMs();
        const float slewHz = 6.0f + 22.0f * (1.0f - rbmod::clamp01(targetMs / std::fmax(80.0f, voice.maxDelayMs)));
        smoothedDelayMs += rbmod::onePoleCoeffHz(slewHz, sampleRate) * (targetMs - smoothedDelayMs);

        const float rateHz = 0.045f + 7.5f * rbmod::smoothstep(controls.rate);
        wowPhase += rateHz / sampleRate;
        wowPhase -= std::floor(wowPhase);
        flutterPhase += (5.5f + 13.0f * controls.depth) / sampleRate;
        flutterPhase -= std::floor(flutterPhase);

        // MF-104M-style LFO waveform select for the delay-time modulation.
        // 6 shapes across the pot; sine (0) is the legacy default so every
        // other delay voice sounds identical.
        float lfo;
        {
            const int shp = (int)(rbmod::clamp01(controls.shape) * 5.999f);
            const float ph = wowPhase - std::floor(wowPhase);
            switch (shp)
            {
            default: lfo = std::sin(rbmod::kTwoPi * ph); break;                       // sine
            case 1:  lfo = 4.0f * std::fabs(ph - 0.5f) - 1.0f; break;                // triangle
            case 2:  lfo = ph < 0.5f ? 1.0f : -1.0f; break;                          // square
            case 3:  lfo = 2.0f * ph - 1.0f; break;                                  // saw up
            case 4:  lfo = 1.0f - 2.0f * ph; break;                                  // saw down
            case 5:                                                                   // sample & hold
                if (ph < lastShPhase) shValue = 2.0f * ((shSeed = shSeed * 1664525u + 1013904223u) >> 16 & 0x7FFF) / 32767.0f - 1.0f;
                lastShPhase = ph; lfo = shValue; break;
            }
        }
        const float wow = lfo * voice.wowMs * (0.15f + 0.85f * controls.depth);
        const float flutter = std::sin(rbmod::kTwoPi * flutterPhase + 0.7f * std::sin(rbmod::kTwoPi * wowPhase)) *
                              voice.flutterMs * (0.18f + 0.82f * controls.depth);
        const float modMs = wow + flutter;

        float tapL = readHeads(smoothedDelayMs, modMs, false);
        float tapR = readHeads(smoothedDelayMs, -0.62f * modMs, true);
        const float fbTap = 0.5f * (tapL + tapR);

        float feedbackPath = loopHp.process(fbTap);
        feedbackPath = loopLp1.process(feedbackPath);
        feedbackPath = loopLp2.process(feedbackPath);
        feedbackPath = characterWet(feedbackPath, smoothedDelayMs);
        lastFeedback = feedbackPath;

        float x = inputHp.process(input);
        x = inputLp.process(x);
        const float driveGain = dbToGain(lerp(voice.driveMinDb, voice.driveMaxDb, controls.drive));
        x = characterDrive(x, driveGain);

        const float fb = voice.feedbackMax * rbmod::smoothstep(controls.feedback);
        const float write = std::tanh(x + feedbackPath * fb);
        line.write(write);

        tapL = outputLpL.process(characterWet(tapL, smoothedDelayMs));
        tapR = outputLpR.process(characterWet(tapR, smoothedDelayMs));

        const float outputGain = dbToGain(lerp(voice.outputMinDb, voice.outputMaxDb, controls.output));
        const float wetLevel = controls.mix * voice.wetGain * outputGain;
        const float dryTrim = 1.0f - voice.dryDucking * controls.mix;

        StereoOut out;
        out.left = std::tanh(feed.left * dryTrim + tapL * wetLevel);
        out.right = std::tanh(feed.right * dryTrim + tapR * wetLevel);
        return out;
    }
};

static inline DelayVoice fm104Voice()
{
    DelayVoice v;
    v.character = kCharacterBbd;
    v.minDelayMs = 40.0f;
    v.maxDelayMs = 800.0f;
    v.inputLpHz = 8200.0f;
    v.loopLpHz = 4300.0f;
    v.outputLpHz = 6500.0f;
    v.toneDepth = 0.50f;
    v.feedbackMax = 0.86f;
    v.wetGain = 1.05f;
    v.dryDucking = 0.10f;
    v.driveMinDb = -4.0f;
    v.driveMaxDb = 12.0f;
    v.outputMinDb = -10.0f;
    v.outputMaxDb = 5.0f;
    v.companderAmount = 0.62f;
    v.noiseBase = 0.000035f;
    v.noiseDelayScale = 0.00070f;
    v.clockBleed = 0.00022f;
    v.clockBuckets = 4096.0f;
    v.wowMs = 0.52f;
    v.flutterMs = 0.08f;
    return v;
}

static inline DelayVoice dm2Voice()
{
    DelayVoice v;
    v.character = kCharacterBbd;
    v.minDelayMs = 20.0f;
    v.maxDelayMs = 300.0f;
    v.reverseDelayKnob = true;
    v.inputLpHz = 6800.0f;
    v.loopLpHz = 3250.0f;
    v.outputLpHz = 4100.0f;
    v.feedbackMax = 0.91f;
    v.wetGain = 1.12f;
    v.dryDucking = 0.07f;
    v.driveMinDb = -3.0f;
    v.driveMaxDb = 6.0f;
    v.outputMinDb = -8.0f;
    v.outputMaxDb = 3.0f;
    v.companderAmount = 0.74f;
    v.noiseBase = 0.000030f;
    v.noiseDelayScale = 0.00055f;
    v.clockBleed = 0.00018f;
    v.clockBuckets = 4096.0f;
    v.wowMs = 0.20f;
    v.flutterMs = 0.045f;
    return v;
}

static inline DelayVoice pt2399Voice()
{
    DelayVoice v;
    v.character = kCharacterPt2399;
    v.minDelayMs = 28.0f;
    v.maxDelayMs = 650.0f;
    v.logDelayTaper = true;
    v.inputLpHz = 7600.0f;
    v.loopLpHz = 3700.0f;
    v.outputLpHz = 6200.0f;
    v.feedbackMax = 0.83f;
    v.wetGain = 1.03f;
    v.driveMinDb = -3.0f;
    v.driveMaxDb = 7.0f;
    v.outputMinDb = -7.0f;
    v.outputMaxDb = 4.0f;
    v.noiseBase = 0.000025f;
    v.noiseDelayScale = 0.00042f;
    v.wowMs = 0.18f;
    v.flutterMs = 0.030f;
    return v;
}

static inline DelayVoice dll10Voice()
{
    DelayVoice v;
    v.character = kCharacterDigital;
    v.minDelayMs = 20.0f;
    v.maxDelayMs = 450.0f;
    v.inputLpHz = 8000.0f;
    v.loopLpHz = 4700.0f;
    v.outputLpHz = 7200.0f;
    v.feedbackMax = 0.78f;
    v.wetGain = 1.02f;
    v.driveMinDb = -5.0f;
    v.driveMaxDb = 3.0f;
    v.outputMinDb = -8.0f;
    v.outputMaxDb = 4.0f;
    v.noiseBase = 0.000012f;
    v.noiseDelayScale = 0.00008f;
    v.wowMs = 1.20f;
    v.flutterMs = 0.06f;
    v.stereoSpreadMs = 3.5f;
    return v;
}

static inline DelayVoice de7Voice()
{
    DelayVoice v;
    v.character = kCharacterDigital;
    v.minDelayMs = 30.0f;
    v.maxDelayMs = 2600.0f;
    v.rangeScales = true;   // the S/M/L RANGE switch
    v.logDelayTaper = true;
    v.inputLpHz = 7800.0f;
    v.loopLpHz = 3600.0f;
    v.outputLpHz = 6100.0f;
    v.feedbackMax = 0.80f;
    v.wetGain = 1.05f;
    v.driveMinDb = -5.0f;
    v.driveMaxDb = 4.0f;
    v.outputMinDb = -8.0f;
    v.outputMaxDb = 4.0f;
    v.noiseBase = 0.000018f;
    v.noiseDelayScale = 0.00018f;
    v.wowMs = 0.90f;
    v.flutterMs = 0.10f;
    v.smearMs = 3.0f;
    v.stereoSpreadMs = 10.0f;
    return v;
}

static inline DelayVoice oilCanVoice()
{
    DelayVoice v;
    v.character = kCharacterOilCan;
    v.minDelayMs = 70.0f;
    v.maxDelayMs = 620.0f;
    v.inputLpHz = 6000.0f;
    v.loopLpHz = 2700.0f;
    v.outputLpHz = 4300.0f;
    v.feedbackMax = 0.76f;
    v.wetGain = 1.18f;
    v.driveMinDb = -2.0f;
    v.driveMaxDb = 8.0f;
    v.outputMinDb = -9.0f;
    v.outputMaxDb = 4.0f;
    v.noiseBase = 0.00008f;
    v.noiseDelayScale = 0.00032f;
    v.wowMs = 2.8f;
    v.flutterMs = 0.45f;
    v.smearMs = 12.0f;
    v.stereoSpreadMs = 1.8f;
    return v;
}

static inline DelayVoice binsonVoice()
{
    DelayVoice v;
    v.character = kCharacterDrum;
    v.minDelayMs = 70.0f;
    v.maxDelayMs = 760.0f;
    v.inputLpHz = 7000.0f;
    v.loopLpHz = 3600.0f;
    v.outputLpHz = 5600.0f;
    v.feedbackMax = 0.84f;
    v.wetGain = 1.08f;
    v.driveMinDb = -3.0f;
    v.driveMaxDb = 10.0f;
    v.outputMinDb = -9.0f;
    v.outputMaxDb = 4.0f;
    v.noiseBase = 0.000045f;
    v.noiseDelayScale = 0.00020f;
    v.wowMs = 1.15f;
    v.flutterMs = 0.11f;
    v.smearMs = 2.0f;
    v.stereoSpreadMs = 5.5f;
    v.headCount = 4;
    v.headRatio[0] = 0.36f; v.headGain[0] = 0.70f;
    v.headRatio[1] = 0.55f; v.headGain[1] = 0.82f;
    v.headRatio[2] = 0.73f; v.headGain[2] = 0.74f;
    v.headRatio[3] = 1.00f; v.headGain[3] = 0.92f;
    return v;
}

} // namespace rbdelay

#endif // RB_PEDAL_DELAY_COMPONENTS_H
