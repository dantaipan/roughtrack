#include "rifftrack/AudioEngine.h"
#include "rifftrack/Session.h"

#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace rifftrack;
using Catch::Approx;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

struct Rig
{
    Rig()
    {
        engine.setSessionDirectory (temp.directory);
        engine.prepare (sampleRate, blockSize);
        track = engine.addTrack ("Guitar");
        REQUIRE (track >= 0);
    }

    void run (int numBlocks, float inputLevel)
    {
        std::vector<float> input ((size_t) blockSize, inputLevel);
        const float* inputs[] { input.data() };
        float* outputs[] { left.data(), right.data() };

        for (int block = 0; block < numBlocks; ++block)
            engine.processBlock (inputs, 1, outputs, 2, blockSize);
    }

    static int blocksFor (double seconds) { return juce::roundToInt (seconds * sampleRate / blockSize); }

    test::TempDirectory temp;
    AudioEngine engine;
    int track = 0;
    std::vector<float> left = std::vector<float> ((size_t) blockSize), right = std::vector<float> ((size_t) blockSize);
};

} // namespace

TEST_CASE ("An armed track's input shows on its meter with monitoring off")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    rig.run (Rig::blocksFor (1.0), 0.5f);

    CHECK (rig.engine.takeTrackInputPeak (rig.track) == Approx (0.5f));
    CHECK (rig.engine.getTrackInputLevel (rig.track) == Approx (0.5f).margin (0.001f));
    CHECK (rig.engine.takeTrackPeak (rig.track) == 0.0f);
    CHECK (rig.left[0] == 0.0f);
}

TEST_CASE ("The input meter ignores the fader")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    rig.engine.setTrackMonitoring (rig.track, true);
    rig.engine.setTrackGain (rig.track, 0.1f);
    rig.run (Rig::blocksFor (1.0), 0.5f);

    CHECK (rig.engine.takeTrackInputPeak (rig.track) == Approx (0.5f));
    CHECK (rig.engine.getTrackInputLevel (rig.track) == Approx (0.5f).margin (0.001f));
    CHECK (rig.engine.getTrackLevel (rig.track) == Approx (0.05f).margin (0.001f));
}

TEST_CASE ("A stereo input meters both channels")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    rig.engine.setTrackInput (rig.track, 0, 2);

    std::vector<float> quiet ((size_t) blockSize, 0.1f), loud ((size_t) blockSize, 0.9f);
    const float* inputs[] { quiet.data(), loud.data() };
    float* outputs[] { rig.left.data(), rig.right.data() };

    rig.engine.processBlock (inputs, 2, outputs, 2, blockSize);

    CHECK (rig.engine.takeTrackInputPeak (rig.track) == Approx (0.9f));
}

TEST_CASE ("An unarmed track's input meter falls to nothing")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    rig.run (Rig::blocksFor (1.0), 0.5f);

    rig.engine.setTrackArmed (rig.track, false);
    rig.engine.takeTrackInputPeak (rig.track);
    rig.run (Rig::blocksFor (1.0), 0.5f);

    CHECK (rig.engine.takeTrackInputPeak (rig.track) == 0.0f);
    CHECK (rig.engine.getTrackInputLevel (rig.track) < 0.001f);
}

TEST_CASE ("Levels rise like a VU needle")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);

    rig.run (Rig::blocksFor (0.3), 0.5f);

    // Squared levels are 99% there at 300 ms, and that's about 99.5% of the level.
    CHECK (rig.engine.getTrackInputLevel (rig.track) == Approx (0.5f * 0.995f).margin (0.005f));
}

TEST_CASE ("The needle's rise doesn't depend on the buffer size")
{
    const auto levelAfter = [] (int size)
    {
        test::TempDirectory temp;
        AudioEngine engine;
        engine.setSessionDirectory (temp.directory);
        engine.prepare (sampleRate, size);

        const auto track = engine.addTrack ("Guitar");
        engine.setTrackArmed (track, true);

        std::vector<float> input ((size_t) size, 0.5f), left ((size_t) size), right ((size_t) size);
        const float* inputs[] { input.data() };
        float* outputs[] { left.data(), right.data() };

        for (int block = 0; block < 4800 / size; ++block)
            engine.processBlock (inputs, 1, outputs, 2, size);

        return engine.getTrackInputLevel (track);
    };

    CHECK (levelAfter (64) == Approx (levelAfter (512)).margin (0.01f));
}

TEST_CASE ("A muted track's output meter falls while its input meter keeps working")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    rig.engine.setTrackMonitoring (rig.track, true);
    rig.run (Rig::blocksFor (0.5), 0.5f);

    REQUIRE (rig.engine.getTrackLevel (rig.track) > 0.4f);

    rig.engine.setTrackMuted (rig.track, true);
    rig.run (Rig::blocksFor (1.0), 0.5f);

    CHECK (rig.engine.getTrackLevel (rig.track) < 0.001f);
    CHECK (rig.engine.getTrackInputLevel (rig.track) > 0.4f);
}

TEST_CASE ("The master level follows the mix after the master fader")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    rig.engine.setTrackMonitoring (rig.track, true);
    rig.engine.setMasterGain (0.5f);
    rig.run (Rig::blocksFor (1.0), 0.5f);

    CHECK (rig.engine.getMasterLevel() == Approx (0.25f).margin (0.001f));
}

TEST_CASE ("Polarity flips what's monitored, and leaves the take alone")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    rig.engine.setTrackMonitoring (rig.track, true);
    rig.engine.setTrackInverted (rig.track, true);
    rig.run (1, 0.5f);

    CHECK (rig.left[0] == Approx (-0.5f));
    CHECK (rig.right[0] == Approx (-0.5f));

    REQUIRE (rig.engine.record().wasOk());
    rig.run (100, 0.5f);
    rig.engine.stop();

    const auto clips = rig.engine.getClips (rig.track);

    REQUIRE (clips.size() == 1);

    const auto take = test::readAll (clips[0].file);

    REQUIRE (take.getNumSamples() > 0);
    CHECK (take.getSample (0, 0) == Approx (0.5f).margin (0.001f));
}

TEST_CASE ("Polarity flips playback too")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);

    REQUIRE (rig.engine.record().wasOk());
    rig.run (500, 0.5f);
    rig.engine.stop();
    rig.engine.setTrackArmed (rig.track, false);
    rig.engine.setPosition (0);
    rig.engine.setTrackInverted (rig.track, true);

    REQUIRE (rig.engine.play());

    auto lowest = 0.0f;

    for (int attempt = 0; attempt < 400; ++attempt)
    {
        rig.run (1, 0.0f);
        lowest = juce::jmin (lowest, rig.left[10]);
        juce::Thread::sleep (attempt < 20 ? 2 : 0);
    }

    CHECK (lowest == Approx (-0.5f).margin (0.01f));
}

TEST_CASE ("Polarity comes back with the session")
{
    Rig rig;
    rig.engine.setTrackInverted (rig.track, true);

    const auto xml = createSessionXml (rig.engine, rig.temp.directory);

    Rig other;
    other.engine.removeAllTracks();
    REQUIRE (applySessionXml (other.engine, *xml, rig.temp.directory).wasOk());
    CHECK (other.engine.getTrackSettings (rig.track).inverted);
}

TEST_CASE ("Sessions from before polarity open the right way up")
{
    Rig rig;
    const auto xml = createSessionXml (rig.engine, rig.temp.directory);

    for (auto* track : xml->getChildWithTagNameIterator ("TRACK"))
        track->removeAttribute ("inverted");

    Rig other;
    other.engine.removeAllTracks();
    REQUIRE (applySessionXml (other.engine, *xml, rig.temp.directory).wasOk());
    CHECK_FALSE (other.engine.getTrackSettings (rig.track).inverted);
}
