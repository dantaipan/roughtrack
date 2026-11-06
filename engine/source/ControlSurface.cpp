#include "roughtrack/ControlSurface.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace roughtrack
{

std::optional<ControlEvent> decodeMackie (const juce::MidiMessage& message)
{
    if (! message.isNoteOn() || message.getChannel() != 1)
        return std::nullopt;

    const auto note = message.getNoteNumber();

    if (note < mackie::firstSelect + stripsPerBank)
    {
        constexpr std::array strips { ControlCommand::armStrip, ControlCommand::soloStrip, ControlCommand::muteStrip, ControlCommand::selectStrip };
        return ControlEvent { strips[(size_t) (note / stripsPerBank)], note % stripsPerBank };
    }

    switch (note)
    {
        case mackie::play:         return ControlEvent { ControlCommand::play };
        case mackie::stop:         return ControlEvent { ControlCommand::stop };
        case mackie::record:       return ControlEvent { ControlCommand::record };
        case mackie::rewind:       return ControlEvent { ControlCommand::rewind };
        case mackie::fastForward:  return ControlEvent { ControlCommand::fastForward };
        case mackie::cycle:        return ControlEvent { ControlCommand::toggleCycle };
        case mackie::bankLeft:
        case mackie::channelLeft:  return ControlEvent { ControlCommand::previousTrack };
        case mackie::bankRight:
        case mackie::channelRight: return ControlEvent { ControlCommand::nextTrack };
        default:                   return std::nullopt;
    }
}

std::optional<ControlEvent> decodeMmc (const juce::MidiMessage& message)
{
    // F0 7F <device> 06 <command> F7
    if (! message.isSysEx() || message.getRawDataSize() != 6)
        return std::nullopt;

    const auto* data = message.getRawData();

    if (data[1] != 0x7f || data[3] != 0x06 || data[5] != 0xf7)
        return std::nullopt;

    switch (data[4])
    {
        case 0x01:
        case 0x09: return ControlEvent { ControlCommand::stop };  // Stop, pause
        case 0x02:
        case 0x03: return ControlEvent { ControlCommand::play };  // Play, deferred play
        case 0x04: return ControlEvent { ControlCommand::fastForward };
        case 0x05: return ControlEvent { ControlCommand::rewind };
        case 0x06: return ControlEvent { ControlCommand::record }; // Record strobe
        default:   return std::nullopt;
    }
}

juce::MidiMessage mackieLight (int note, bool on)
{
    return juce::MidiMessage::noteOn (1, note, (juce::uint8) (on ? 127 : 0));
}

bool looksLikeControlPort (const juce::String& portName)
{
    for (const auto* word : { "Mackie", "DAW", "MIDIIN2", "Control Surface" })
        if (portName.containsIgnoreCase (word))
            return true;

    return false;
}

namespace
{

juce::int64 barLine (juce::int64 bar, double samplesPerBar)
{
    return (juce::int64) std::llround ((double) bar * samplesPerBar);
}

} // namespace

// Bar lines are compared in whole samples, so after fast-forwarding to a line that
// isn't a whole number of samples, rewinding still goes back a bar.
juce::int64 previousBar (juce::int64 position, double samplesPerBar)
{
    if (samplesPerBar <= 0.0 || position <= 0)
        return 0;

    auto bar = (juce::int64) std::floor ((double) position / samplesPerBar);

    while (bar > 0 && barLine (bar, samplesPerBar) >= position)
        --bar;

    return barLine (bar, samplesPerBar);
}

juce::int64 nextBar (juce::int64 position, double samplesPerBar)
{
    if (samplesPerBar <= 0.0)
        return position;

    position = juce::jmax<juce::int64> (0, position);
    auto bar = (juce::int64) std::floor ((double) position / samplesPerBar) + 1;

    while (barLine (bar, samplesPerBar) <= position)
        ++bar;

    return barLine (bar, samplesPerBar);
}

int nextArmedLane (const std::vector<bool>& armedByLane, int direction)
{
    const auto numLanes = (int) armedByLane.size();

    if (numLanes == 0)
        return -1;

    const auto first = std::find (armedByLane.begin(), armedByLane.end(), true);

    if (first == armedByLane.end())
        return direction < 0 ? numLanes - 1 : 0;

    if (direction < 0)
        return juce::jmax (0, (int) std::distance (armedByLane.begin(), first) - 1);

    const auto last = std::find (armedByLane.rbegin(), armedByLane.rend(), true);
    return juce::jmin (numLanes - 1, numLanes - (int) std::distance (armedByLane.rbegin(), last));
}

int bankStart (const std::vector<bool>& armedByLane)
{
    const auto first = std::find (armedByLane.begin(), armedByLane.end(), true);

    if (first == armedByLane.end())
        return 0;

    return (int) std::distance (armedByLane.begin(), first) / stripsPerBank * stripsPerBank;
}

} // namespace roughtrack
