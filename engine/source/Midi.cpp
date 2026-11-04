#include "roughtrack/Midi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace roughtrack
{

namespace
{

constexpr int statusNoteOff = 0x80;
constexpr int statusNoteOn = 0x90;
constexpr float maximumVelocity = 127.0f;

bool isNoteOn (const MidiEvent& event) noexcept
{
    return (event.data[0] & 0xf0) == statusNoteOn && event.data[2] > 0;
}

bool isNoteOff (const MidiEvent& event) noexcept
{
    return (event.data[0] & 0xf0) == statusNoteOff || ((event.data[0] & 0xf0) == statusNoteOn && event.data[2] == 0);
}

std::pair<int, int> getNoteKey (const MidiEvent& event) noexcept
{
    return { event.data[0] & 0x0f, (int) event.data[1] };
}

/** The nearest grid line, or the next nearest if that's outside the range. */
juce::int64 snapToGrid (juce::int64 tick, juce::int64 startTick, juce::int64 endTick, juce::int64 timelineOffset, juce::int64 gridTicks) noexcept
{
    const auto halfWayOn = tick + timelineOffset + gridTicks / 2;
    auto snapped = halfWayOn - juce::negativeAwareModulo (halfWayOn, gridTicks) - timelineOffset;

    if (snapped < startTick)
        snapped += gridTicks;

    if (snapped >= endTick)
        snapped -= gridTicks;

    return snapped >= startTick && snapped < endTick ? snapped : tick;
}

} // namespace

std::shared_ptr<const MidiTake> quantizeTake (const MidiTake& take, juce::int64 startTick, juce::int64 endTick,
                                              juce::int64 timelineOffset, int gridTicks)
{
    struct PlayedNote
    {
        MidiEvent on;
        juce::int64 end = 0;
    };

    std::vector<MidiEvent> events;
    std::vector<PlayedNote> notes;
    std::map<std::pair<int, int>, size_t> sounding;

    for (const auto& event : take.events)
    {
        const auto found = sounding.find (getNoteKey (event));

        if (isNoteOn (event))
        {
            if (found != sounding.end())
                notes[found->second].end = event.tick;

            sounding[getNoteKey (event)] = notes.size();
            notes.push_back ({ event, event.tick });
        }
        else if (! isNoteOff (event))
        {
            events.push_back (event);
        }
        else if (found != sounding.end())
        {
            notes[found->second].end = event.tick;
            sounding.erase (found);
        }
    }

    for (auto& note : notes)
    {
        if (note.on.tick >= startTick && note.on.tick < endTick)
        {
            const auto length = note.end - note.on.tick;

            note.on.tick = snapToGrid (note.on.tick, startTick, endTick, timelineOffset, gridTicks);
            note.end = note.on.tick + length;
        }
    }

    std::stable_sort (notes.begin(), notes.end(), [] (const PlayedNote& a, const PlayedNote& b) { return a.on.tick < b.on.tick; });

    // Quick repeats of a pitch can land together, and a key can only be down once.
    std::vector<PlayedNote> separate;
    std::map<std::pair<int, int>, size_t> latest;

    for (const auto& note : notes)
    {
        const auto found = latest.find (getNoteKey (note.on));

        if (found != latest.end())
        {
            auto& earlier = separate[found->second];

            if (earlier.on.tick == note.on.tick)
            {
                earlier.on.data[2] = juce::jmax (earlier.on.data[2], note.on.data[2]);
                earlier.end = juce::jmax (earlier.end, note.end);
                continue;
            }

            earlier.end = juce::jmin (earlier.end, note.on.tick);
        }

        latest[getNoteKey (note.on)] = separate.size();
        separate.push_back (note);
    }

    auto length = take.length;

    // Note-offs go in first, so one landing on the next note-on's tick plays before it.
    for (const auto& note : separate)
    {
        MidiEvent off;
        off.tick = juce::jmax (note.end, note.on.tick + 1);
        off.data[0] = (juce::uint8) (statusNoteOff | (note.on.data[0] & 0x0f));
        off.data[1] = note.on.data[1];

        length = juce::jmax (length, off.tick);
        events.push_back (off);
    }

    for (const auto& note : separate)
        events.push_back (note.on);

    return makeMidiTake (std::move (events), length);
}

std::shared_ptr<const MidiTake> makeMidiTake (std::vector<MidiEvent> events, juce::int64 length)
{
    std::stable_sort (events.begin(), events.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

    auto take = std::make_shared<MidiTake>();
    take->length = length;

    // Where each sounding note began, by channel and pitch.
    std::map<std::pair<int, int>, size_t> sounding;

    for (const auto& event : events)
    {
        const auto key = std::make_pair (event.data[0] & 0x0f, (int) event.data[1]);

        if (isNoteOn (event))
        {
            sounding[key] = take->notes.size();
            take->notes.push_back ({ event.tick, 0, event.data[1], (float) event.data[2] / maximumVelocity });
        }
        else if (isNoteOff (event))
        {
            const auto found = sounding.find (key);

            if (found != sounding.end())
            {
                auto& note = take->notes[found->second];
                note.length = event.tick - note.tick;
                sounding.erase (found);
            }
        }
    }

    take->events = std::move (events);

    for (const auto& [key, index] : sounding)
    {
        auto& note = take->notes[index];
        note.length = juce::jmax<juce::int64> (0, length - note.tick);

        MidiEvent off;
        off.tick = juce::jmax (note.tick, length);
        off.data[0] = (juce::uint8) (statusNoteOff | key.first);
        off.data[1] = (juce::uint8) key.second;
        take->events.push_back (off);
    }

    return take;
}

std::shared_ptr<const MidiTake> readMidiTake (const juce::File& file)
{
    juce::FileInputStream stream (file);
    juce::MidiFile midi;

    if (! stream.openedOk() || ! midi.readFrom (stream) || midi.getTimeFormat() <= 0)
        return nullptr;

    const auto scale = (double) ticksPerBeat / midi.getTimeFormat();

    std::vector<MidiEvent> events;
    auto length = (juce::int64) 0;

    for (int track = 0; track < midi.getNumTracks(); ++track)
    {
        const auto& sequence = *midi.getTrack (track);

        for (const auto* holder : sequence)
        {
            const auto& message = holder->message;
            const auto tick = (juce::int64) std::llround (message.getTimeStamp() * scale);

            length = juce::jmax (length, tick);

            if (message.getRawDataSize() > 3 || message.isMetaEvent() || message.getRawData()[0] >= 0xf0)
                continue;

            MidiEvent event;
            event.tick = tick;
            std::copy_n (message.getRawData(), message.getRawDataSize(), event.data);
            events.push_back (event);
        }
    }

    return makeMidiTake (std::move (events), length);
}

bool writeMidiTake (const MidiTake& take, const juce::File& file)
{
    juce::MidiMessageSequence sequence;

    for (const auto& event : take.events)
        sequence.addEvent (juce::MidiMessage (event.data, event.getSize(), (double) event.tick));

    // The end marker keeps the silence after the last note.
    sequence.addEvent (juce::MidiMessage::endOfTrack().withTimeStamp ((double) take.length));
    sequence.updateMatchedPairs();

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticksPerBeat);
    midi.addTrack (sequence);

    file.deleteFile();
    juce::FileOutputStream stream (file);

    return stream.openedOk() && midi.writeTo (stream);
}

juce::int64 sampleToTick (juce::int64 sample, double samplesPerTick) noexcept
{
    return (juce::int64) std::ceil (((double) sample - 0.5) / samplesPerTick);
}

} // namespace roughtrack
