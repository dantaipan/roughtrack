#include "TestHelpers.h"

#include "roughtrack/AudioEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace roughtrack;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

/** Delays and scales its input, so where a signal lands proves the routing. */
class TestPlugin final : public juce::AudioPluginInstance
{
public:
    TestPlugin (int delaySamples, float gainToApply, bool monoInput = false)
        : AudioPluginInstance (BusesProperties().withInput ("In", monoInput ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo())
                                                .withOutput ("Out", juce::AudioChannelSet::stereo())),
          delay (delaySamples),
          gain (gainToApply),
          mono (monoInput)
    {
        setLatencySamples (delay);
        line.resize ((size_t) juce::jmax (1, delay) * 2, 0.0f);
    }

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        return layout.getMainInputChannels() == (mono ? 1 : 2) && layout.getMainOutputChannels() == 2;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* samples = buffer.getWritePointer (channel);

            if (channel == 0)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    samples[i] = delayed (samples[i]) * gain;
            else if (mono)
                juce::FloatVectorOperations::copy (samples, buffer.getReadPointer (0), buffer.getNumSamples());
        }

        if (! mono && delay == 0)
            buffer.applyGain (1, 0, buffer.getNumSamples(), gain);
        else if (! mono)
            juce::FloatVectorOperations::copy (buffer.getWritePointer (1), buffer.getReadPointer (0), buffer.getNumSamples());
    }

    float delayed (float sample)
    {
        if (delay == 0)
            return sample;

        const auto out = line[readIndex];
        line[(readIndex + (size_t) delay) % line.size()] = sample;
        readIndex = (readIndex + 1) % line.size();
        return out;
    }

    void getStateInformation (juce::MemoryBlock& block) override
    {
        block.replaceAll (&gain, sizeof (gain));
    }

    void setStateInformation (const void* data, int size) override
    {
        if (size == (int) sizeof (gain))
            std::memcpy (&gain, data, sizeof (gain));
    }

    void fillInPluginDescription (juce::PluginDescription& description) const override
    {
        description.name = "Test Plugin";
        description.pluginFormatName = "Test";
        description.fileOrIdentifier = "test";
    }

    const juce::String getName() const override { return "Test Plugin"; }
    void prepareToPlay (double, int) override { ++numPrepares; }
    void releaseResources() override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    int numPrepares = 0;

private:
    int delay;
    float gain;
    bool mono;
    std::vector<float> line;
    size_t readIndex = 0;
};

juce::File writePulse (const juce::File& directory, const juce::String& name, int pulsePosition)
{
    juce::AudioBuffer<float> take (1, 48000);
    take.clear();
    take.setSample (0, pulsePosition, 0.8f);

    const auto file = directory.getChildFile (name);
    test::writeWav (file, take, sampleRate);
    return file;
}

/** Runs the engine and records what reaches each output. */
struct Rig
{
    Rig()
    {
        test::useTracks (engine);
        engine.setSessionDirectory (temp.directory);
        engine.prepare (sampleRate, blockSize);
    }

    void run (int numBlocks)
    {
        const float* inputs[] { input.data() };

        for (int block = 0; block < numBlocks; ++block)
        {
            float* outputs[] { left.data(), right.data() };
            engine.processBlock (inputs, 1, outputs, 2, blockSize);
            outLeft.insert (outLeft.end(), left.begin(), left.end());
            outRight.insert (outRight.end(), right.begin(), right.end());
        }
    }

    static int peakOf (const std::vector<float>& samples)
    {
        return (int) std::distance (samples.begin(), std::max_element (samples.begin(), samples.end()));
    }

    test::TempDirectory temp;
    AudioEngine engine;
    std::vector<float> input = std::vector<float> ((size_t) blockSize, 0.0f);
    std::vector<float> left = std::vector<float> ((size_t) blockSize);
    std::vector<float> right = std::vector<float> ((size_t) blockSize);
    std::vector<float> outLeft, outRight;
};

} // namespace

TEST_CASE ("Tracks stay aligned when only one has a plugin with latency")
{
    constexpr int pulse = 12000;
    constexpr int pluginDelay = 1000;

    Rig rig;

    REQUIRE (rig.engine.loadTake (0, writePulse (rig.temp.directory, "a.wav", pulse), 0));
    REQUIRE (rig.engine.loadTake (1, writePulse (rig.temp.directory, "b.wav", pulse), 0));
    rig.engine.setTrackPan (0, -1.0f);
    rig.engine.setTrackPan (1, 1.0f);

    REQUIRE (rig.engine.setInsert (0, 0, std::make_unique<TestPlugin> (pluginDelay, 1.0f)).wasOk());
    CHECK (rig.engine.getInsertLatency (0) == pluginDelay);

    REQUIRE (rig.engine.play());
    rig.run (300);

    CHECK (Rig::peakOf (rig.outLeft) == pulse);
    CHECK (Rig::peakOf (rig.outRight) == pulse);
}

TEST_CASE ("A drum track with a slow plugin stays in time with the tracks around it")
{
    constexpr int pluginDelay = 1000;

    Rig rig;

    test::writeTestDrums (rig.temp.directory.getChildFile ("Drums"));
    rig.engine.setDrumFolder (rig.temp.directory.getChildFile ("Drums"));
    REQUIRE (rig.engine.loadDrums().wasOk());

    rig.engine.activateTrack (7, "Drums", TrackType::drums);
    REQUIRE (rig.engine.setDrumParts (7, { { "beat", 1, 2, 0 } }).wasOk());
    REQUIRE (rig.engine.setInsert (7, 0, std::make_unique<TestPlugin> (pluginDelay, 1.0f)).wasOk());

    REQUIRE (rig.engine.play());
    rig.run (2000);

    CHECK (Rig::peakOf (rig.outLeft) == 96000);
}

TEST_CASE ("A bypassed plugin adds no latency and no processing")
{
    constexpr int pulse = 12000;

    Rig rig;

    REQUIRE (rig.engine.loadTake (0, writePulse (rig.temp.directory, "a.wav", pulse), 0));
    REQUIRE (rig.engine.setInsert (0, 1, std::make_unique<TestPlugin> (500, 0.5f)).wasOk());

    rig.engine.setInsertBypassed (0, 1, true);
    CHECK (rig.engine.getInsertLatency (0) == 0);

    REQUIRE (rig.engine.play());
    rig.run (300);

    CHECK (Rig::peakOf (rig.outLeft) == pulse);
    CHECK (rig.outLeft[pulse] == Catch::Approx (0.8f).margin (1.0e-3));
}

TEST_CASE ("Plugins run in slot order and their latencies add up")
{
    Rig rig;

    REQUIRE (rig.engine.setInsert (0, 0, std::make_unique<TestPlugin> (100, 0.5f)).wasOk());
    REQUIRE (rig.engine.setInsert (0, 3, std::make_unique<TestPlugin> (200, 0.5f)).wasOk());
    CHECK (rig.engine.getInsertLatency (0) == 300);

    rig.engine.setTrackArmed (0, true);
    rig.engine.setTrackMonitoring (0, true);
    std::fill (rig.input.begin(), rig.input.end(), 0.4f);
    rig.run (10);

    CHECK (rig.outLeft[299] == 0.0f);
    CHECK (rig.outLeft[300] == Catch::Approx (0.1f).margin (1.0e-4));
}

TEST_CASE ("The take stays dry while the player hears the plugin")
{
    Rig rig;

    REQUIRE (rig.engine.setInsert (0, 0, std::make_unique<TestPlugin> (0, 0.5f)).wasOk());
    rig.engine.setRecordingLatency (0);
    rig.engine.setTrackArmed (0, true);
    rig.engine.setTrackMonitoring (0, true);
    std::fill (rig.input.begin(), rig.input.end(), 0.4f);

    REQUIRE (rig.engine.record().wasOk());
    rig.run (100);
    rig.engine.stop();

    CHECK (rig.outLeft[100] == Catch::Approx (0.2f).margin (1.0e-4));

    const auto take = test::readAll (rig.engine.getTake (0).file);
    REQUIRE (take.getNumSamples() > 100);
    CHECK (take.getSample (0, 100) == Catch::Approx (0.4f).margin (1.0e-4));
}

TEST_CASE ("A mono-input plugin gets a mix of both sides and fills both outputs")
{
    Rig rig;

    REQUIRE (rig.engine.setInsert (0, 0, std::make_unique<TestPlugin> (0, 1.0f, true)).wasOk());
    rig.engine.setTrackArmed (0, true);
    rig.engine.setTrackMonitoring (0, true);
    std::fill (rig.input.begin(), rig.input.end(), 0.4f);
    rig.run (1);

    CHECK (rig.outLeft[0] == Catch::Approx (0.4f).margin (1.0e-4));
    CHECK (rig.outRight[0] == Catch::Approx (0.4f).margin (1.0e-4));
}

TEST_CASE ("Plugins can't be changed while the transport runs")
{
    Rig rig;

    REQUIRE (rig.engine.setInsert (0, 0, std::make_unique<TestPlugin> (0, 1.0f)).wasOk());
    REQUIRE (rig.engine.play());

    CHECK (rig.engine.setInsert (0, 1, std::make_unique<TestPlugin> (0, 1.0f)).failed());
    CHECK (rig.engine.removeInsert (0, 0).failed());
    CHECK (rig.engine.getInsert (0, 0) != nullptr);
    CHECK (rig.engine.getInsert (0, 1) == nullptr);

    rig.engine.stop();
    CHECK (rig.engine.removeInsert (0, 0).wasOk());
    CHECK (rig.engine.getInsert (0, 0) == nullptr);
}

TEST_CASE ("Saved inserts come back with their settings")
{
    Rig rig;

    REQUIRE (rig.engine.setInsert (2, 1, std::make_unique<TestPlugin> (0, 0.25f)).wasOk());
    rig.engine.setInsertBypassed (2, 1, true);

    const auto xml = rig.engine.createInsertsXml();
    const auto saved = parseSavedInserts (*xml);

    REQUIRE (saved.size() == 1);
    CHECK (saved[0].track == 2);
    CHECK (saved[0].slot == 1);
    CHECK (saved[0].bypassed);
    CHECK (saved[0].description.name == "Test Plugin");

    Rig restored;
    REQUIRE (restored.engine.setInsert (0, 0, std::make_unique<TestPlugin> (0, 1.0f), saved[0].state).wasOk());

    restored.engine.setTrackArmed (0, true);
    restored.engine.setTrackMonitoring (0, true);
    std::fill (restored.input.begin(), restored.input.end(), 0.4f);
    restored.run (1);

    CHECK (restored.outLeft[0] == Catch::Approx (0.1f).margin (1.0e-4));
}

TEST_CASE ("A new device prepares plugins already in place")
{
    Rig rig;

    auto plugin = std::make_unique<TestPlugin> (0, 1.0f);
    auto* raw = plugin.get();

    REQUIRE (rig.engine.setInsert (0, 0, std::move (plugin)).wasOk());
    const auto preparesBefore = raw->numPrepares;

    rig.engine.prepare (44100.0, 128);

    CHECK (raw->numPrepares == preparesBefore + 1);
    CHECK (raw->getSampleRate() == 44100.0);
}
