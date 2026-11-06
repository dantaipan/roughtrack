#include "roughtrack/Loops.h"

namespace roughtrack
{

namespace
{

/** The nearest start after the item's own, or -1 if nothing starts later. */
template <typename List, typename StartOf>
juce::int64 findNextStart (const List& items, int index, StartOf startOf)
{
    const auto start = startOf (items[(size_t) index]);
    juce::int64 next = -1;

    for (const auto& item : items)
        if (const auto other = startOf (item); other > start && (next < 0 || other < next))
            next = other;

    return next;
}

/** Copies of a clip laid end to end from its own end up to a given end, the last cut short. */
void addRepeats (ClipList& result, const Clip& clip, juce::int64 end)
{
    if (clip.length <= 0)
        return;

    for (auto start = clip.timelineEnd(); start < end; start += clip.length)
    {
        auto copy = clip;
        copy.timelineStart = start;
        copy.length = juce::jmin (clip.length, end - start);
        copy.loopsToNext = false;
        result.push_back (copy);
    }
}

} // namespace

juce::int64 findLoopEnd (const ClipList& clips, int index, juce::int64 songEnd)
{
    const auto& clip = clips[(size_t) index];
    const auto next = findNextStart (clips, index, [] (const Clip& c) { return c.timelineStart; });

    return juce::jmax (clip.timelineEnd(), next >= 0 ? next : songEnd);
}

int findLoopEnd (const DrumPartList& parts, int index, int songEndBar)
{
    const auto& part = parts[(size_t) index];
    const auto next = findNextStart (parts, index, [] (const DrumPart& p) { return (juce::int64) p.startBar; });

    return juce::jmax (part.endBar(), next >= 0 ? (int) next : songEndBar);
}

ClipList expandLoops (const ClipList& clips, juce::int64 songEnd)
{
    ClipList result;
    result.reserve (clips.size());

    for (int index = 0; index < (int) clips.size(); ++index)
    {
        auto clip = clips[(size_t) index];
        const auto loops = clip.loopsToNext;

        clip.loopsToNext = false;
        result.push_back (clip);

        if (loops)
            addRepeats (result, clip, findLoopEnd (clips, index, songEnd));
    }

    return result;
}

DrumPartList expandLoops (const DrumPartList& parts, int songEndBar)
{
    auto result = parts;

    for (int index = 0; index < (int) parts.size(); ++index)
    {
        auto& part = result[(size_t) index];

        if (part.loopsToNext)
            part.bars = findLoopEnd (parts, index, songEndBar) - part.startBar;

        part.loopsToNext = false;
    }

    return result;
}

ClipList bakeLoop (const ClipList& clips, int index, juce::int64 newEnd)
{
    auto clip = clips[(size_t) index];
    clip.loopsToNext = false;

    ClipList baked;
    auto first = clip;
    first.length = juce::jlimit ((juce::int64) 1, clip.length, newEnd - clip.timelineStart);
    baked.push_back (first);

    addRepeats (baked, clip, newEnd);

    auto result = clips;
    result.erase (result.begin() + index);
    result.insert (result.begin() + index, baked.begin(), baked.end());
    return result;
}

} // namespace roughtrack
