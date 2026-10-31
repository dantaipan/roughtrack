#include "rifftrack/Clip.h"

namespace rifftrack
{

namespace
{
constexpr juce::int64 minimumClipLength = 64;

/** True if the audio carries straight on across a timeline position. */
bool isSeamless (const ClipList& clips, juce::int64 edge)
{
    const auto before = findClipAt (clips, edge - 1);
    const auto after = findClipAt (clips, edge);

    if (before < 0 || after < 0)
        return before == after;

    const auto& a = clips[(size_t) before];
    const auto& b = clips[(size_t) after];

    return a.file == b.file && a.fileOffset - a.timelineStart == b.fileOffset - b.timelineStart;
}
} // namespace

PlaySegment findSegment (const ClipList& clips, juce::int64 position, juce::int64 maximumLength)
{
    const auto clip = findClipAt (clips, position);
    auto length = maximumLength;

    if (clip >= 0)
        length = juce::jmin (length, clips[(size_t) clip].timelineEnd() - position);

    // The segment ends where anything that would play on top of it begins.
    for (size_t i = (size_t) (clip + 1); i < clips.size(); ++i)
        if (clips[i].timelineStart > position)
            length = juce::jmin (length, clips[i].timelineStart - position);

    return { clip, length };
}

EdgeDistances findEdgeDistances (const ClipList& clips, juce::int64 position, juce::int64 searchDistance)
{
    EdgeDistances distances { searchDistance, searchDistance };

    for (const auto& clip : clips)
    {
        for (const auto edge : { clip.timelineStart, clip.timelineEnd() })
        {
            if (isSeamless (clips, edge))
                continue;

            if (edge <= position)
                distances.sinceEdge = juce::jmin (distances.sinceEdge, position - edge);

            if (edge > position)
                distances.untilEdge = juce::jmin (distances.untilEdge, edge - position);
        }
    }

    return distances;
}

int findClipAt (const ClipList& clips, juce::int64 position)
{
    for (auto i = (int) clips.size() - 1; i >= 0; --i)
    {
        const auto& clip = clips[(size_t) i];

        if (position >= clip.timelineStart && position < clip.timelineEnd())
            return i;
    }

    return -1;
}

ClipList moveClip (ClipList clips, int index, juce::int64 newTimelineStart)
{
    auto& clip = clips[(size_t) index];
    clip.timelineStart = juce::jmax (newTimelineStart, 1 - clip.length);
    return clips;
}

ClipList trimClipStart (ClipList clips, int index, juce::int64 newTimelineStart)
{
    auto& clip = clips[(size_t) index];

    const auto earliest = clip.timelineStart - clip.fileOffset;
    const auto latest = clip.timelineEnd() - minimumClipLength;
    const auto start = juce::jlimit (earliest, juce::jmax (earliest, latest), newTimelineStart);
    const auto change = start - clip.timelineStart;

    clip.timelineStart = start;
    clip.fileOffset += change;
    clip.length -= change;
    return clips;
}

ClipList trimClipEnd (ClipList clips, int index, juce::int64 newTimelineEnd)
{
    auto& clip = clips[(size_t) index];

    const auto longest = clip.fileLength - clip.fileOffset;
    const auto requested = newTimelineEnd - clip.timelineStart;

    clip.length = juce::jlimit (juce::jmin (minimumClipLength, longest), longest, requested);
    return clips;
}

ClipList splitClip (ClipList clips, int index, juce::int64 position)
{
    const auto first = clips[(size_t) index];

    if (position <= first.timelineStart || position >= first.timelineEnd())
        return clips;

    const auto firstLength = position - first.timelineStart;

    auto second = first;
    second.timelineStart = position;
    second.fileOffset += firstLength;
    second.length -= firstLength;

    clips[(size_t) index].length = firstLength;
    clips.insert (clips.begin() + index + 1, second);
    return clips;
}

ClipList removeClip (ClipList clips, int index)
{
    clips.erase (clips.begin() + index);
    return clips;
}

} // namespace rifftrack
