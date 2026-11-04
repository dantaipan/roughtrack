#pragma once

#include "roughtrack/Clip.h"
#include "roughtrack/Midi.h"

#include <array>
#include <bitset>
#include <memory>

namespace roughtrack
{

/** Plays a track's MIDI clips, working out what's due from the timeline position
    alone, as the click and the drums do. Clip positions are in ticks.

    setClips() and rewind() must only be called while the transport is stopped.
*/
class MidiSequencer
{
public:
    /** takes holds one entry for each clip, or nullptr for one that's missing. */
    void setClips (const ClipList&, std::vector<std::shared_ptr<const MidiTake>> takes);

    /** Forgets where it was, ready to start from somewhere else. */
    void rewind() noexcept;

    void render (juce::MidiBuffer&, int numSamples, juce::int64 timelinePosition, double samplesPerTick) noexcept;

    /** Notes the keyboard played, so they can be stopped too. */
    void noteSent (const juce::uint8* data) noexcept;

    /** Ends everything sounding, so nothing hangs when the music stops. */
    void endSoundingNotes (juce::MidiBuffer&, int sampleOffset) noexcept;

private:
    bool continuesFrom (int previous, int next) const noexcept;
    void renderSegment (juce::MidiBuffer&, const Clip&, const MidiTake&, juce::int64 fromTick, juce::int64 toTick,
                        juce::int64 timelinePosition, int numSamples, double samplesPerTick) noexcept;

    ClipList clips;
    std::vector<std::shared_ptr<const MidiTake>> takes;

    static constexpr int noClip = -1;
    int lastClip = noClip;

    std::array<std::bitset<128>, 16> soundingNotes;
    std::bitset<16> channelsUsed;
};

} // namespace roughtrack
