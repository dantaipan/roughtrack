#include "TestHelpers.h"

#include "rifftrack/AudioEngine.h"
#include "rifftrack/Midi.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace rifftrack;
using Catch::Approx;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

juce::File writeTake (const juce::File& directory, const juce::String& name, int length, float level, int pulseAt = -1)
{
    juce::AudioBuffer<float> take (1, length);

    for (int i = 0; i < length; ++i)
        take.setSample (0, i, pulseAt < 0 ? level : (i == pulseAt ? level : 0.0f));

    const auto file = directory.getChildFile (name);
    test::writeWav (file, take, sampleRate);
    return file;
}

struct Rig
{
    Rig()
    {
        test::useTracks (engine, 2);
        engine.setSessionDirectory (temp.directory);
        engine.prepare (sampleRate, blockSize);
    }

    test::TempDirectory temp;
    AudioEngine engine;
    juce::File mix = temp.directory.getChildFile ("Mixdowns/mix.wav");
};

} // namespace

TEST_CASE ("A mixdown sounds like playback, with takes where they sit")
{
    Rig rig;
    REQUIRE (rig.engine.loadTake (0, writeTake (rig.temp.directory, "a.wav", 9600, 0.25f), 0));
    REQUIRE (rig.engine.loadTake (1, writeTake (rig.temp.directory, "b.wav", 9600, 0.5f, 100), 3000));
    rig.engine.setTrackPan (0, -1.0f);

    const auto end = rig.engine.getSessionLength();
    CHECK (end == 12600);

    const auto result = rig.engine.bounce (rig.mix, 0, end);
    REQUIRE (result.result.wasOk());

    const auto mixed = test::readAll (rig.mix);
    REQUIRE (mixed.getNumChannels() == 2);
    REQUIRE (mixed.getNumSamples() == (int) result.numSamples);

    // Panned hard left, the first track rises to the full equal-power gain.
    CHECK (mixed.getSample (0, 1000) == Approx (0.25f * std::sqrt (2.0f)).margin (1.0e-4));
    CHECK (mixed.getSample (1, 1000) == Approx (0.0f).margin (1.0e-4));

    CHECK (mixed.getSample (1, 3100) == Approx (0.5f).margin (1.0e-4));
    CHECK (mixed.getSample (1, 3099) == Approx (0.0f).margin (1.0e-4));
    CHECK (result.peak == Approx (0.25f * std::sqrt (2.0f) + 0.5f).margin (1.0e-3));

    // With no effects, the mix runs past the end only far enough to hear half a second of silence.
    CHECK (result.numSamples >= end);
    CHECK (result.numSamples < end + 24000 + 2 * blockSize);
}

TEST_CASE ("A mixdown keeps the delay's tail")
{
    Rig rig;
    REQUIRE (rig.engine.loadTake (0, writeTake (rig.temp.directory, "a.wav", 4800, 0.8f, 0), 0));
    rig.engine.getChannel (0).setParameter (ChannelParameter::sendDelay, 1.0f);

    const auto end = rig.engine.getSessionLength();
    const auto result = rig.engine.bounce (rig.mix, 0, end);
    REQUIRE (result.result.wasOk());

    // The repeats run on for seconds after the take ends, and the file ends quiet.
    CHECK (result.numSamples > end + (juce::int64) sampleRate);

    const auto mixed = test::readAll (rig.mix);
    CHECK (mixed.getMagnitude (mixed.getNumSamples() - 2400, 2400) < 1.0e-4f);
}

TEST_CASE ("The click and live inputs stay out of a mixdown")
{
    Rig rig;
    REQUIRE (rig.engine.loadTake (0, writeTake (rig.temp.directory, "quiet.wav", 48000, 0.0f), 0));
    rig.engine.setClickEnabled (true);
    rig.engine.setTrackArmed (1, true);
    rig.engine.setTrackMonitoring (1, true);

    const auto result = rig.engine.bounce (rig.mix, 0, rig.engine.getSessionLength());

    REQUIRE (result.result.wasOk());
    CHECK (result.peak == 0.0f);
}

TEST_CASE ("After a mixdown the transport is where it was, and the device plays again")
{
    Rig rig;
    REQUIRE (rig.engine.loadTake (0, writeTake (rig.temp.directory, "a.wav", 9600, 0.25f), 0));
    rig.engine.setPosition (1280);

    REQUIRE (rig.engine.bounce (rig.mix, 0, rig.engine.getSessionLength()).result.wasOk());

    CHECK (rig.engine.getState() == TransportState::stopped);
    CHECK (rig.engine.getPosition() == 1280);

    std::vector<float> left (blockSize), right (blockSize);
    float* outputs[] { left.data(), right.data() };

    REQUIRE (rig.engine.play());
    rig.engine.processBlock (nullptr, 0, outputs, 2, blockSize);
    CHECK (left[0] == Approx (0.25f).margin (1.0e-3));
}

TEST_CASE ("A mixdown can be cancelled and leaves no file")
{
    Rig rig;
    REQUIRE (rig.engine.loadTake (0, writeTake (rig.temp.directory, "a.wav", 48000, 0.25f), 0));

    auto asked = 0;
    const auto result = rig.engine.bounce (rig.mix, 0, rig.engine.getSessionLength(), [&asked] (double progress)
    {
        CHECK (progress >= 0.0);
        CHECK (progress <= 1.0);
        return ++asked < 3;
    });

    CHECK (result.result.failed());
    CHECK_FALSE (rig.mix.exists());
    CHECK (rig.engine.getState() == TransportState::stopped);
}

TEST_CASE ("A mixdown refuses an empty session or a running transport")
{
    Rig rig;
    CHECK (rig.engine.getSessionLength() == 0);
    CHECK (rig.engine.bounce (rig.mix, 0, 0).result.failed());

    REQUIRE (rig.engine.loadTake (0, writeTake (rig.temp.directory, "a.wav", 4800, 0.25f), 0));
    REQUIRE (rig.engine.play());
    CHECK (rig.engine.bounce (rig.mix, 0, 4800).result.failed());
}

TEST_CASE ("The session length covers MIDI clips and drum parts too")
{
    Rig rig;
    rig.engine.setTempo (120.0);

    const auto keys = rig.engine.addTrack ("Keys", TrackType::midi);
    const auto takeFile = rig.temp.directory.getChildFile ("keys.mid");
    REQUIRE (writeMidiTake (*makeMidiTake ({}, 3840), takeFile));

    Clip clip;
    clip.file = takeFile;
    clip.timelineStart = 960;
    clip.length = 3840;
    clip.fileLength = 3840;
    REQUIRE (rig.engine.setClips (keys, { clip }).wasOk());

    // Five beats at 120 BPM.
    CHECK (rig.engine.getSessionLength() == 120000);

    const auto drums = rig.engine.addTrack ("Drums", TrackType::drums);
    DrumPart part;
    part.groove = "none";
    part.startBar = 2;
    part.bars = 1;
    REQUIRE (rig.engine.setDrumParts (drums, { part }).wasOk());

    // Three bars of four beats.
    CHECK (rig.engine.getSessionLength() == 288000);
}

TEST_CASE ("A steady offset after the music doesn't keep a mixdown running")
{
    Rig rig;

    // A take that ends on a constant offset, as an amp model with no input can leave.
    juce::AudioBuffer<float> take (1, 48000);

    for (int i = 0; i < take.getNumSamples(); ++i)
        take.setSample (0, i, i < 4800 ? 0.3f * (float) std::sin (0.05 * i) : 2.0e-4f);

    const auto file = rig.temp.directory.getChildFile ("offset.wav");
    test::writeWav (file, take, sampleRate);
    REQUIRE (rig.engine.loadTake (0, file, 0));

    // Ending the mix where the music stops leaves the offset running on, as an amp would.
    constexpr juce::int64 end = 4800;
    const auto result = rig.engine.bounce (rig.mix, 0, end);

    REQUIRE (result.result.wasOk());
    CHECK (result.numSamples < end + 24000 + 2 * blockSize);
}

namespace
{

juce::Array<juce::File> stemFiles (const juce::File& folder)
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.wav");
    files.sort();
    return files;
}

} // namespace

TEST_CASE ("Stems add up to the mixdown")
{
    Rig rig;
    REQUIRE (rig.engine.loadTake (0, writeTake (rig.temp.directory, "a.wav", 9600, 0.25f), 0));
    REQUIRE (rig.engine.loadTake (1, writeTake (rig.temp.directory, "b.wav", 4800, 0.6f, 50), 2000));
    rig.engine.setTrackPan (0, -0.5f);
    rig.engine.getChannel (1).setParameter (ChannelParameter::sendDelay, 0.7f);
    rig.engine.setMasterGain (0.5f);

    const auto end = rig.engine.getSessionLength();
    const auto mixResult = rig.engine.bounce (rig.mix, 0, end);
    const auto folder = rig.temp.directory.getChildFile ("Stems");
    const auto stemResult = rig.engine.bounceStems (folder, 0, end);

    REQUIRE (mixResult.result.wasOk());
    REQUIRE (stemResult.result.wasOk());
    CHECK (stemResult.numSamples == mixResult.numSamples);

    const auto files = stemFiles (folder);
    REQUIRE (files.size() == 3);

    const auto mix = test::readAll (rig.mix);
    juce::AudioBuffer<float> sum (2, mix.getNumSamples());
    sum.clear();

    for (const auto& file : files)
    {
        const auto stem = test::readAll (file);
        REQUIRE (stem.getNumSamples() == mix.getNumSamples());

        for (int channel = 0; channel < 2; ++channel)
            sum.addFrom (channel, 0, stem, channel, 0, stem.getNumSamples());
    }

    auto worst = 0.0f;

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < mix.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (sum.getSample (channel, i) - mix.getSample (channel, i)));

    // Each 24-bit file rounds a little on its own.
    CHECK (worst < 1.0e-5f);
}

TEST_CASE ("Each stem holds only its own track, named in track order")
{
    Rig rig;
    rig.engine.activateTrack (2, "Bass");
    REQUIRE (rig.engine.loadTake (0, writeTake (rig.temp.directory, "a.wav", 4800, 0.25f), 0));
    REQUIRE (rig.engine.loadTake (1, writeTake (rig.temp.directory, "b.wav", 4800, 0.5f), 0));
    REQUIRE (rig.engine.loadTake (2, writeTake (rig.temp.directory, "c.wav", 4800, 0.4f), 6000));

    const auto folder = rig.temp.directory.getChildFile ("Stems");

    SECTION ("every track that can be heard, then the effects")
    {
        REQUIRE (rig.engine.bounceStems (folder, 0, rig.engine.getSessionLength()).result.wasOk());

        const auto files = stemFiles (folder);
        REQUIRE (files.size() == 4);
        CHECK (files[0].getFileName() == "01 Track 1.wav");
        CHECK (files[1].getFileName() == "02 Track 2.wav");
        CHECK (files[2].getFileName() == "03 Bass.wav");
        CHECK (files[3].getFileName() == "04 Reverb and Delay.wav");

        const auto bass = test::readAll (files[2]);
        CHECK (bass.getSample (0, 1000) == 0.0f);
        CHECK (bass.getSample (0, 7000) == Approx (0.4f).margin (1.0e-4));

        const auto first = test::readAll (files[0]);
        CHECK (first.getSample (0, 1000) == Approx (0.25f).margin (1.0e-4));
        CHECK (first.getSample (0, 7000) == 0.0f);
    }

    SECTION ("a muted track is left out, as it is from the mix")
    {
        rig.engine.setTrackMuted (1, true);
        REQUIRE (rig.engine.bounceStems (folder, 0, rig.engine.getSessionLength()).result.wasOk());

        const auto files = stemFiles (folder);
        REQUIRE (files.size() == 3);
        CHECK (files[0].getFileName() == "01 Track 1.wav");
        CHECK (files[1].getFileName() == "02 Bass.wav");
    }

    SECTION ("cancelling leaves no stems behind")
    {
        auto asked = 0;
        const auto result = rig.engine.bounceStems (folder, 0, rig.engine.getSessionLength(), [&asked] (double) { return ++asked < 1; });

        CHECK (result.result.failed());
        CHECK (stemFiles (folder).isEmpty());
    }
}
