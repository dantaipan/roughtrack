#include "roughtrack/ControlSurface.h"

#include <catch2/catch_test_macros.hpp>

using namespace roughtrack;

namespace
{

std::optional<ControlCommand> commandOf (const std::optional<ControlEvent>& event)
{
    return event.has_value() ? std::optional (event->command) : std::nullopt;
}

juce::MidiMessage press (int note) { return juce::MidiMessage::noteOn (1, note, (juce::uint8) 127); }

juce::MidiMessage mmc (juce::uint8 device, juce::uint8 command)
{
    const juce::uint8 data[] { 0x7f, device, 0x06, command };
    return juce::MidiMessage::createSysExMessage (data, (int) sizeof (data));
}

} // namespace

TEST_CASE ("Mackie buttons give their commands")
{
    CHECK (commandOf (decodeMackie (press (94))) == ControlCommand::play);
    CHECK (commandOf (decodeMackie (press (93))) == ControlCommand::stop);
    CHECK (commandOf (decodeMackie (press (95))) == ControlCommand::record);
    CHECK (commandOf (decodeMackie (press (91))) == ControlCommand::rewind);
    CHECK (commandOf (decodeMackie (press (92))) == ControlCommand::fastForward);

    for (const auto note : { 46, 48 })
        CHECK (commandOf (decodeMackie (press (note))) == ControlCommand::previousTrack);

    for (const auto note : { 47, 49 })
        CHECK (commandOf (decodeMackie (press (note))) == ControlCommand::nextTrack);
}

TEST_CASE ("Mackie's Cycle button toggles the loop")
{
    CHECK (commandOf (decodeMackie (press (86))) == ControlCommand::toggleCycle);
}

TEST_CASE ("The buttons under the faders say which strip they're for")
{
    CHECK (decodeMackie (press (0)) == ControlEvent { ControlCommand::armStrip, 0 });
    CHECK (decodeMackie (press (7)) == ControlEvent { ControlCommand::armStrip, 7 });
    CHECK (decodeMackie (press (10)) == ControlEvent { ControlCommand::soloStrip, 2 });
    CHECK (decodeMackie (press (19)) == ControlEvent { ControlCommand::muteStrip, 3 });
    CHECK (decodeMackie (press (31)) == ControlEvent { ControlCommand::selectStrip, 7 });

    // Their releases do nothing, like the other buttons'.
    CHECK_FALSE (decodeMackie (juce::MidiMessage::noteOn (1, 0, (juce::uint8) 0)).has_value());
    CHECK_FALSE (decodeMackie (press (32)).has_value());
}

TEST_CASE ("The fader buttons cover the eight tracks around the armed one")
{
    CHECK (bankStart ({}) == 0);
    CHECK (bankStart (std::vector<bool> (20, false)) == 0);

    auto armed = std::vector<bool> (20, false);
    armed[3] = true;
    CHECK (bankStart (armed) == 0);

    armed[3] = false;
    armed[9] = true;
    CHECK (bankStart (armed) == 8);

    armed[17] = true;
    CHECK (bankStart (armed) == 8);
}

TEST_CASE ("Releases and other Mackie messages give nothing")
{
    CHECK_FALSE (decodeMackie (juce::MidiMessage::noteOff (1, 94)).has_value());
    CHECK_FALSE (decodeMackie (juce::MidiMessage::noteOn (1, 94, (juce::uint8) 0)).has_value());
    CHECK_FALSE (decodeMackie (juce::MidiMessage::noteOn (2, 94, (juce::uint8) 127)).has_value());
    CHECK_FALSE (decodeMackie (press (60)).has_value());
    CHECK_FALSE (decodeMackie (juce::MidiMessage::controllerEvent (1, 94, 127)).has_value());
}

TEST_CASE ("MMC transport messages give their commands")
{
    for (const auto device : { (juce::uint8) 0x7f, (juce::uint8) 0x00 })
    {
        CHECK (commandOf (decodeMmc (mmc (device, 0x01))) == ControlCommand::stop);
        CHECK (commandOf (decodeMmc (mmc (device, 0x02))) == ControlCommand::play);
        CHECK (commandOf (decodeMmc (mmc (device, 0x03))) == ControlCommand::play);
        CHECK (commandOf (decodeMmc (mmc (device, 0x04))) == ControlCommand::fastForward);
        CHECK (commandOf (decodeMmc (mmc (device, 0x05))) == ControlCommand::rewind);
        CHECK (commandOf (decodeMmc (mmc (device, 0x06))) == ControlCommand::record);
        CHECK (commandOf (decodeMmc (mmc (device, 0x09))) == ControlCommand::stop);
    }
}

TEST_CASE ("Other system exclusive messages aren't taken as MMC")
{
    // Record exit, which RoughTrack has no use for.
    CHECK_FALSE (decodeMmc (mmc (0x7f, 0x07)).has_value());

    // An identity request is universal too, but not machine control.
    const juce::uint8 identity[] { 0x7e, 0x7f, 0x06, 0x01 };
    CHECK_FALSE (decodeMmc (juce::MidiMessage::createSysExMessage (identity, (int) sizeof (identity))).has_value());

    const juce::uint8 tooShort[] { 0x7f, 0x7f, 0x06 };
    CHECK_FALSE (decodeMmc (juce::MidiMessage::createSysExMessage (tooShort, (int) sizeof (tooShort))).has_value());

    CHECK_FALSE (decodeMmc (press (94)).has_value());
}

TEST_CASE ("Lights are notes at full or no velocity")
{
    const auto on = mackieLight (mackie::play, true);
    const auto off = mackieLight (mackie::record, false);

    CHECK (on.getRawData()[0] == 0x90);
    CHECK (on.getNoteNumber() == 94);
    CHECK (on.getVelocity() == 127);

    CHECK (off.getRawData()[0] == 0x90);
    CHECK (off.getNoteNumber() == 95);
    CHECK (off.getVelocity() == 0);
}

TEST_CASE ("DAW ports are recognised by name")
{
    CHECK (looksLikeControlPort ("Oxygen 49 MKV Mackie/HUI"));
    CHECK (looksLikeControlPort ("MIDIIN2 (Oxygen 49 MKV)"));
    CHECK (looksLikeControlPort ("Oxygen DAW"));
    CHECK (looksLikeControlPort ("KeyLab mkII 49 DAW"));

    CHECK_FALSE (looksLikeControlPort ("Oxygen 49 MKV"));
    CHECK_FALSE (looksLikeControlPort ("Scarlett 4i4 USB"));
    CHECK_FALSE (looksLikeControlPort ("IAC Driver Bus 1"));
}

TEST_CASE ("Rewind and fast-forward move by a bar")
{
    constexpr double bar = 96000.0;

    CHECK (previousBar (150000, bar) == 96000);
    CHECK (previousBar (96000, bar) == 0);
    CHECK (previousBar (192000, bar) == 96000);
    CHECK (previousBar (0, bar) == 0);
    CHECK (previousBar (10, bar) == 0);

    CHECK (nextBar (0, bar) == 96000);
    CHECK (nextBar (150000, bar) == 192000);
    CHECK (nextBar (96000, bar) == 192000);

    // At 133 bpm a bar isn't a whole number of samples, but rewinding after
    // fast-forwarding still goes back to the start.
    constexpr double oddBar = 60.0 * 44100.0 / 133.0 * 4.0;
    const auto first = nextBar (0, oddBar);
    CHECK (first == 79579);
    CHECK (previousBar (first, oddBar) == 0);
    CHECK (nextBar (first, oddBar) == 159158);
    CHECK (previousBar (nextBar (first, oddBar), oddBar) == first);
}

TEST_CASE ("Track buttons move the arm along the lanes")
{
    SECTION ("Nothing armed starts at an end")
    {
        CHECK (nextArmedLane ({ false, false, false }, 1) == 0);
        CHECK (nextArmedLane ({ false, false, false }, -1) == 2);
    }

    SECTION ("One armed moves by one")
    {
        CHECK (nextArmedLane ({ false, true, false }, 1) == 2);
        CHECK (nextArmedLane ({ false, true, false }, -1) == 0);
    }

    SECTION ("At the ends it stays put")
    {
        CHECK (nextArmedLane ({ false, false, true }, 1) == 2);
        CHECK (nextArmedLane ({ true, false, false }, -1) == 0);
    }

    SECTION ("Several armed moves on from the outermost")
    {
        CHECK (nextArmedLane ({ false, true, false, true, false }, 1) == 4);
        CHECK (nextArmedLane ({ false, true, false, true, false }, -1) == 0);
    }

    SECTION ("No lanes")
    {
        CHECK (nextArmedLane ({}, 1) == -1);
    }
}
