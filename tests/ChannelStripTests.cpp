#include "TestHelpers.h"

#include "rifftrack/Session.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <thread>

using namespace rifftrack;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

const juce::File examples (RIFFTRACK_NAM_EXAMPLES);

/** Runs a signal through a track that's monitored, and returns the left output. */
struct Rig
{
    Rig()
    {
        test::useTracks (engine);
        engine.setSessionDirectory (temp.directory);
        engine.prepare (sampleRate, blockSize);
        engine.setRecordingLatency (0);
        engine.setTrackArmed (0, true);
        engine.setTrackMonitoring (0, true);
    }

    template <typename Generator>
    std::vector<float> run (int numSamples, Generator&& generator)
    {
        std::vector<float> input ((size_t) blockSize), left ((size_t) blockSize), right ((size_t) blockSize), out;
        const float* inputs[] { input.data() };

        for (int done = 0; done < numSamples; done += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
                input[(size_t) i] = generator (done + i);

            float* outputs[] { left.data(), right.data() };
            engine.processBlock (inputs, 1, outputs, 2, blockSize);
            out.insert (out.end(), left.begin(), left.end());
        }

        return out;
    }

    /** Level of a sine after the filters have settled. */
    float levelAt (double frequency)
    {
        const auto out = run (48000, [frequency] (int i)
        {
            return 0.25f * (float) std::sin (2.0 * std::numbers::pi * frequency * i / sampleRate);
        });

        return *std::max_element (out.begin() + 24000, out.end());
    }

    test::TempDirectory temp;
    AudioEngine engine;
};

float decibels (float level)
{
    return juce::Decibels::gainToDecibels (level / 0.25f);
}

auto& channelOf (Rig& rig)
{
    return rig.engine.getChannel (0);
}

} // namespace

TEST_CASE ("With everything off the signal passes untouched")
{
    Rig rig;
    const auto out = rig.run (640, [] (int) { return 0.25f; });

    CHECK (out[300] == Catch::Approx (0.25f));
}

TEST_CASE ("The high pass removes rumble and leaves the guitar")
{
    Rig rig;
    channelOf (rig).setParameter (ChannelParameter::eqEnabled, 1.0f);
    channelOf (rig).setParameter (ChannelParameter::highPass, 200.0f);

    CHECK (decibels (rig.levelAt (50.0)) < -20.0f);
    CHECK (decibels (rig.levelAt (2000.0)) == Catch::Approx (0.0f).margin (0.5));
}

TEST_CASE ("Each EQ band moves its own part of the spectrum")
{
    Rig rig;
    auto& channel = channelOf (rig);
    channel.setParameter (ChannelParameter::eqEnabled, 1.0f);

    SECTION ("low")
    {
        channel.setParameter (ChannelParameter::lowGain, 12.0f);
        CHECK (decibels (rig.levelAt (40.0)) == Catch::Approx (12.0f).margin (1.0));
        CHECK (decibels (rig.levelAt (3000.0)) == Catch::Approx (0.0f).margin (0.5));
    }

    SECTION ("mid")
    {
        channel.setParameter (ChannelParameter::midGain, -12.0f);
        channel.setParameter (ChannelParameter::midFrequency, 1000.0f);
        CHECK (decibels (rig.levelAt (1000.0)) == Catch::Approx (-12.0f).margin (0.5));
        CHECK (decibels (rig.levelAt (100.0)) == Catch::Approx (0.0f).margin (1.0));
    }

    SECTION ("high")
    {
        channel.setParameter (ChannelParameter::highGain, 12.0f);
        CHECK (decibels (rig.levelAt (15000.0)) == Catch::Approx (12.0f).margin (1.0));
        CHECK (decibels (rig.levelAt (200.0)) == Catch::Approx (0.0f).margin (0.5));
    }
}

TEST_CASE ("A setting left off has no effect")
{
    Rig rig;
    channelOf (rig).setParameter (ChannelParameter::lowGain, 12.0f);

    CHECK (decibels (rig.levelAt (40.0)) == Catch::Approx (0.0f).margin (0.1));
}

TEST_CASE ("The compressor turns loud signals down by its ratio")
{
    Rig rig;
    auto& channel = channelOf (rig);
    channel.setParameter (ChannelParameter::compressorEnabled, 1.0f);
    channel.setParameter (ChannelParameter::compressorThreshold, -30.0f);
    channel.setParameter (ChannelParameter::compressorRatio, 4.0f);

    // 0.25 is 12 dB, or 18 dB over the threshold, so 4:1 leaves 13.5 dB of reduction.
    CHECK (decibels (rig.levelAt (1000.0)) == Catch::Approx (-13.5f).margin (1.5));

    channel.setParameter (ChannelParameter::compressorMakeup, 6.0f);
    CHECK (decibels (rig.levelAt (1000.0)) == Catch::Approx (-7.5f).margin (1.5));
}

TEST_CASE ("NAM captures load in every format")
{
    for (const auto* name : { "wavenet.nam", "lstm.nam", "A2.nam", "slimmable_container.nam", "wavenet_a1_standard.nam" })
    {
        INFO (name);
        const auto loaded = AmpModel::load (examples.getChildFile (name), sampleRate, blockSize);

        REQUIRE (loaded.model != nullptr);

        std::vector<float> block ((size_t) blockSize, 0.1f);

        for (int i = 0; i < 100; ++i)
            loaded.model->process (block.data(), blockSize);

        CHECK (std::isfinite (block[10]));
    }
}

TEST_CASE ("A file that isn't a capture is refused with a reason")
{
    test::TempDirectory temp;
    const auto file = temp.directory.getChildFile ("broken.nam");
    file.replaceWithText ("not a model");

    const auto loaded = AmpModel::load (file, sampleRate, blockSize);

    CHECK (loaded.model == nullptr);
    CHECK (loaded.error.isNotEmpty());
}

TEST_CASE ("A capture made at another sample rate is refused")
{
    const auto loaded = AmpModel::load (examples.getChildFile ("wavenet.nam"), 44100.0, blockSize);

    CHECK (loaded.model == nullptr);
    CHECK (loaded.error.contains ("Hz"));
}

TEST_CASE ("A device change to the wrong rate stops the amp, not the track")
{
    Rig rig;
    REQUIRE (rig.engine.loadAmp (0, examples.getChildFile ("wavenet.nam")).wasOk());

    rig.engine.prepare (44100.0, blockSize);
    const auto out = rig.run (640, [] (int) { return 0.25f; });

    CHECK (out[300] == Catch::Approx (0.25f));
}

TEST_CASE ("An amp changes the sound, and the take stays dry")
{
    Rig rig;
    REQUIRE (rig.engine.loadAmp (0, examples.getChildFile ("wavenet_a1_standard.nam")).wasOk());

    REQUIRE (rig.engine.record().wasOk());

    const auto tone = [] (int i) { return 0.25f * (float) std::sin (2.0 * std::numbers::pi * 220.0 * i / sampleRate); };
    const auto out = rig.run (9600, tone);

    rig.engine.stop();

    auto difference = 0.0f;

    for (int i = 4800; i < 9600; ++i)
        difference = juce::jmax (difference, std::abs (out[(size_t) i] - tone (i)));

    CHECK (difference > 0.01f);

    const auto take = test::readAll (rig.engine.getTake (0).file);
    REQUIRE (take.getNumSamples() > 4800);

    for (int i = 0; i < 4800; i += 97)
        REQUIRE (take.getSample (0, i) == Catch::Approx (tone (i)).margin (1.0e-4));
}

TEST_CASE ("Switching the amp off leaves the signal dry")
{
    Rig rig;
    REQUIRE (rig.engine.loadAmp (0, examples.getChildFile ("wavenet.nam")).wasOk());
    channelOf (rig).setParameter (ChannelParameter::ampEnabled, 0.0f);

    CHECK (rig.run (640, [] (int) { return 0.25f; })[300] == Catch::Approx (0.25f));
}

TEST_CASE ("A cabinet IR shapes the sound with no latency")
{
    Rig rig;

    juce::AudioBuffer<float> impulseResponse (1, 2400);
    impulseResponse.clear();
    impulseResponse.setSample (0, 0, 1.0f);
    impulseResponse.setSample (0, 480, 0.5f);

    const auto file = rig.temp.directory.getChildFile ("cab.wav");
    test::writeWav (file, impulseResponse, sampleRate);

    REQUIRE (channelOf (rig).loadCabinet (file).wasOk());

    // The IR loads on a background thread, and it's in place once silence stops
    // being passed straight through the crossfade.
    std::this_thread::sleep_for (std::chrono::milliseconds (500));
    rig.run (48000, [] (int) { return 0.0f; });
    std::this_thread::sleep_for (std::chrono::milliseconds (200));
    rig.run (48000, [] (int) { return 0.0f; });

    constexpr int pulse = 6400;
    const auto out = rig.run (19200, [] (int i) { return i == pulse ? 0.5f : 0.0f; });

    // Scaled to unit energy: 1 / sqrt (1 + 0.5 * 0.5) is 0.894.
    CHECK (out[(size_t) pulse - 1] == Catch::Approx (0.0f).margin (1.0e-3));
    CHECK (out[pulse] == Catch::Approx (0.447f).margin (0.005));
    CHECK (out[pulse + 480] == Catch::Approx (0.2236f).margin (0.005));
}

TEST_CASE ("The delay repeats exactly on time and fades by its feedback")
{
    Rig rig;
    auto& aux = rig.engine.getAux();

    channelOf (rig).setParameter (ChannelParameter::sendDelay, 1.0f);
    aux.setParameter (AuxParameter::delayTime, 100.0f);
    aux.setParameter (AuxParameter::delayFeedback, 0.5f);
    aux.setParameter (AuxParameter::reverbReturn, 0.0f);

    constexpr int pulse = 1000;
    const auto out = rig.run (24000, [] (int i) { return i == pulse ? 0.8f : 0.0f; });

    CHECK (out[pulse] == Catch::Approx (0.8f));
    CHECK (out[pulse + 4800] == Catch::Approx (0.8f));
    CHECK (out[pulse + 9600] == Catch::Approx (0.4f));
    CHECK (out[pulse + 4799] == 0.0f);
}

TEST_CASE ("Synced delay follows the tempo")
{
    Rig rig;
    auto& aux = rig.engine.getAux();

    rig.engine.setTempo (120.0);
    channelOf (rig).setParameter (ChannelParameter::sendDelay, 1.0f);
    aux.setParameter (AuxParameter::delaySync, 1.0f);
    aux.setParameter (AuxParameter::reverbReturn, 0.0f);

    constexpr int pulse = 1000;
    const auto out = rig.run (24000, [] (int i) { return i == pulse ? 0.8f : 0.0f; });

    // A dotted eighth at 120 BPM is 375 ms.
    CHECK (out[pulse + 18000] == Catch::Approx (0.8f));
}

TEST_CASE ("The reverb tail follows its send")
{
    Rig rig;
    const auto burst = [] (int i) { return i < 4800 ? 0.5f * (float) std::sin (i * 0.05) : 0.0f; };

    const auto tailPeak = [] (const std::vector<float>& out)
    {
        auto peak = 0.0f;

        for (size_t i = 6000; i < out.size(); ++i)
            peak = juce::jmax (peak, std::abs (out[i]));

        return peak;
    };

    CHECK (tailPeak (rig.run (24000, burst)) == 0.0f);

    channelOf (rig).setParameter (ChannelParameter::sendReverb, 1.0f);
    CHECK (tailPeak (rig.run (24000, burst)) > 0.01f);
}

TEST_CASE ("A muted track sends nothing to the effects")
{
    Rig rig;

    channelOf (rig).setParameter (ChannelParameter::sendDelay, 1.0f);
    rig.engine.getAux().setParameter (AuxParameter::delayTime, 10.0f);
    rig.engine.setTrackMuted (0, true);

    const auto out = rig.run (4800, [] (int) { return 0.5f; });

    CHECK (*std::max_element (out.begin(), out.end()) == 0.0f);
}

TEST_CASE ("Sends follow the fader")
{
    Rig rig;
    auto& aux = rig.engine.getAux();

    channelOf (rig).setParameter (ChannelParameter::sendDelay, 1.0f);
    aux.setParameter (AuxParameter::delayTime, 100.0f);
    aux.setParameter (AuxParameter::delayFeedback, 0.0f);
    rig.engine.setTrackGain (0, 0.5f);

    constexpr int pulse = 1000;
    const auto out = rig.run (9600, [] (int i) { return i == pulse ? 0.8f : 0.0f; });

    CHECK (out[pulse + 4800] == Catch::Approx (0.4f));
}

TEST_CASE ("Channel and aux settings come back with the session")
{
    Rig rig;
    auto& channel = channelOf (rig);

    REQUIRE (rig.engine.loadAmp (0, examples.getChildFile ("wavenet.nam")).wasOk());
    channel.setParameter (ChannelParameter::eqEnabled, 1.0f);
    channel.setParameter (ChannelParameter::midGain, -4.5f);
    channel.setParameter (ChannelParameter::sendReverb, 0.3f);
    rig.engine.getAux().setParameter (AuxParameter::delayTime, 250.0f);

    const auto xml = createSessionXml (rig.engine, rig.temp.directory);

    AudioEngine restored;
    restored.prepare (sampleRate, blockSize);

    REQUIRE (applySessionXml (restored, *xml, rig.temp.directory).wasOk());

    const auto& loaded = restored.getChannel (0);

    CHECK (loaded.hasAmp());
    CHECK (loaded.getAmpFile() == examples.getChildFile ("wavenet.nam"));
    CHECK (loaded.getParameter (ChannelParameter::eqEnabled) == 1.0f);
    CHECK (loaded.getParameter (ChannelParameter::midGain) == -4.5f);
    CHECK (loaded.getParameter (ChannelParameter::sendReverb) == Catch::Approx (0.3f));
    CHECK (restored.getAux().getParameter (AuxParameter::delayTime) == 250.0f);
}

TEST_CASE ("A session from before the channel strip still opens")
{
    juce::XmlElement xml ("RIFFTRACK_SESSION");
    xml.setAttribute ("tempo", 100.0);
    xml.createNewChildElement ("TRACK")->setAttribute ("index", 0);

    AudioEngine engine;
    test::useTracks (engine);
    engine.prepare (sampleRate, blockSize);

    REQUIRE (applySessionXml (engine, xml, juce::File()).wasOk());
    CHECK (engine.getChannel (0).getParameter (ChannelParameter::ampEnabled) == 1.0f);
}

TEST_CASE ("A missing capture is reported and the session still opens")
{
    Rig rig;
    const auto copy = rig.temp.directory.getChildFile ("gone.nam");
    examples.getChildFile ("wavenet.nam").copyFileTo (copy);

    REQUIRE (rig.engine.loadAmp (0, copy).wasOk());

    const auto xml = createSessionXml (rig.engine, rig.temp.directory);
    copy.deleteFile();

    AudioEngine restored;
    restored.prepare (sampleRate, blockSize);

    const auto result = applySessionXml (restored, *xml, rig.temp.directory);

    CHECK (result.failed());
    CHECK (result.getErrorMessage().contains ("gone.nam"));
}

TEST_CASE ("Resetting channels clears amps and settings")
{
    Rig rig;

    REQUIRE (rig.engine.loadAmp (0, examples.getChildFile ("wavenet.nam")).wasOk());
    channelOf (rig).setParameter (ChannelParameter::midGain, 5.0f);
    rig.engine.getAux().setParameter (AuxParameter::delayTime, 100.0f);

    rig.engine.resetChannels();

    CHECK_FALSE (channelOf (rig).hasAmp());
    CHECK (channelOf (rig).getParameter (ChannelParameter::midGain) == 0.0f);
    CHECK (rig.engine.getAux().getParameter (AuxParameter::delayTime) == 375.0f);
}
