#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <vector>

namespace roughtrack
{

/** MIDI is timed in ticks rather than samples, so it follows the tempo. */
inline constexpr int ticksPerBeat = 960;

struct MidiEvent
{
    juce::int64 tick = 0;
    juce::uint8 data[3] {};

    int getSize() const noexcept { return juce::MidiMessage::getMessageLengthFromFirstByte (data[0]); }
};

struct MidiNote
{
    juce::int64 tick = 0;
    juce::int64 length = 0;
    int pitch = 0;
    float velocity = 0.0f;
};

/** A performance as recorded. Clips show parts of it, as they do with an audio file. */
struct MidiTake
{
    std::vector<MidiEvent> events;
    std::vector<MidiNote> notes;
    juce::int64 length = 0;
};

/** Puts events in order and ends any note still held when the take stopped. */
std::shared_ptr<const MidiTake> makeMidiTake (std::vector<MidiEvent>, juce::int64 length);

/** Moves the notes that start from startTick up to endTick onto a grid, keeping their
    lengths and staying inside that range. Everything else in the take is left where it
    was. The grid counts from the session's start, so timelineOffset is what to add to a
    tick in the take to find it on the timeline.
*/
std::shared_ptr<const MidiTake> quantizeTake (const MidiTake&, juce::int64 startTick, juce::int64 endTick,
                                              juce::int64 timelineOffset, int gridTicks);

std::shared_ptr<const MidiTake> readMidiTake (const juce::File&);
bool writeMidiTake (const MidiTake&, const juce::File&);

/** The first tick that lands at or after a sample. Blocks divide the timeline by
    this, so a tick can't fall between two of them or play in both.
*/
juce::int64 sampleToTick (juce::int64 sample, double samplesPerTick) noexcept;

} // namespace roughtrack
