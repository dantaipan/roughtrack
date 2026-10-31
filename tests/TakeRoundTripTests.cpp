#include "TestHelpers.h"

#include "rifftrack/TakeWriter.h"
#include "rifftrack/TrackPlayer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace rifftrack;

TEST_CASE ("TakeWriter writes a 24-bit WAV that reads back intact")
{
    test::TempDirectory temp;
    const auto file = temp.directory.getChildFile ("take.wav");

    juce::AudioBuffer<float> source (2, 48000);

    for (int i = 0; i < source.getNumSamples(); ++i)
    {
        source.setSample (0, i, (float) i / 96000.0f);
        source.setSample (1, i, -(float) i / 96000.0f);
    }

    {
        juce::TimeSliceThread thread ("test writer");
        thread.startThread();

        auto writer = TakeWriter::create (file, 48000.0, 2, thread);
        REQUIRE (writer != nullptr);

        for (int start = 0; start < source.getNumSamples(); start += 64)
        {
            const float* channels[] { source.getReadPointer (0, start), source.getReadPointer (1, start) };
            REQUIRE (writer->write (channels, juce::jmin (64, source.getNumSamples() - start)));
        }

        CHECK (writer->getDroppedSamples() == 0);
    }

    const auto reader = test::openReader (file);
    REQUIRE (reader != nullptr);
    CHECK (reader->bitsPerSample == 24);
    CHECK (reader->numChannels == 2);
    CHECK (reader->sampleRate == 48000.0);
    CHECK (reader->lengthInSamples == source.getNumSamples());

    const auto result = test::readAll (file);

    for (int i = 0; i < source.getNumSamples(); i += 997)
    {
        CHECK (result.getSample (0, i) == Catch::Approx (source.getSample (0, i)).margin (1.0e-6));
        CHECK (result.getSample (1, i) == Catch::Approx (source.getSample (1, i)).margin (1.0e-6));
    }
}

TEST_CASE ("TakeWriter reports a block it had to drop")
{
    test::TempDirectory temp;

    // The disk thread is never started, so the FIFO can only fill up.
    juce::TimeSliceThread thread ("idle writer");
    auto writer = TakeWriter::create (temp.directory.getChildFile ("take.wav"), 1000.0, 1, thread);
    REQUIRE (writer != nullptr);

    std::vector<float> block (20000);
    const float* channels[] { block.data() };

    thread.startThread();
    thread.stopThread (1000);

    CHECK_FALSE (writer->write (channels, 20000));
    CHECK (writer->getDroppedSamples() == 20000);
}

TEST_CASE ("TrackPlayer delivers samples from the requested timeline position")
{
    test::TempDirectory temp;
    const auto file = temp.directory.getChildFile ("take.wav");

    juce::AudioBuffer<float> source (1, 2000);

    for (int i = 0; i < source.getNumSamples(); ++i)
        source.setSample (0, i, (float) (i + 1) / 4000.0f);

    test::writeWav (file, source, 48000.0);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    juce::TimeSliceThread thread ("test reader");
    thread.startThread();

    TrackPlayer player (thread, formats);

    std::vector<float> left (100), right (100);

    SECTION ("with no take it plays silence")
    {
        left[0] = 1.0f;
        player.pull (left.data(), right.data(), 100);
        CHECK (left[0] == 0.0f);
        CHECK (player.getUnderrunSamples() == 0);
    }

    SECTION ("a take starting later is preceded by silence")
    {
        REQUIRE (player.load (file, 1000));
        player.prepareToPlay (950);
        player.pull (left.data(), right.data(), 100);

        CHECK (left[49] == 0.0f);
        CHECK (left[50] == Catch::Approx (source.getSample (0, 0)).margin (1.0e-6));
        CHECK (right[99] == Catch::Approx (source.getSample (0, 49)).margin (1.0e-6));
    }

    SECTION ("a take moved before zero starts part way through its file")
    {
        REQUIRE (player.load (file, -300));
        player.prepareToPlay (0);
        player.pull (left.data(), right.data(), 100);

        CHECK (left[0] == Catch::Approx (source.getSample (0, 300)).margin (1.0e-6));
    }

    SECTION ("playback past the end of a take is silent")
    {
        REQUIRE (player.load (file, 0));
        player.prepareToPlay (1950);
        player.pull (left.data(), right.data(), 100);

        CHECK (left[49] == Catch::Approx (source.getSample (0, 1999)).margin (1.0e-6));
        CHECK (left[50] == 0.0f);
    }
}
