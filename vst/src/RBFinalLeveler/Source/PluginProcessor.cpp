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
        sigSamples = 0;
        sustainSamples = 0;
        fastLatch = false;
        gateHoldSamples = 0;
        gateGain = 1.0f;
        msEnv = 0.0;
        rawMsEnv = 0.0;
        noiseFloorMs = 1.0e-12;
        frameSum = 0.0;
        frameN = 0;
        frameDbMean = -60.0f;
        frameDbVar = 9.0f;     // start "not stationary" so nothing is learned as noise yet
        periodicBlocksInFrame = 0;
        blocksInFrame = 0;
        frameDbHist.fill(0.0f);
        frameHistPos = 0;
        frameHistCount = 0;
        gateOpenState = false;
        prevBlockDb = -120.0f;
        pitchVotes = 0;
        nacRing.fill(0.0f);
        nacPos = 0;
        nacReady = false;
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

        // Loudness detector: a running mean-square of the K-WEIGHTED signal
        // (ITU-R BS.1770), integrated per sample so it is independent of host
        // block size. K-weighting makes this measure PERCEIVED loudness (LUFS),
        // not raw RMS — so spectrally different tones level to equal perceived
        // loudness.
        // TIME CONSTANT = 400 ms (≈ BS.1770 "momentary"), NOT the old 15 ms:
        // a 15 ms detector drives the SHORT-TERM level to target, which makes
        // dense sustained material (fuzz/synth-bass walls) sit AT target
        // continuously while sparse material (fingerpicked acoustic) only
        // touches target at each pluck and decays in between — integrated, the
        // sparse tone plays several dB quieter ("Dust in the Wind muy bajo,
        // el synth-fuzz muy fuerte"). A 400 ms window spans pluck+decay cycles
        // so the AVERAGE loudness is driven to target for both. Tone-switch
        // reactivity is preserved by the SEED below (first-signal jump) and the
        // urgency snap in the gain follower (big gap → 8 ms convergence).
        // NB: preview/monitor path ALSO runs this leveler now, so what you hear
        // in Rig Builder == the song. Cross-tone evenness (LUFS) is kept.
        // TWO-SPEED: the first ~0.5 s of signal keeps the old fast 15 ms tau so
        // the level decision converges quickly from the block-sized seed (a
        // seed landing on a pluck attack would otherwise take ~1 s to correct
        // at 400 ms); after that the detector slows to the momentary window.
        const bool detectorSettling = sigSamples < int(0.5 * sr);
        const float rmsTauMs = detectorSettling ? 15.0f : 400.0f;
        const float rmsCoef = 1.0f - std::exp(-1.0f / std::max(1.0f, (rmsTauMs / 1000.0f) * float(sr)));

        const float* chData[2] = { nullptr, nullptr };
        const int chN = std::min(numChannels, 2);
        for (int ch = 0; ch < chN; ++ch)
            chData[ch] = buffer.getReadPointer(ch);

        // PRE-PASS: this block's raw (unweighted) power. Cheap, and needed UP
        // FRONT so the gate decision and the detector freeze below have no
        // one-block lag on note attacks.
        double preRawSum = 0.0;
        for (int i = 0; i < numSamples; ++i)
            for (int ch = 0; ch < chN; ++ch)
            {
                const double x = double(chData[ch][i]);
                preRawSum += x * x;
            }
        const double rawBlockMs = preRawSum / double(std::max(1, numSamples) * std::max(1, chN));
        const float instRawDb = (rawBlockMs > 1.0e-12)
            ? float(10.0 * std::log10(rawBlockMs)) : -120.0f;

        // Feed this block's mono into the periodicity window, then classify the
        // current ~43 ms as PITCHED (a note) or NOISE via the normalized
        // autocorrelation peak (NAC). Spectral flatness was tried first and
        // FAILED: post-cab idle hiss is heavily COLORED (energy packed into the
        // low bins) and reads exactly like a bass note's spectrum at this FFT
        // resolution, so a flatness gate either boosted dist-pedal hiss forever
        // ("se mantiene el ruido... suena como oscilando") or muted real notes.
        // Periodicity is the robust discriminator: any note — clean, distorted,
        // cab-colored, dense low harmonics, chords — repeats at its fundamental
        // lag; hiss of ANY color does not. Validated offline (sim, per-block
        // NAC): white noise ≤0.16, colored dist hiss ≤0.33, chords ≥0.34,
        // single notes ≥0.9 → threshold 0.35 + 2-of-3 vote below.
        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < chN; ++ch) mono += chData[ch][i];
            nacRing[(size_t) nacPos] = (chN > 0) ? mono / float(chN) : 0.0f;
            nacPos = (nacPos + 1) & (kNacN - 1);
            if (nacPos == 0) nacReady = true;
        }
        const bool pitched = nacReady && (computeNac() > kNacThresh);

        // ── NOISE-FLOOR TRACKER ──────────────────────────────────────────────
        // The chain's idle floor (hiss/hum, any level — stacked fuzz can idle at
        // -30 dB) is learned by STATIONARITY over 100 ms frames: FALL fast
        // toward any quieter frame; RISE toward frames that look like NOISE.
        // 100 ms frames (not 5 ms blocks): block-level dB variance is bandwidth-
        // dependent — narrowband post-cab hiss has few independent samples per
        // block and looks "unstable" at block scale; frames average that out
        // for every noise color while playing still swings frame-to-frame.
        // Rise paths: (a) NON-periodic + fairly stationary → normal hiss learn;
        // (b) PERIODIC but dead-steady over 4 s NET drift → hum/drone, learn it
        // too. The NET drift check (vs 4 s ago, not frame-to-frame deltas) is
        // what protects long sustained notes: a slow decay moves ~0.3 dB per
        // frame ("steady") but >2 dB over 4 s.
        frameSum += rawBlockMs * double(numSamples);
        frameN += numSamples;
        ++blocksInFrame;
        if (pitched) ++periodicBlocksInFrame;
        if (frameN >= int(0.100 * sr))
        {
            const double frameMs = frameSum / double(frameN);
            const float frameDb = (frameMs > 1.0e-12) ? float(10.0 * std::log10(frameMs)) : -120.0f;
            const bool framePeriodic = periodicBlocksInFrame * 3 > blocksInFrame;   // >1/3 of blocks
            const float oldFrameDb = frameDbHist[(size_t) frameHistPos];
            const bool histFull = frameHistCount >= (int) frameDbHist.size();
            frameDbHist[(size_t) frameHistPos] = frameDb;
            frameHistPos = (frameHistPos + 1) % (int) frameDbHist.size();
            if (frameHistCount < (int) frameDbHist.size()) ++frameHistCount;
            const bool steady4s = histFull && std::abs(frameDb - oldFrameDb) < 2.0f;
            if (frameDb < frameDbMean - 8.0f) { frameDbMean = frameDb; frameDbVar = 4.0f; }
            const float cStat = 1.0f - std::exp(-0.100f / 1.0f);   // ~1 s stats horizon
            frameDbMean += cStat * (frameDb - frameDbMean);
            const float dev = frameDb - frameDbMean;
            frameDbVar += cStat * (dev * dev - frameDbVar);
            const bool noiseLike = (!framePeriodic && frameDbVar < 1.0f)
                                || (framePeriodic && steady4s && frameDbVar < 1.0f);
            if (frameMs < noiseFloorMs)
                noiseFloorMs += double(1.0f - std::exp(-0.100f / 0.20f)) * (frameMs - noiseFloorMs);
            else if (noiseLike)
                noiseFloorMs += double(1.0f - std::exp(-0.100f / 1.5f)) * (frameMs - noiseFloorMs);
            frameSum = 0.0; frameN = 0; blocksInFrame = 0; periodicBlocksInFrame = 0;
        }
        const float floorDb = (noiseFloorMs > 1.0e-12)
            ? float(10.0 * std::log10(noiseFloorMs)) : -120.0f;

        // Adaptive gate with HYSTERESIS: open at floor+8, close at floor+4, so
        // hiss riding near one threshold can't flap the gate (the flapping +
        // 300 ms hold + slow release WAS the audible "oscilando"). Upper clamp
        // -20 (was -32): a stacked-dist chain can idle at -30 and the gate must
        // be allowed to sit above that; playing quieter than a -30 dB hiss is
        // masked by the hiss anyway. LOWER bound = the gate PARAM (not a fixed
        // -44): quiet chains (bare game-cab, no amp) sit below -44 and the host
        // bakes a lower Gate dB for them; the NAC pitch test keeps noise from
        // opening the gate at any level.
        const float openDb  = juce::jlimit(gateDb, -20.0f, floorDb + 8.0f);
        const float closeDb = openDb - 4.0f;
        const bool levelOpen = instRawDb >= (gateOpenState ? closeDb : openDb);
        gateOpenState = levelOpen;

        // 2-of-3-block pitch vote: a single-block NAC fluke on noise can't open
        // the gate; a single-block dip on a chord can't close it.
        pitchVotes = ((pitchVotes << 1) | (pitched ? 1 : 0)) & 0x7;
        const int nVotes = (pitchVotes & 1) + ((pitchVotes >> 1) & 1) + ((pitchVotes >> 2) & 1);
        const bool votedPitch = nVotes >= 2;

        // ATTACK-TRANSIENT override: a sudden level jump is a note onset (noise
        // floors don't step up 9 dB block-to-block) — open immediately, before
        // the 43 ms periodicity window has locked onto the new note, so attacks
        // and palm-muted chugs are never softened by NAC/vote lag.
        const bool transient = levelOpen && (instRawDb >= prevBlockDb + 9.0f);
        prevBlockDb = instRawDb;

        // ── DETECTOR (freeze during silence) ─────────────────────────────────
        // The K-filters run on every sample (they must stay warm), but the
        // loudness envelopes only INTEGRATE while this block carries signal.
        // Previously they kept integrating through silence, decayed to the
        // noise floor, and the AGC briefly over-boosted the first note after a
        // pause (~+4 dB for <200 ms). Frozen, the gain on resume is already
        // exact for the tone that was playing.
        // Gate/detector open only on a signal that is BOTH loud enough AND
        // pitched (a real note) — a loud but aperiodic idle hiss no longer opens
        // the gate or drives the AGC, so the noise floor stops "subiendo".
        const bool integrating = (levelOpen && votedPitch) || transient;
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
            if (integrating)
            {
                msEnv += double(rmsCoef) * (sq - msEnv);
                rawMsEnv += double(rmsCoef) * (rawSq - rawMsEnv);
            }
        }

        // SEED the detector on the FIRST block that actually carries signal: jump
        // msEnv straight to this block's mean square instead of letting the 30 ms
        // IIR ramp up from 0. Without this the first ~90 ms read artificially
        // quiet, so the AGC snapped to a big boost and the tone BLASTED then
        // dropped ("suena fuerte el bajo y luego se baja") on every song/tone
        // start. Seeding makes the very first gain decision use the real level.
        const double blockMs = sumSq / double(std::max(1, numSamples));
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
        // el ruido blanco"). Raw RMS reflects the true level. The threshold is
        // the ADAPTIVE hysteresis gate computed above (open floor+8 / close
        // floor+4, clamped [-44, -20]); `integrating` is this block's signal test.
        const bool hasSignal = integrating;

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
        if (hasSignal && sigSamples < int(0.5 * sr) + numSamples)
            sigSamples += numSamples;   // total signal seen — switches detector speed
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
            // Drive by perceived LOUDNESS (LUFS) to the target (param name
            // kept for state compat). We do NOT clamp the boost by the
            // instantaneous PEAK — that starved the boost on quiet/dynamic
            // high-crest tones; peaks are caught by the brickwall limiter on
            // the output instead.
            // NB: a density-compensated target (sparse material up, sustained
            // walls down) was tried here and REMOVED: song sections alternate
            // staccato/sustained, so the density measure moved DURING a song
            // and the gain audibly drifted ("se sigue bajando el volumen").
            // Any future per-material bias must be decided ONCE per tone (at
            // lock time), never tracked live.
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
            // Tightened 6 → 3 dB: with the standard K-weighting the LUFS drive
            // gives sub-heavy tones more boost than the flattened measure did,
            // and at raw = target+6 a fuzz synth-bass audibly dominated the mix
            // again ("el basssynth suena muy fuerte"). +3 still never binds for
            // full-range tones (their raw sits within ~2 dB of LUFS).
            constexpr float RAW_MARGIN_DB = 3.0f;
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

        // ── Gain scheduler: the per-tone gain is (almost) STATIC ────────────
        // The old follower (attack/release + urgency snap from a 1 dB gap)
        // TRACKED the loudness measure — with any detector window that means
        // riding musical dynamics: plucks/riff rests moved the measure a few
        // dB at note rate and the gain followed ("se baja y sube el volumen
        // todo el rato"). A tone's normalization gain should be decided once
        // and then barely move:
        //   gap ≤ 1.5 dB  → tau 8 s   (center on target imperceptibly)
        //   gap ~ 4.5 dB  → tau 2.5 s (gentle correction, still inaudible)
        //   gap > 4.5 dB SUSTAINED (0.3 s cutting / 0.75 s boosting) → LATCH
        //     fast (40 ms) until within 1.5 dB — a real level regime change
        //     (pickup/volume-knob change, drastic section), not one loud chord
        //     or one quiet bar. Cutting arms faster than boosting: too-loud is
        //     the worse artifact. (attackMs/releaseMs params kept for state
        //     compat; the schedule above replaces them.)
        juce::ignoreUnused(attackMs, releaseMs);
        const float gapSigned = wantedGainDb - currentGainDb;
        const float aGap = std::abs(gapSigned);
        if (hasSignal && warm && aGap > 4.5f)
            sustainSamples += numSamples;
        else
            sustainSamples = std::max(0, sustainSamples - 2 * numSamples);
        const int needSustain = int((gapSigned < 0.0f ? 0.30 : 0.75) * sr);
        if (!fastLatch && sustainSamples >= needSustain) fastLatch = true;
        if (fastLatch && aGap < 1.5f) { fastLatch = false; sustainSamples = 0; }
        float timeMs;
        if (fastLatch) {
            timeMs = 40.0f;
        } else {
            const float t = juce::jlimit(0.0f, 1.0f, (aGap - 1.5f) / 3.0f);
            timeMs = 8000.0f * (1.0f - t) + 2500.0f * t;
        }
        // First ~1 s of signal: the seed + fast detector are still locking the
        // tone's level — keep the follower quick so the song starts at the
        // right gain, THEN freeze into the slow schedule above.
        if (sigSamples < int(1.0 * sr))
            timeMs = std::min(timeMs, 150.0f);

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
        const bool gateOpenNow = integrating;   // same instantaneous raw-level test
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
    int sigSamples = 0;            // total signal seen — two-speed detector switch
    int sustainSamples = 0;        // how long the gain gap has stayed large
    bool fastLatch = false;        // latched fast re-convergence (regime change)
    int gateHoldSamples = 0;       // output noise-gate hold counter
    float gateGain = 1.0f;         // output noise-gate gain (fast attack / slow release)
    double msEnv = 0.0;   // running mean-square of the K-weighted signal (400 ms; frozen in silence)
    double rawMsEnv = 0.0; // running mean-square of the UNWEIGHTED signal — caps sub-heavy boost
    double noiseFloorMs = 1.0e-12;  // tracked idle-noise power (mean-square)

    // 100 ms frame accumulator + stats for the stationarity floor learner
    double frameSum = 0.0;
    int    frameN = 0;
    float  frameDbMean = -60.0f;    // EMA of frame level (dB), ~1 s horizon
    float  frameDbVar = 9.0f;       // EMA variance: hiss is stationary, playing is not
    int    periodicBlocksInFrame = 0, blocksInFrame = 0;
    std::array<float, 40> frameDbHist {};   // last 4 s of frame levels (net-drift check)
    int    frameHistPos = 0, frameHistCount = 0;
    bool   gateOpenState = false;   // gate level-hysteresis state
    float  prevBlockDb = -120.0f;   // attack-transient detector
    int    pitchVotes = 0;          // last-3-blocks pitched bitmask (2-of-3 vote)

    // ── Note-vs-noise gate (periodicity / NAC) ────────────────────────────────
    // A level gate can't tell a LOUD idle hiss from a real note. A note — even
    // distorted, cab-colored, dense low harmonics — is PERIODIC at its
    // fundamental lag; hiss of ANY color is not. (Spectral flatness was tried
    // and failed: post-cab hiss is colored into the low bins and reads like a
    // bass note at 1024-bin resolution.) NAC = peak normalized autocorrelation
    // over 30 Hz..1.2 kHz fundamental lags, computed by FFT on a ~43 ms mono
    // window. Validated offline: white ≤0.16, colored dist hiss ≤0.33, chords
    // ≥0.34, notes ≥0.9 → threshold 0.35 (+ 2-of-3 vote in processBlock).
    static constexpr int kNacN = 2048;                 // ~43 ms @ 48k (reaches 30 Hz lags)
    static constexpr int kNacFft = 4096;               // zero-padded → linear autocorr
    static constexpr float kNacThresh = 0.35f;
    std::array<float, kNacN> nacRing {};               // rolling mono window
    std::array<float, kNacFft> acRe {}, acIm {};       // FFT work buffers
    int  nacPos = 0;
    bool nacReady = false;                             // ring filled at least once

    // In-place iterative radix-2 FFT (N a power of two); real input via re[], im[]=0.
    static void radix2Fft(float* re, float* im, int n)
    {
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1) {
            const float ang = -2.0f * juce::MathConstants<float>::pi / float(len);
            const float wr = std::cos(ang), wi = std::sin(ang);
            for (int i = 0; i < n; i += len) {
                float cr = 1.0f, ci = 0.0f;
                for (int k = 0; k < len / 2; ++k) {
                    const int a = i + k, b = a + len / 2;
                    const float xr = re[b] * cr - im[b] * ci;
                    const float xi = re[b] * ci + im[b] * cr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr;        im[a] += xi;
                    const float ncr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr; cr = ncr;
                }
            }
        }
    }
    // Peak normalized autocorrelation (NAC) of the rolling mono window over
    // musical fundamental lags. Autocorr via Wiener–Khinchin: FFT → |X|² → FFT
    // again (the power spectrum is real+even, so a forward FFT equals the
    // inverse up to a scale that cancels in the r[lag]/r[0] ratio). The window
    // is zero-padded 2× so the circular autocorr is the LINEAR one.
    float computeNac()
    {
        double mean = 0.0;
        for (int i = 0; i < kNacN; ++i) mean += nacRing[(size_t) i];
        mean /= double(kNacN);
        for (int i = 0; i < kNacN; ++i) {
            const int idx = (nacPos + i) & (kNacN - 1);        // oldest → newest
            acRe[(size_t) i] = nacRing[(size_t) idx] - float(mean);
        }
        std::fill(acRe.begin() + kNacN, acRe.end(), 0.0f);
        std::fill(acIm.begin(), acIm.end(), 0.0f);
        radix2Fft(acRe.data(), acIm.data(), kNacFft);
        for (int k = 0; k < kNacFft; ++k) {
            const double p = double(acRe[(size_t) k]) * acRe[(size_t) k]
                           + double(acIm[(size_t) k]) * acIm[(size_t) k];
            acRe[(size_t) k] = float(p);
            acIm[(size_t) k] = 0.0f;
        }
        radix2Fft(acRe.data(), acIm.data(), kNacFft);
        const double r0 = double(acRe[0]);
        if (r0 <= 1.0e-9)
            return 0.0f;
        const int minLag = std::max(24, int(sr / 1200.0));        // ≤1.2 kHz fundamental
        const int maxLag = std::min(kNacN - 256, int(sr / 30.0)); // ≥30 Hz fundamental
        float best = 0.0f;
        for (int lag = minLag; lag <= maxLag; ++lag) {
            // Unbiased normalization for the shrinking overlap, CAPPED at 1.5:
            // far-lag estimates are noisy and an uncapped correction inflates
            // random noise peaks above the pitch threshold.
            const float unb = float(kNacN) / float(kNacN - lag);
            const float v = float(double(acRe[(size_t) lag]) / r0) * std::min(unb, 1.5f);
            if (v > best) best = v;
        }
        return best;
    }

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
        // Stage 1 — high-shelf pre-filter, STANDARD BS.1770 (+4 dB).
        // A "bass-faithful" flattened variant (+1.5 dB shelf, 22 Hz corner) was
        // tried here to keep two differently-voiced bass tones at equal bass
        // energy — but it made the drive measure diverge from TRUE perceived
        // loudness: sim archetype check showed bright tones landing ~-13.8
        // real LUFS and sub-heavy tones ~-17.1 (≈4 dB spread → "unos tonos
        // suenan más fuerte y otros más bajo"). With the standard weighting the
        // same archetypes land within 0.5 dB of each other. Equal true LUFS is
        // what "same volume" means across tones; the sub-dominance guard stays
        // in the RAW_MARGIN_DB cap in processBlock.
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
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
        // Stage 2 — RLB high-pass (numerator 1, -2, 1), standard 38 Hz corner
        // (see the stage-1 note: the 22 Hz "bass-faithful" corner skewed the
        // measure away from real perceived loudness).
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
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
