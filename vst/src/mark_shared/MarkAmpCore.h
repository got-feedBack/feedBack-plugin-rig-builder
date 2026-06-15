#ifndef RB_MARK_AMP_CORE_H
#define RB_MARK_AMP_CORE_H

#include <cmath>

/*
 * Shared Mesa Mark amp core.
 *
 * Local references used for this voicing pass:
 *   amps/Mesa Mark III (CA_38)/boogie_mkiii.pdf
 *   amps/Mesa Mark III (CA_38)/mk3-1.gif
 *   amps/Mesa Mark III (CA_38)/mk3-2.gif
 *   amps/Mesa Mark IV (CA_85)/Mesa_Boogie_Mark_IV_Schematics.pdf
 *
 * The folder names are legacy/confusing in this checkout. The runtime mapping is:
 *   Amp_CA85 -> Mark III, voiced as the Mark III R2 / crunch path.
 *   Amp_CA38 -> Mark IV, voiced around the Mark IV lead path.
 */

static constexpr float rbPi = 3.14159265359f;

static inline float rbClamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline float rbSmooth(float v)
{
    v = rbClamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

static inline float rbSmoothRange(float a, float b, float x)
{
    return rbSmooth((x - a) / (b - a));
}

static inline float rbEqDb(float v, float rangeDb)
{
    return (rbClamp01(v) - 0.5f) * 2.0f * rangeDb;
}

static inline float rbAsymTube(float x, float drive, float bias)
{
    const float b = std::tanh(bias);
    const float y = std::tanh(x * drive + bias) - b;
    return y / (1.0f - 0.24f * std::fabs(b));
}

class RbBiquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        if (std::fabs(na0) < 1.0e-12f)
            na0 = 1.0f;
        const float k = 1.0f / na0;
        b0 = nb0 * k; b1 = nb1 * k; b2 = nb2 * k; a1 = na1 * k; a2 = na2 * k;
    }

    static float hzLimit(float hz, float sr)
    {
        return std::fmax(20.0f, std::fmin(hz, sr * 0.45f));
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

    void highPass(float sr, float hz, float q)
    {
        hz = hzLimit(hz, sr);
        const float w = 2.0f * rbPi * hz / sr, c = std::cos(w), a = std::sin(w) / (2.0f * q);
        set((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f, 1.0f + a, -2.0f * c, 1.0f - a);
    }

    void lowPass(float sr, float hz, float q)
    {
        hz = hzLimit(hz, sr);
        const float w = 2.0f * rbPi * hz / sr, c = std::cos(w), a = std::sin(w) / (2.0f * q);
        set((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f, 1.0f + a, -2.0f * c, 1.0f - a);
    }

    void peak(float sr, float hz, float q, float gainDb)
    {
        hz = hzLimit(hz, sr);
        const float amp = std::pow(10.0f, gainDb / 40.0f);
        const float w = 2.0f * rbPi * hz / sr, c = std::cos(w), a = std::sin(w) / (2.0f * q);
        set(1.0f + a * amp, -2.0f * c, 1.0f - a * amp, 1.0f + a / amp, -2.0f * c, 1.0f - a / amp);
    }

    void shelf(float sr, float hz, bool high, float gainDb)
    {
        hz = hzLimit(hz, sr);
        const float amp = std::pow(10.0f, gainDb / 40.0f);
        const float w = 2.0f * rbPi * hz / sr, c = std::cos(w), s = std::sin(w);
        const float root = std::sqrt(amp), a = s * 0.5f * std::sqrt(2.0f);
        if (high)
            set(amp * ((amp + 1.0f) + (amp - 1.0f) * c + 2.0f * root * a),
                -2.0f * amp * ((amp - 1.0f) + (amp + 1.0f) * c),
                amp * ((amp + 1.0f) + (amp - 1.0f) * c - 2.0f * root * a),
                (amp + 1.0f) - (amp - 1.0f) * c + 2.0f * root * a,
                2.0f * ((amp - 1.0f) - (amp + 1.0f) * c),
                (amp + 1.0f) - (amp - 1.0f) * c - 2.0f * root * a);
        else
            set(amp * ((amp + 1.0f) - (amp - 1.0f) * c + 2.0f * root * a),
                2.0f * amp * ((amp - 1.0f) - (amp + 1.0f) * c),
                amp * ((amp + 1.0f) - (amp - 1.0f) * c - 2.0f * root * a),
                (amp + 1.0f) + (amp - 1.0f) * c + 2.0f * root * a,
                -2.0f * ((amp - 1.0f) + (amp + 1.0f) * c),
                (amp + 1.0f) + (amp - 1.0f) * c - 2.0f * root * a);
    }
};

class RbMarkAmpCore
{
    bool markIV = false;
    float sampleRate = 48000.0f;
    float gain = 0.5f, bass = 0.5f, mid = 0.5f, treble = 0.6f;
    float sag = 0.0f, dcX = 0.0f, dcY = 0.0f;

    RbBiquad inputHp, inputLp, brightShelf;
    RbBiquad toneBass, toneMid, toneTreble, postToneHp, postToneLp;
    RbBiquad crunchBody, crunchBite, leadTight, leadMid, leadBright;
    RbBiquad geq80, geq240, geq750, geq2200, geq6600;
    RbBiquad presenceShelf, feedbackLow;
    RbBiquad speakerHp, speakerThump, speakerLowMid, speakerBite, speakerNotch, speakerLp;

    float crunch() const
    {
        return markIV ? rbSmoothRange(0.24f, 0.74f, gain)
                      : rbSmoothRange(0.12f, 0.90f, gain);
    }

    float lead() const
    {
        return markIV ? rbSmoothRange(0.48f, 0.98f, gain)
                      : 0.40f * rbSmoothRange(0.72f, 1.0f, gain);
    }

    void update()
    {
        const float c = crunch();
        const float l = lead();

        if (markIV)
        {
            // Mark IV: V1A -> tone controls -> channel gains -> V1B -> lead
            // drive -> V3A/V3B -> V2A, then GEQ and power amp.
            inputHp.highPass(sampleRate, 70.0f + 70.0f * c + 40.0f * (1.0f - bass), 0.71f);
            inputLp.lowPass(sampleRate, 15000.0f + 1100.0f * treble - 3300.0f * l, 0.64f);
            brightShelf.shelf(sampleRate, 1150.0f + 1150.0f * treble, true,
                              -1.4f + 5.3f * treble + 1.3f * c);

            toneBass.shelf(sampleRate, 100.0f + 26.0f * bass, false,
                           rbEqDb(bass, 7.2f) - 4.4f * c);
            toneMid.peak(sampleRate, 560.0f + 210.0f * mid, 0.78f,
                         rbEqDb(mid, 7.0f) + 1.2f * c - 1.8f * l);
            toneTreble.shelf(sampleRate, 1850.0f + 850.0f * treble, true,
                             rbEqDb(treble, 7.8f) + 2.0f * c);
            postToneHp.highPass(sampleRate, 92.0f + 88.0f * l + 40.0f * (1.0f - bass), 0.70f);
            postToneLp.lowPass(sampleRate, 9500.0f + 1200.0f * treble - 2600.0f * l, 0.64f);

            crunchBody.peak(sampleRate, 440.0f + 150.0f * mid, 0.76f,
                            -0.5f + 3.2f * mid + 1.1f * bass);
            crunchBite.peak(sampleRate, 1300.0f + 620.0f * treble, 0.82f,
                            0.4f + 3.0f * treble + 1.4f * c);
            leadTight.shelf(sampleRate, 126.0f, false, -4.8f * l - 1.8f * c + 3.2f * bass);
            leadMid.peak(sampleRate, 760.0f + 130.0f * mid, 0.95f,
                         -0.8f - 2.8f * l + 3.2f * mid);
            leadBright.shelf(sampleRate, 2600.0f + 520.0f * treble, true,
                             -1.3f + 4.2f * treble + 2.4f * l);

            geq80.shelf(sampleRate, 82.0f, false, 1.2f + 2.9f * bass + 1.2f * c);
            geq240.peak(sampleRate, 240.0f, 0.88f, -1.0f + 1.8f * bass - 1.8f * l);
            geq750.peak(sampleRate, 750.0f, 1.03f, -1.8f - 5.6f * l * (1.0f - 0.50f * mid));
            geq2200.peak(sampleRate, 2200.0f + 360.0f * treble, 0.84f,
                         0.8f + 2.7f * treble + 2.2f * l);
            geq6600.peak(sampleRate, 6600.0f, 1.12f, -2.8f - 2.8f * l + 0.9f * treble);
            presenceShelf.shelf(sampleRate, 2750.0f + 650.0f * treble, true,
                                -3.0f + 5.3f * treble + 1.8f * l);
            feedbackLow.shelf(sampleRate, 112.0f + 16.0f * bass, false,
                              -0.8f + 3.0f * bass + 1.5f * c);

            speakerHp.highPass(sampleRate, 80.0f, 0.72f);
            speakerThump.peak(sampleRate, 120.0f + 18.0f * bass, 0.88f, 0.8f + 2.1f * bass);
            speakerLowMid.peak(sampleRate, 410.0f + 120.0f * mid, 0.76f, 0.4f + 1.9f * mid - 0.8f * l);
            speakerBite.peak(sampleRate, 2950.0f + 540.0f * treble, 0.80f, 0.7f + 2.4f * treble + 1.1f * l);
            speakerNotch.peak(sampleRate, 5350.0f, 1.12f, -3.4f - 2.6f * l);
            speakerLp.lowPass(sampleRate, 6600.0f + 1750.0f * treble - 1000.0f * l, 0.66f);
        }
        else
        {
            // Mark III for CA85: treat the game Gain as the R2/crunch gain.
            // The schematic's tone controls sit before the gain stages, so Bass
            // tightens heavily as Gain rises and Treble drives the first stages.
            inputHp.highPass(sampleRate, 60.0f + 58.0f * c + 54.0f * (1.0f - bass), 0.71f);
            inputLp.lowPass(sampleRate, 15200.0f + 900.0f * treble - 1900.0f * c, 0.64f);
            brightShelf.shelf(sampleRate, 950.0f + 1100.0f * treble, true,
                              -1.8f + 5.8f * treble + 0.9f * c);

            toneBass.shelf(sampleRate, 94.0f + 24.0f * bass, false,
                           rbEqDb(bass, 6.8f) - 3.5f * c);
            toneMid.peak(sampleRate, 520.0f + 220.0f * mid, 0.74f,
                         rbEqDb(mid, 7.2f) + 2.4f * c);
            toneTreble.shelf(sampleRate, 1750.0f + 820.0f * treble, true,
                             rbEqDb(treble, 7.6f) + 1.4f * c);
            postToneHp.highPass(sampleRate, 78.0f + 58.0f * c + 34.0f * (1.0f - bass), 0.70f);
            postToneLp.lowPass(sampleRate, 10200.0f + 1100.0f * treble - 1350.0f * c, 0.64f);

            crunchBody.peak(sampleRate, 390.0f + 150.0f * mid, 0.76f,
                            0.8f + 3.0f * mid + 1.2f * bass);
            crunchBite.peak(sampleRate, 1250.0f + 610.0f * treble, 0.82f,
                            0.5f + 3.3f * treble + 1.4f * c);
            leadTight.shelf(sampleRate, 126.0f, false, -2.6f * c + 3.6f * bass);
            leadMid.peak(sampleRate, 710.0f + 120.0f * mid, 0.92f,
                         0.4f + 2.8f * mid - 1.0f * l);
            leadBright.shelf(sampleRate, 2450.0f + 520.0f * treble, true,
                             -1.6f + 3.5f * treble + 1.0f * c);

            geq80.shelf(sampleRate, 82.0f, false, 1.0f + 2.6f * bass + 0.7f * c);
            geq240.peak(sampleRate, 240.0f, 0.88f, -0.5f + 1.8f * bass - 0.8f * c);
            geq750.peak(sampleRate, 750.0f, 1.00f, -0.9f - 3.2f * c * (1.0f - 0.55f * mid));
            geq2200.peak(sampleRate, 2200.0f + 350.0f * treble, 0.84f,
                         0.6f + 2.1f * treble + 1.1f * c);
            geq6600.peak(sampleRate, 6600.0f, 1.12f, -2.1f - 1.7f * c + 0.7f * treble);
            presenceShelf.shelf(sampleRate, 2850.0f + 540.0f * treble, true,
                                -2.8f + 4.2f * treble + 0.8f * c);
            feedbackLow.shelf(sampleRate, 112.0f + 14.0f * bass, false,
                              -0.4f + 2.8f * bass + 0.8f * c);

            speakerHp.highPass(sampleRate, 82.0f, 0.72f);
            speakerThump.peak(sampleRate, 122.0f + 16.0f * bass, 0.88f, 0.6f + 1.8f * bass);
            speakerLowMid.peak(sampleRate, 430.0f + 115.0f * mid, 0.76f, 0.5f + 2.0f * mid);
            speakerBite.peak(sampleRate, 2850.0f + 500.0f * treble, 0.80f, 0.7f + 2.0f * treble + 0.7f * c);
            speakerNotch.peak(sampleRate, 5200.0f, 1.12f, -2.7f - 1.7f * c);
            speakerLp.lowPass(sampleRate, 6900.0f + 1600.0f * treble - 650.0f * c, 0.66f);
        }
    }

public:
    explicit RbMarkAmpCore(bool isMarkIV) : markIV(isMarkIV) {}
    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; reset(); }

    void reset()
    {
        inputHp.reset(); inputLp.reset(); brightShelf.reset();
        toneBass.reset(); toneMid.reset(); toneTreble.reset(); postToneHp.reset(); postToneLp.reset();
        crunchBody.reset(); crunchBite.reset(); leadTight.reset(); leadMid.reset(); leadBright.reset();
        geq80.reset(); geq240.reset(); geq750.reset(); geq2200.reset(); geq6600.reset();
        presenceShelf.reset(); feedbackLow.reset();
        speakerHp.reset(); speakerThump.reset(); speakerLowMid.reset();
        speakerBite.reset(); speakerNotch.reset(); speakerLp.reset();
        sag = dcX = dcY = 0.0f;
        update();
    }

    void setGain(float v) { gain = rbClamp01(v); update(); }
    void setBass(float v) { bass = rbClamp01(v); update(); }
    void setMid(float v) { mid = rbClamp01(v); update(); }
    void setTreble(float v) { treble = rbClamp01(v); update(); }

    float process(float in)
    {
        const float c = crunch();
        const float l = lead();
        float x = inputHp.process(in);
        x = inputLp.process(x);
        x = brightShelf.process(x);
        x = toneBass.process(x);
        x = toneMid.process(x);
        x = toneTreble.process(x);
        x = postToneHp.process(x);
        x = postToneLp.process(x);

        float y;
        if (markIV)
        {
            y = rbAsymTube(x, 1.30f + 4.9f * gain + 1.9f * c, 0.010f + 0.006f * treble);
            y = crunchBody.process(y);
            y = crunchBite.process(y);
            y = leadTight.process(y);
            y = rbAsymTube(y, 1.45f + 4.5f * gain + 3.0f * l, -0.012f - 0.010f * l);
            y = leadMid.process(y);
            y = leadBright.process(y);
            y = 0.68f * y + 0.32f * rbAsymTube(y, 1.65f + 2.5f * l, 0.004f);
        }
        else
        {
            y = rbAsymTube(x, 1.18f + 3.2f * gain + 1.3f * c, 0.012f + 0.004f * treble);
            y = crunchBody.process(y);
            y = crunchBite.process(y);
            y = leadTight.process(y);
            y = rbAsymTube(y, 1.22f + 2.7f * gain + 1.5f * c, -0.006f - 0.004f * c);
            y = leadMid.process(y);
            y = leadBright.process(y);
            y = 0.82f * y + 0.18f * rbAsymTube(y, 1.55f + 1.6f * c + 1.8f * l, 0.003f);
        }

        y = geq80.process(y);
        y = geq240.process(y);
        y = geq750.process(y);
        y = geq2200.process(y);
        y = geq6600.process(y);
        y = presenceShelf.process(y);
        y = feedbackLow.process(y);

        const float env = std::fabs(y);
        const float a = 1.0f - std::exp(-1.0f / (0.004f * sampleRate));
        const float r = 1.0f - std::exp(-1.0f / (0.115f * sampleRate));
        sag += (env - sag) * (env > sag ? a : r);
        const float powerDrive = markIV ? (1.04f + 0.82f * gain + 0.55f * c)
                                        : (1.02f + 0.62f * gain + 0.38f * c);
        y = rbAsymTube(y, powerDrive / (1.0f + sag * (markIV ? 0.34f : 0.26f)),
                       0.004f + 0.006f * (treble - bass));

        const float dc = y - dcX + 0.995f * dcY;
        dcX = y; dcY = dc; y = dc;
        y = speakerHp.process(y);
        y = speakerThump.process(y);
        y = speakerLowMid.process(y);
        y = speakerBite.process(y);
        y = speakerNotch.process(y);
        y = speakerLp.process(y);

        const float level = (markIV ? 0.74f : 0.86f) / (1.0f + 0.18f * gain + 0.28f * c + 0.24f * l);
        return std::tanh(y * level) * 0.98f;
    }
};

#endif // RB_MARK_AMP_CORE_H
