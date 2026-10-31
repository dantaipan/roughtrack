#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

TEST_CASE ("A take survives its process being killed mid-recording")
{
    rifftrack::test::TempDirectory temp;
    const auto file = temp.directory.getChildFile ("take.wav");

    juce::ChildProcess recorder;
    REQUIRE (recorder.start (juce::StringArray { RIFFTRACK_BENCH_PATH, "crash-writer", file.getFullPathName() }));

    const auto started = juce::Time::getMillisecondCounterHiRes();

    while (! file.existsAsFile() && juce::Time::getMillisecondCounterHiRes() - started < 5000.0)
        juce::Thread::sleep (10);

    REQUIRE (file.existsAsFile());

    juce::Thread::sleep (3500);

    // ChildProcess::kill() sends SIGKILL, so nothing gets a chance to clean up.
    const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    REQUIRE (recorder.kill());

    const auto reader = rifftrack::test::openReader (file);
    REQUIRE (reader != nullptr);
    CHECK (reader->bitsPerSample == 24);

    const auto recoveredSeconds = (double) reader->lengthInSamples / reader->sampleRate;
    INFO ("recovered " << recoveredSeconds << "s of " << elapsedSeconds << "s");

    // Allows for one flush interval plus process start-up.
    CHECK (recoveredSeconds >= elapsedSeconds - 1.75);

    const auto recovered = rifftrack::test::readAll (file);

    for (int i = 0; i < recovered.getNumSamples(); i += 1009)
    {
        const auto expected = 0.5 * std::sin (2.0 * std::numbers::pi * 440.0 * i / 48000.0);
        REQUIRE (recovered.getSample (0, i) == Catch::Approx (expected).margin (1.0e-5));
    }
}
