#include "roughtrack/MidiSequencer.h"

#include <algorithm>
#include <cmath>

namespace roughtrack
{

namespace
{
constexpr int sustainController = 64;
} // namespace

void MidiSequencer::setClips (const ClipList& clipsToPlay, std::vector<std::shared_ptr<const MidiTake>> takesToPlay)
{
    clips = clipsToPlay;
    takes = std::move (takesToPlay);
    lastClip = noClip;
}

void MidiSequencer::rewind() noexcept
{
    lastClip = noClip;
}

// Two halves of a split clip are one performance, so a note held across the join rings on.
bool MidiSequencer::continuesFrom (int previous, int next) const noexcept
{
    if (previous < 0 || next < 0)
        return false;

    const auto& a = clips[(size_t) previous];
    const auto& b = clips[(size_t) next];

    return takes[(size_t) previous] == takes[(size_t) next] && a.fileOffset - a.timelineStart == b.fileOffset - b.timelineStart;
}

void MidiSequencer::render (juce::MidiBuffer& output, int numSamples, juce::int64 timelinePosition, double samplesPerTick) noexcept
{
    const auto lastTick = sampleToTick (timelinePosition + numSamples, samplesPerTick);

    for (auto tick = sampleToTick (timelinePosition, samplesPerTick); tick < lastTick;)
    {
        const auto segment = findSegment (clips, tick, lastTick - tick);

        if (segment.clip != lastClip && ! continuesFrom (lastClip, segment.clip))
        {
            const auto sample = (juce::int64) std::llround ((double) tick * samplesPerTick);
            endSoundingNotes (output, juce::jlimit (0, numSamples - 1, (int) (sample - timelinePosition)));
        }

        lastClip = segment.clip;

        if (segment.clip >= 0 && takes[(size_t) segment.clip] != nullptr)
            renderSegment (output, clips[(size_t) segment.clip], *takes[(size_t) segment.clip], tick, tick + segment.length,
                           timelinePosition, numSamples, samplesPerTick);

        tick += segment.length;
    }
}

void MidiSequencer::renderSegment (juce::MidiBuffer& output, const Clip& clip, const MidiTake& take, juce::int64 fromTick, juce::int64 toTick,
                                   juce::int64 timelinePosition, int numSamples, double samplesPerTick) noexcept
{
    const auto toTakeTick = clip.fileOffset - clip.timelineStart;

    const auto first = std::lower_bound (take.events.begin(), take.events.end(), fromTick + toTakeTick,
                                         [] (const MidiEvent& event, juce::int64 tick) { return event.tick < tick; });

    for (auto event = first; event != take.events.end() && event->tick < toTick + toTakeTick; ++event)
    {
        const auto sample = (juce::int64) std::llround ((double) (event->tick - toTakeTick) * samplesPerTick);

        noteSent (event->data);
        output.addEvent (event->data, event->getSize(), juce::jlimit (0, numSamples - 1, (int) (sample - timelinePosition)));
    }
}

void MidiSequencer::noteSent (const juce::uint8* data) noexcept
{
    const auto type = data[0] & 0xf0;
    const auto channel = (size_t) (data[0] & 0x0f);

    channelsUsed.set (channel);

    if (type == 0x90 && data[2] > 0)
        soundingNotes[channel].set (data[1]);
    else if (type == 0x80 || type == 0x90)
        soundingNotes[channel].reset (data[1]);
}

void MidiSequencer::endSoundingNotes (juce::MidiBuffer& output, int sampleOffset) noexcept
{
    for (int channel = 0; channel < 16; ++channel)
    {
        if (! channelsUsed.test ((size_t) channel))
            continue;

        for (int pitch = 0; pitch < 128; ++pitch)
        {
            if (soundingNotes[(size_t) channel].test ((size_t) pitch))
            {
                const juce::uint8 off[] { (juce::uint8) (0x80 | channel), (juce::uint8) pitch, 0 };
                output.addEvent (off, 3, sampleOffset);
            }
        }

        // A held pedal would otherwise keep the note ringing after its note-off.
        const juce::uint8 pedalUp[] { (juce::uint8) (0xb0 | channel), sustainController, 0 };
        output.addEvent (pedalUp, 3, sampleOffset);

        soundingNotes[(size_t) channel].reset();
    }

    channelsUsed.reset();
}

} // namespace roughtrack
