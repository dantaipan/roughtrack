#include "ControllerInput.h"

#include <catch2/catch_test_macros.hpp>

using roughtrack::ControlCommand;

namespace
{

struct Recorder
{
    std::vector<juce::MidiMessage> forwarded;
    std::vector<ControlCommand> commands;

    ControllerInput input { [this] (juce::MidiInput*, const juce::MidiMessage& message) { forwarded.push_back (message); },
                            [this] (roughtrack::ControlEvent event) { commands.push_back (event.command); } };
};

juce::MidiMessage mmcPlay()
{
    const juce::uint8 data[] { 0x7f, 0x7f, 0x06, 0x02 };
    return juce::MidiMessage::createSysExMessage (data, (int) sizeof (data));
}

} // namespace

TEST_CASE ("The controller port's buttons become commands and are never played")
{
    Recorder recorder;
    recorder.input.setControllerPort ("daw");

    recorder.input.route ("daw", nullptr, juce::MidiMessage::noteOn (1, 94, (juce::uint8) 127));
    recorder.input.route ("daw", nullptr, juce::MidiMessage::noteOff (1, 94));

    // Faders and anything else the surface sends are held back too.
    recorder.input.route ("daw", nullptr, juce::MidiMessage::pitchWheel (1, 9000));

    CHECK (recorder.commands == std::vector { ControlCommand::play });
    CHECK (recorder.forwarded.empty());
}

TEST_CASE ("Other ports still play, even the notes Mackie uses")
{
    Recorder recorder;
    recorder.input.setControllerPort ("daw");

    recorder.input.route ("keys", nullptr, juce::MidiMessage::noteOn (1, 94, (juce::uint8) 100));
    recorder.input.route ("keys", nullptr, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));

    CHECK (recorder.commands.empty());
    CHECK (recorder.forwarded.size() == 2);
}

TEST_CASE ("Without a controller port everything is played")
{
    Recorder recorder;

    recorder.input.route ({}, nullptr, juce::MidiMessage::noteOn (1, 94, (juce::uint8) 100));
    recorder.input.route ("keys", nullptr, juce::MidiMessage::noteOn (1, 94, (juce::uint8) 100));

    CHECK (recorder.commands.empty());
    CHECK (recorder.forwarded.size() == 2);
}

TEST_CASE ("MMC works from any port")
{
    Recorder recorder;
    recorder.input.setControllerPort ("daw");

    recorder.input.route ("keys", nullptr, mmcPlay());
    recorder.input.route ("daw", nullptr, mmcPlay());

    CHECK (recorder.commands == std::vector { ControlCommand::play, ControlCommand::play });
    CHECK (recorder.forwarded.empty());
}
