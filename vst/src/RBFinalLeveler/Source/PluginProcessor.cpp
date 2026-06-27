#include <JuceHeader.h>

class RBFinalLevelerAudioProcessor final : public juce::AudioProcessor
{
public:
    RBFinalLevelerAudioProcessor()
        : juce::AudioProcessor(
              BusesProperties()
                  .withInput("Input", juce::AudioChannelSet::stereo(), true)
                  .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
    {
    }

    ~RBFinalLevelerAudioProcessor() override = default;

    const juce::String getName() const override
    {
        return "RB Final Leveler";
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        currentGainDb = 0.0f;
        currentGain = 1.0f;
        limGain = 1.0f;
        levelInitialized = false;
        detectorSeeded = false;
        warmupSamples = 0;
        gateHoldSamples = 0;
        gateGain = 1.0f;
        msEnv = 0.0;
        rawMsEnv = 0.0;
        designKWeighting(sr);
        for (int ch = 0; ch < 2; ++ch) { kPre[ch].reset(); kRlb[ch].reset(); }
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();

        if (in != out)
            return false;

        return in == juce::AudioChannelSet::mono()
            || in == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        if (numChannels <= 0 || numSamples <= 0)
            return;

        const float targetRmsDb = *apvts.getRawParameterValue("target_rms_db");
        const float maxBoostDb = *apvts.getRawParameterValue("max_boost_db");
        const float maxCutDb = *apvts.getRawParameterValue("max_cut_db");
        const float gateDb = *apvts.getRawParameterValue("gate_db");
        const float attackMs = *apvts.getRawParameterValue("attack_ms");
        const float releaseMs = *apvts.getRawParameterValue("release_ms");
        const float ceilingDb = *apvts.getRawParameterValue("ceiling_db");
        const float trimDb = *apvts.getRawParameterValue("trim_db");

        // Loudness detector: a FIXED-TIME (~30 ms) running mean-square of the
        // K-WEIGHTED signal (ITU-R BS.1770), integrated per sample so it is
        // independent of host block size. K-weighting makes this measure
        // PERCEIVED loudness (LUFS), not raw RMS — so spectrally/dynamically
        // different tones (bass vs guitar, clean vs fuzz) are leveled to equal
        // perceived loudness instead of equal RMS (which sounded uneven). 30 ms
        // reacts quickly to a tone change; the gain follower below smooths any
        // residual per-cycle ripple on low bass notes.
        // NB: preview/monitor path ALSO runs this leveler now, so what you hear
        // in Rig Builder == the song. Cross-tone evenness (LUFS) is kept.
        const float rmsTauMs = 15.0f;   // faster loudness detector (was 30) — reacts ~2x quicker to a tone change
        const float rmsCoef = 1.0f - std::exp(-1.0f / std::max(1.0f, (rmsTauMs / 1000.0f) * float(sr)));

        const float* chData[2] = { nullptr, nullptr };
        const int chN = std::min(numChannels, 2);
        for (int ch = 0; ch < chN; ++ch)
            chData[ch] = buffer.getReadPointer(ch);

        double sumSq = 0.0;
        double rawSumSq = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            double sq = 0.0;
            double rawSq = 0.0;
            for (int ch = 0; ch < chN; ++ch)
            {
                const double x = double(chData[ch][i]);
                rawSq += x * x;                                // UNWEIGHTED power
                const double w = kRlb[ch].process(kPre[ch].process(x));
                sq += w * w;                                   // K-weighted power
            }
            sq /= double(std::max(1, chN));
            rawSq /= double(std::max(1, chN));
            sumSq += sq;
            rawSumSq += rawSq;
            msEnv += double(rmsCoef) * (sq - msEnv);
            rawMsEnv += double(rmsCoef) * (rawSq - rawMsEnv);
        }

        // SEED the detector on the FIRST block that actually carries signal: jump
        // msEnv straight to this block's mean square instead of letting the 30 ms
        // IIR ramp up from 0. Without this the first ~90 ms read artificially
        // quiet, so the AGC snapped to a big boost and the tone BLASTED then
        // dropped ("suena fuerte el bajo y luego se baja") on every song/tone
        // start. Seeding makes the very first gain decision use the real level.
        const double blockMs = sumSq / double(std::max(1, numSamples));
        const double rawBlockMs = rawSumSq / double(std::max(1, numSamples));
        if (! detectorSeeded && blockMs > 1.0e-9
            && (-0.691 + 10.0 * std::log10(blockMs)) >= gateDb)
        {
            msEnv = blockMs;
            rawMsEnv = rawBlockMs;
            detectorSeeded = true;
        }

        // BS.1770 loudness of the smoothed K-weighted power (mono-equivalent).
        const float loudnessLufs = (msEnv > 1.0e-12)
            ? float(-0.691 + 10.0 * std::log10(msEnv))
            : -120.0f;
        // RAW (unweighted) RMS — used for the noise GATE and the sub-heavy cap.
        const float rawRmsDb = (rawMsEnv > 1.0e-12)
            ? float(10.0 * std::log10(rawMsEnv)) : -120.0f;

        float wantedGainDb = currentGainDb;
        // GATE on the RAW level, NOT the K-weighted LUFS: white noise reads high
        // in K-weighting (the +4 dB high-shelf), so an LUFS gate let the idle
        // hiss through and the AGC then boosted it ("si no se toca nada, boostea
        // el ruido blanco"). Raw RMS reflects the true level. The floor must sit
        // BELOW real playing — a bass tone arrives QUIET at the leveler (the cab
        // IR attenuates it; the leveler exists to boost it up), so a too-high gate
        // ate the bass and it played "muy bajo". -44 dB clears the playing level
        // while still catching the (lower) idle noise floor.
        const float effGateDb = std::max(gateDb, -44.0f);
        const bool hasSignal = rawRmsDb >= effGateDb;

        // Warm-up: for the first ~45 ms of signal the 30 ms detector hasn't fully
        // settled, so its loudness reads low and the AGC would snap to a big boost
        // → the tone BLASTS then drops on song/tone start ("suena fuerte el bajo y
        // luego se baja"). So HOLD the gain and MUTE the output (`confidence`
        // below) until the detector is trustworthy, then fade in over ~20 ms at
        // the already-correct level. Net: a brief soft attack on the very first
        // note instead of a blast. (Per fresh plugin instance = per song load.)
        // Hold MUTED long enough for the 15 ms loudness detector to actually
        // settle (~3-4 taus) before the AGC snaps — otherwise it engages on an
        // unsettled (too-low) reading and BLASTS then drops on song/tone load.
        // 18 ms (1.2 taus) was too short and reintroduced the blast; 55 ms lands
        // on an accurate first reading → clean soft attack, the whole load muted.
        // NB: this is the LOAD warm-up only; the mid-song AGC speed (attack/
        // release/urgency below) is unchanged — switching tones stays snappy.
        const int kWarmupHold  = int(0.055 * sr);
        const int kWarmupFade  = std::max(1, int(0.022 * sr));
        const int kWarmupTotal = kWarmupHold + kWarmupFade;
        if (hasSignal && warmupSamples < kWarmupTotal)
            warmupSamples += numSamples;
        const bool warm = warmupSamples >= kWarmupHold;

        if (!hasSignal || !warm)
        {
            // Silence OR still warming the detector — HOLD the gain. The idle
            // noise floor is ducked by the separate OUTPUT GATE below (NOT by
            // dropping the AGC gain), so when playing resumes the level is already
            // correct and the note is never muted by a slow gain recovery.
            wantedGainDb = currentGainDb;
        }
        else
        {
            // Drive purely by perceived LOUDNESS (LUFS) to the target (param
            // name kept for state compat). We do NOT clamp the boost by the
            // instantaneous PEAK — that starved the boost on quiet/dynamic
            // high-crest tones ("the comp must raise the low volumes"); peaks
            // are caught by the brickwall limiter on the output instead.
            wantedGainDb = targetRmsDb - loudnessLufs;

            // SUB-HEAVY BOOST CAP (by mean RMS, NOT peak — so it doesn't starve
            // dynamic tones). K-weighting attenuates lows ~10-15 dB, so a bass-
            // only tone (fuzz-bass / synth bass) reads far quieter in LUFS than
            // its true RMS; the LUFS drive above would then boost it ~10 dB and
            // it DOMINATES over the backing ("synthbass suena MUY fuerte en la
            // canción"). Cap the boost so the UNWEIGHTED output RMS can't exceed
            // the target by more than RAW_MARGIN_DB. Full-range tones have raw
            // RMS within a few dB of their LUFS so they never hit this cap and
            // level by LUFS as before; only abnormally low-end-heavy tones (raw
            // RMS ≫ LUFS) get reined in. Tunable: lower margin = tamer bass.
            constexpr float RAW_MARGIN_DB = 6.0f;
            const float rawCapGainDb = (targetRmsDb + RAW_MARGIN_DB) - rawRmsDb;
            wantedGainDb = std::min(wantedGainDb, rawCapGainDb);

            wantedGainDb = juce::jlimit(-maxCutDb, maxBoostDb, wantedGainDb);
        }

        if (hasSignal && warm && !levelInitialized)
        {
            currentGainDb = juce::jlimit(-maxCutDb, maxBoostDb, wantedGainDb);
            currentGain = juce::Decibels::decibelsToGain(currentGainDb);   // AGC gain only (makeup applied later)
            levelInitialized = true;
        }

        // Output confidence: 0 while the detector warms up (output muted), then
        // ramps to 1 over kWarmupFade once the gain is trustworthy.
        const float confidence = juce::jlimit(0.0f, 1.0f,
            float(warmupSamples - kWarmupHold) / float(kWarmupFade));

        // Cutting reacts fast, boosting is slower (avoids audible fade-in).
        const bool cutting = wantedGainDb < currentGainDb;
        const float baseMs = cutting ? attackMs : releaseMs;

        // Adaptive convergence: snap fast on a BIG sustained level change — a
        // tone switch (e.g. drive -> clean), where the old fixed 120 ms boost
        // made the new tone fade in quietly over ~half a second. Small drift
        // keeps the slow time so musical dynamics aren't flattened (no
        // pumping). urgency: 0 at <=3 dB gap, ramps to 1 at >=12 dB.
        const float gapDb = std::abs(wantedGainDb - currentGainDb);
        // Reach full-speed sooner (urgency hits 1 by ~6 dB instead of 12) and snap
        // harder on a change, so a tone switch lands at level almost immediately.
        const float urgency = juce::jlimit(0.0f, 1.0f, (gapDb - 1.0f) / 5.0f);
        const float fastMs = 8.0f;
        const float timeMs = baseMs * (1.0f - urgency) + fastMs * urgency;

        const float blockSeconds = float(numSamples / sr);
        const float alpha = 1.0f - std::exp(-blockSeconds / std::max(0.001f, timeMs / 1000.0f));

        currentGainDb += (wantedGainDb - currentGainDb) * juce::jlimit(0.0f, 1.0f, alpha);
        currentGainDb = juce::jlimit(-maxCutDb, maxBoostDb, currentGainDb);

        // ── Idle-noise GATE (separate from the AGC) ──────────────────────────
        // The AGC HOLDS its boost during silence (above), which alone would keep
        // amplifying the idle noise floor. So duck the OUTPUT with a real noise
        // gate that OPENS fast on the CURRENT block's level (a note attack passes
        // un-muted) and CLOSES with a 300 ms hold + slow release on sustained
        // silence. Being a separate output multiplier — not the AGC gain — note
        // onsets are NEVER muted by a slow gain recovery (the bug with the old
        // release-to-unity: "de repente el fuzz se mutea").
        const float instRawDb = (rawBlockMs > 1.0e-12)
            ? float(10.0 * std::log10(rawBlockMs)) : -120.0f;
        const bool gateOpenNow = instRawDb >= effGateDb;
        if (gateOpenNow) gateHoldSamples = int(0.30 * sr);             // 300 ms hold
        else if (gateHoldSamples > 0) gateHoldSamples = std::max(0, gateHoldSamples - numSamples);
        const float gateTarget = (gateOpenNow || gateHoldSamples > 0) ? 1.0f : 0.0f;
        const float gateAtkCoef = 1.0f - std::exp(-1.0f / std::max(1.0f, 0.003f * float(sr)));  // ~3 ms open
        const float gateRelCoef = 1.0f - std::exp(-1.0f / std::max(1.0f, 0.250f * float(sr)));  // ~250 ms close

        // Order matters: AGC (loudness normalize) -> brickwall limiter on the
        // NORMALIZED signal -> makeup (Output Trim, the user's loudness) LAST.
        // The limiter must sit BEFORE the makeup; if it sits after, it claws the
        // makeup straight back down to the ceiling and everything plays quiet.
        // So `currentGain`/`nextAgcGain` track the AGC gain WITHOUT the makeup;
        // the limiter caps the normalized peaks; `makeupGain` is applied clean on
        // top (loud, the way the Chain-volume knob intends).
        const float nextAgcGain = juce::Decibels::decibelsToGain(currentGainDb);
        const float makeupGain  = juce::Decibels::decibelsToGain(trimDb);
        const float ceilLin = juce::Decibels::decibelsToGain(ceilingDb);
        const float relCoef = 1.0f - std::exp(-1.0f / std::max(1.0f, 0.080f * float(sr)));
        const float invN = numSamples > 1 ? 1.0f / float(numSamples - 1) : 0.0f;
        float* wch[2] = { nullptr, nullptr };
        for (int ch = 0; ch < chN; ++ch) wch[ch] = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float gAgc = currentGain + (nextAgcGain - currentGain) * (float(i) * invN);
            float pk = 0.0f;
            for (int ch = 0; ch < chN; ++ch)
                pk = std::max(pk, std::abs(wch[ch][i]) * gAgc);   // peak at the normalized (pre-makeup) stage
            const float need = (pk > ceilLin && pk > 0.0f) ? (ceilLin / pk) : 1.0f;
            if (need < limGain) limGain = need;                       // instant attack: no overshoot
            else                limGain += relCoef * (need - limGain); // slow release
            const float gc = (gateTarget > gateGain) ? gateAtkCoef : gateRelCoef;
            gateGain += gc * (gateTarget - gateGain);                  // fast-open / slow-close noise gate
            const float tot = gAgc * limGain * makeupGain * confidence * gateGain;  // ...makeup, warm-up fade, gate, last
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer(ch)[i] *= tot;
        }

        currentGain = nextAgcGain;   // currentGain now tracks the AGC gain (no makeup)
    }

    bool hasEditor() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override
    {
        return nullptr;
    }

    void getStateInformation(juce::MemoryBlock& destData) override
    {
        auto state = apvts.copyState();
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        copyXmlToBinary(*xml, destData);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

        if (xml != nullptr)
        {
            auto state = juce::ValueTree::fromXml(*xml);
            if (state.isValid())
                apvts.replaceState(state);
        }
    }

private:
    juce::AudioProcessorValueTreeState apvts;

    double sr = 48000.0;
    float currentGainDb = 0.0f;
    float currentGain = 1.0f;
    float limGain = 1.0f;   // brickwall peak-limiter gain (post-AGC)
    bool levelInitialized = false;
    bool detectorSeeded = false;   // jump msEnv to the real level on first signal
    int warmupSamples = 0;         // signal samples seen — gates the first gain decision
    int gateHoldSamples = 0;       // output noise-gate hold counter
    float gateGain = 1.0f;         // output noise-gate gain (fast attack / slow release)
    double msEnv = 0.0;   // running mean-square of the K-weighted signal (~30 ms)
    double rawMsEnv = 0.0; // running mean-square of the UNWEIGHTED signal — caps sub-heavy boost

    // ── ITU-R BS.1770 K-weighting (perceptual loudness) ───────────────────
    // Two biquads per channel: a high-shelf pre-filter + an RLB high-pass.
    // Coefficients are recomputed for the actual sample rate in prepareToPlay
    // (libebur128 derivation), so it's correct at 44.1 k and 48 k alike.
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;  // a0 normalized
        double z1 = 0.0, z2 = 0.0;
        inline double process(double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0.0; }
    };
    Biquad kPre[2], kRlb[2];   // stage 1 (shelf), stage 2 (high-pass) per channel

    void designKWeighting(double fs)
    {
        // Stage 1 — high-shelf pre-filter.
        // BASS-FAITHFUL TWEAK (was the standard BS.1770 +4 dB shelf): reduced to
        // +1.5 dB so the loudness measure depends much less on brightness. With
        // the full +4 dB, two bass tones through differently-voiced cabs measured
        // differently (the brighter one read louder → got boosted less → its bass
        // ended up quieter), so bass tones normalized to INCONSISTENT perceived
        // loudness. A flatter measure levels them by their (bass-dominated) energy.
        {
            const double f0 = 1681.974450955533, G = 1.5, Q = 0.7071752369554196;
            const double K = std::tan(juce::MathConstants<double>::pi * f0 / fs);
            const double Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            Biquad b;
            b.b0 = (Vh + Vb * K / Q + K * K) / a0;
            b.b1 = 2.0 * (K * K - Vh) / a0;
            b.b2 = (Vh - Vb * K / Q + K * K) / a0;
            b.a1 = 2.0 * (K * K - 1.0) / a0;
            b.a2 = (1.0 - K / Q + K * K) / a0;
            kPre[0] = b; kPre[1] = b;
        }
        // Stage 2 — RLB high-pass (numerator 1, -2, 1).
        // Corner lowered 38 → 22 Hz so the measure captures the bass fundamentals
        // (low E ≈ 41 Hz, low B ≈ 31 Hz) instead of attenuating them — another
        // source of bass-tone loudness inconsistency. Still cleans sub-22 Hz rumble.
        {
            const double f0 = 22.0, Q = 0.5003270373238773;
            const double K = std::tan(juce::MathConstants<double>::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            Biquad b;
            b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0;
            b.a1 = 2.0 * (K * K - 1.0) / a0;
            b.a2 = (1.0 - K / Q + K * K) / a0;
            kRlb[0] = b; kRlb[1] = b;
        }
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "target_rms_db",
            "Target RMS dB",
            juce::NormalisableRange<float>(-30.0f, -6.0f, 0.1f),
            -14.0f
        ));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "max_boost_db",
            "Max Boost dB",
            juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f),
            18.0f
        ));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "max_cut_db",
            "Max Cut dB",
            juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f),
            18.0f
        ));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "gate_db",
            "Gate dB",
            juce::NormalisableRange<float>(-80.0f, -30.0f, 0.1f),
            -50.0f
        ));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "attack_ms",
            "Attack ms",
            juce::NormalisableRange<float>(1.0f, 250.0f, 1.0f),
            12.0f
        ));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "release_ms",
            "Release ms",
            juce::NormalisableRange<float>(20.0f, 1000.0f, 1.0f),
            120.0f
        ));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "ceiling_db",
            "Ceiling dB",
            juce::NormalisableRange<float>(-12.0f, -0.1f, 0.1f),
            -1.0f
        ));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "trim_db",
            "Output Trim dB",
            juce::NormalisableRange<float>(-24.0f, 18.0f, 0.1f),
            0.0f
        ));

        return { params.begin(), params.end() };
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RBFinalLevelerAudioProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RBFinalLevelerAudioProcessor();
}
