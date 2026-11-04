#include "roughtrack/AudioDevices.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("bufferLatencyMs converts samples to milliseconds")
{
    CHECK (roughtrack::bufferLatencyMs (64, 48000.0) == Catch::Approx (1.3333).epsilon (0.001));
    CHECK (roughtrack::bufferLatencyMs (480, 48000.0) == Catch::Approx (10.0));
}

TEST_CASE ("bufferLatencyMs ignores an unset sample rate")
{
    CHECK (roughtrack::bufferLatencyMs (64, 0.0) == 0.0);
}

TEST_CASE ("preferredInputDevice uses the interface rather than the built-in mic")
{
    const std::vector<std::string> inputs { "MacBook Pro Microphone", "Scarlett 2i2 4th Gen" };

    CHECK (roughtrack::preferredInputDevice ("Scarlett 2i2 4th Gen", "MacBook Pro Microphone", inputs) == "Scarlett 2i2 4th Gen");
}

TEST_CASE ("preferredInputDevice leaves the input alone when the output has none")
{
    const std::vector<std::string> inputs { "MacBook Pro Microphone" };

    CHECK (roughtrack::preferredInputDevice ("MacBook Pro Speakers", "MacBook Pro Microphone", inputs) == "MacBook Pro Microphone");
}

TEST_CASE ("listAudioDevices reports usable device details")
{
    for (const auto& device : roughtrack::listAudioDevices())
    {
        INFO (device.name);
        CHECK_FALSE (device.name.empty());
        CHECK (device.inputChannels + device.outputChannels > 0);
        CHECK_FALSE (device.sampleRates.empty());
    }
}

TEST_CASE ("The chosen audio setup comes back once its device is there")
{
    const roughtrack::AudioDeviceChoice chosen { "Scarlett 2i2 4th Gen", 48000.0, 64 };
    const std::vector<std::string> withInterface { "MacBook Pro Speakers", "Scarlett 2i2 4th Gen" };
    const std::vector<std::string> withoutInterface { "MacBook Pro Speakers" };

    SECTION ("nothing to do when it's already running")
    {
        CHECK_FALSE (roughtrack::shouldRestoreAudioDevice (chosen, chosen, withInterface));
    }

    SECTION ("the interface came back with a different buffer size")
    {
        CHECK (roughtrack::shouldRestoreAudioDevice (chosen, { "Scarlett 2i2 4th Gen", 48000.0, 512 }, withInterface));
    }

    SECTION ("the interface came back while the app sat on the built-in speakers")
    {
        CHECK (roughtrack::shouldRestoreAudioDevice (chosen, { "MacBook Pro Speakers", 48000.0, 512 }, withInterface));
    }

    SECTION ("the sample rate changed")
    {
        CHECK (roughtrack::shouldRestoreAudioDevice (chosen, { "Scarlett 2i2 4th Gen", 44100.0, 64 }, withInterface));
    }

    SECTION ("the fallback stays while the interface is unplugged")
    {
        CHECK_FALSE (roughtrack::shouldRestoreAudioDevice (chosen, { "MacBook Pro Speakers", 48000.0, 512 }, withoutInterface));
    }
}
