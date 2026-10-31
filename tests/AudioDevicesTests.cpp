#include "rifftrack/AudioDevices.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("bufferLatencyMs converts samples to milliseconds")
{
    CHECK (rifftrack::bufferLatencyMs (64, 48000.0) == Catch::Approx (1.3333).epsilon (0.001));
    CHECK (rifftrack::bufferLatencyMs (480, 48000.0) == Catch::Approx (10.0));
}

TEST_CASE ("bufferLatencyMs ignores an unset sample rate")
{
    CHECK (rifftrack::bufferLatencyMs (64, 0.0) == 0.0);
}

TEST_CASE ("preferredInputDevice uses the interface rather than the built-in mic")
{
    const std::vector<std::string> inputs { "MacBook Pro Microphone", "Scarlett 2i2 4th Gen" };

    CHECK (rifftrack::preferredInputDevice ("Scarlett 2i2 4th Gen", "MacBook Pro Microphone", inputs) == "Scarlett 2i2 4th Gen");
}

TEST_CASE ("preferredInputDevice leaves the input alone when the output has none")
{
    const std::vector<std::string> inputs { "MacBook Pro Microphone" };

    CHECK (rifftrack::preferredInputDevice ("MacBook Pro Speakers", "MacBook Pro Microphone", inputs) == "MacBook Pro Microphone");
}

TEST_CASE ("listAudioDevices reports usable device details")
{
    for (const auto& device : rifftrack::listAudioDevices())
    {
        INFO (device.name);
        CHECK_FALSE (device.name.empty());
        CHECK (device.inputChannels + device.outputChannels > 0);
        CHECK_FALSE (device.sampleRates.empty());
    }
}
