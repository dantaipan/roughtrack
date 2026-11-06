#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace roughtrack
{

/** A stretch of an audio file placed on the timeline. Editing only ever changes
    these numbers, so the recording itself is never altered.
*/
struct Clip
{
    juce::File file;
    juce::int64 timelineStart = 0;
    juce::int64 fileOffset = 0;
    juce::int64 length = 0;
    juce::int64 fileLength = 0;

    /** Repeats until the next clip on the track starts, or to the end of the song. */
    bool loopsToNext = false;

    juce::int64 timelineEnd() const noexcept { return timelineStart + length; }
    bool operator== (const Clip&) const = default;
};

/** Later clips play over earlier ones, so a punch-in only replaces what it covers. */
using ClipList = std::vector<Clip>;

/** A run of timeline where one clip plays, or nothing does if clip is -1. */
struct PlaySegment
{
    int clip = -1;
    juce::int64 length = 0;
};

PlaySegment findSegment (const ClipList&, juce::int64 position, juce::int64 maximumLength);

/** How far a position is from where the audio cuts to something else. Two pieces
    of a split clip that still line up aren't an edge, because nothing changes.
*/
struct EdgeDistances
{
    juce::int64 sinceEdge = 0;
    juce::int64 untilEdge = 0;
};

EdgeDistances findEdgeDistances (const ClipList&, juce::int64 position, juce::int64 searchDistance);

/** Short enough to keep timing tight, long enough that a cut doesn't click. */
inline constexpr juce::int64 edgeFadeSamples = 96;

/** The clip heard at a position, or -1. */
int findClipAt (const ClipList&, juce::int64 position);

ClipList moveClip (ClipList, int index, juce::int64 newTimelineStart);
ClipList trimClipStart (ClipList, int index, juce::int64 newTimelineStart);
ClipList trimClipEnd (ClipList, int index, juce::int64 newTimelineEnd);
ClipList splitClip (ClipList, int index, juce::int64 position);
ClipList removeClip (ClipList, int index);

} // namespace roughtrack
