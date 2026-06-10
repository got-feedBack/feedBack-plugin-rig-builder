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
        levelInitialized = false;
        msEnv = 0.0;
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

        // Loudness detector: a FIXED-TIME (~30 ms) running mean-square,
        // integrated per sample so it is independent of the host block size
        // (the old code averaged a single block, ~3-10 ms). 30 ms reacts
        // quickly to a tone change while the gain follower below smooths any
        // residual per-cycle ripple on low bass notes.
        const float rmsTauMs = 30.0f;
        const float rmsCoef = 1.0f - std::exp(-1.0f / std::max(1.0f, (rmsTauMs / 1000.0f) * float(sr)));

        float peak = 0.0f;
        const float* chData[2] = { nullptr, nullptr };
        const int chN = std::min(numChannels, 2);
        for (int ch = 0; ch < chN; ++ch)
            chData[ch] = buffer.getReadPointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            double sq = 0.0;
            for (int ch = 0; ch < chN; ++ch)
            {
                const float x = chData[ch][i];
                sq += double(x) * double(x);
                peak = std::max(peak, std::abs(x));
            }
            sq /= double(std::max(1, chN));
            msEnv += double(rmsCoef) * (sq - msEnv);
        }

        const float rms = std::sqrt(float(msEnv));

        const float rmsDb = juce::Decibels::gainToDecibels(rms, -120.0f);
        const float peakDb = juce::Decibels::gainToDecibels(peak, -120.0f);

        float wantedGainDb = currentGainDb;
        const bool hasSignal = rmsDb >= gateDb;

        if (!hasSignal)
        {
            // Do not chase silence/noise. Holding the learned correction avoids
            // the audible fade-in that happens when gain resets between notes.
            wantedGainDb = currentGainDb;
        }
        else
        {
            wantedGainDb = targetRmsDb - rmsDb;
            wantedGainDb = juce::jlimit(-maxCutDb, maxBoostDb, wantedGainDb);

            // Anti-clip: si el peak ya está cerca de 0 dBFS, limita el boost.
            const float maxAllowedByPeak = ceilingDb - peakDb;
            if (wantedGainDb > maxAllowedByPeak)
                wantedGainDb = maxAllowedByPeak;
        }

        if (hasSignal && !levelInitialized)
        {
            currentGainDb = juce::jlimit(-maxCutDb, maxBoostDb, wantedGainDb);
            currentGain = juce::Decibels::decibelsToGain(currentGainDb + trimDb);
            levelInitialized = true;
        }

        // Cutting reacts fast, boosting is slower (avoids audible fade-in).
        const bool cutting = wantedGainDb < currentGainDb;
        const float baseMs = cutting ? attackMs : releaseMs;

        // Adaptive convergence: snap fast on a BIG sustained level change — a
        // tone switch (e.g. drive -> clean), where the old fixed 120 ms boost
        // made the new tone fade in quietly over ~half a second. Small drift
        // keeps the slow time so musical dynamics aren't flattened (no
        // pumping). urgency: 0 at <=3 dB gap, ramps to 1 at >=12 dB.
        const float gapDb = std::abs(wantedGainDb - currentGainDb);
        const float urgency = juce::jlimit(0.0f, 1.0f, (gapDb - 3.0f) / 9.0f);
        const float fastMs = 20.0f;
        const float timeMs = baseMs * (1.0f - urgency) + fastMs * urgency;

        const float blockSeconds = float(numSamples / sr);
        const float alpha = 1.0f - std::exp(-blockSeconds / std::max(0.001f, timeMs / 1000.0f));

        currentGainDb += (wantedGainDb - currentGainDb) * juce::jlimit(0.0f, 1.0f, alpha);
        currentGainDb = juce::jlimit(-maxCutDb, maxBoostDb, currentGainDb);

        const float finalGainDb = currentGainDb + trimDb;
        const float nextGain = juce::Decibels::decibelsToGain(finalGainDb);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.applyGainRamp(ch, 0, numSamples, currentGain, nextGain);

        currentGain = nextGain;
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
    bool levelInitialized = false;
    double msEnv = 0.0;   // running mean-square (fixed ~100 ms loudness window)

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
            juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
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
