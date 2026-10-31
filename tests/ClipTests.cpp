#include "rifftrack/Clip.h"

#include <catch2/catch_test_macros.hpp>

using namespace rifftrack;

namespace
{

Clip makeClip (juce::int64 start, juce::int64 length, juce::int64 offset = 0, juce::int64 fileLength = 10000)
{
    Clip clip;
    clip.timelineStart = start;
    clip.length = length;
    clip.fileOffset = offset;
    clip.fileLength = fileLength;
    return clip;
}

} // namespace

TEST_CASE ("A later clip plays over an earlier one for only the part it covers")
{
    const ClipList clips { makeClip (0, 1000), makeClip (400, 200) };

    CHECK (findClipAt (clips, 399) == 0);
    CHECK (findClipAt (clips, 400) == 1);
    CHECK (findClipAt (clips, 599) == 1);
    CHECK (findClipAt (clips, 600) == 0);
    CHECK (findClipAt (clips, 1000) == -1);
}

TEST_CASE ("Segments break exactly where the playing clip changes")
{
    const ClipList clips { makeClip (100, 1000), makeClip (400, 200) };

    const auto check = [&] (juce::int64 position, int clip, juce::int64 length)
    {
        const auto segment = findSegment (clips, position, 5000);
        CHECK (segment.clip == clip);
        CHECK (segment.length == length);
    };

    check (0, -1, 100);
    check (100, 0, 300);
    check (400, 1, 200);
    check (600, 0, 500);
    check (1100, -1, 5000);
}

TEST_CASE ("A segment never exceeds what was asked for")
{
    CHECK (findSegment ({ makeClip (0, 1000) }, 0, 64).length == 64);
}

TEST_CASE ("Moving a clip keeps its audio and length")
{
    const auto moved = moveClip ({ makeClip (100, 500, 50) }, 0, 900);

    CHECK (moved[0].timelineStart == 900);
    CHECK (moved[0].fileOffset == 50);
    CHECK (moved[0].length == 500);
}

TEST_CASE ("Trimming the start leaves later audio where it was")
{
    const auto trimmed = trimClipStart ({ makeClip (100, 500) }, 0, 300);

    CHECK (trimmed[0].timelineStart == 300);
    CHECK (trimmed[0].fileOffset == 200);
    CHECK (trimmed[0].length == 300);
    CHECK (trimmed[0].timelineEnd() == 600);
}

TEST_CASE ("Trimming can recover audio but can't go past the file's start")
{
    const auto trimmed = trimClipStart ({ makeClip (300, 300, 200) }, 0, -5000);

    CHECK (trimmed[0].timelineStart == 100);
    CHECK (trimmed[0].fileOffset == 0);
    CHECK (trimmed[0].length == 500);
}

TEST_CASE ("Trimming the end stops at the end of the file")
{
    CHECK (trimClipEnd ({ makeClip (0, 500, 0, 800) }, 0, 300)[0].length == 300);
    CHECK (trimClipEnd ({ makeClip (0, 500, 0, 800) }, 0, 5000)[0].length == 800);
}

TEST_CASE ("Trimming can't remove a whole clip")
{
    CHECK (trimClipStart ({ makeClip (0, 500) }, 0, 5000)[0].length > 0);
    CHECK (trimClipEnd ({ makeClip (0, 500) }, 0, -5000)[0].length > 0);
}

TEST_CASE ("Splitting makes two clips that play the same audio")
{
    const auto split = splitClip ({ makeClip (100, 500, 40) }, 0, 300);

    REQUIRE (split.size() == 2);
    CHECK (split[0].timelineStart == 100);
    CHECK (split[0].length == 200);
    CHECK (split[1].timelineStart == 300);
    CHECK (split[1].fileOffset == 240);
    CHECK (split[1].length == 300);
    CHECK (split[1].timelineEnd() == 600);
}

TEST_CASE ("Splitting outside a clip does nothing")
{
    CHECK (splitClip ({ makeClip (100, 500) }, 0, 100).size() == 1);
    CHECK (splitClip ({ makeClip (100, 500) }, 0, 600).size() == 1);
}

TEST_CASE ("Splitting keeps a clip's place in the layers")
{
    auto clips = ClipList { makeClip (0, 1000), makeClip (200, 400) };
    clips = splitClip (clips, 0, 500);

    REQUIRE (clips.size() == 3);
    CHECK (findClipAt (clips, 300) == 2);
}

TEST_CASE ("Edges are measured from where the audio really changes")
{
    const ClipList clips { makeClip (1000, 1000) };

    const auto distances = findEdgeDistances (clips, 1100, 500);

    CHECK (distances.sinceEdge == 100);
    CHECK (distances.untilEdge == 500);
    CHECK (findEdgeDistances (clips, 1950, 500).untilEdge == 50);
}

TEST_CASE ("A split that hasn't been moved isn't an edge")
{
    const auto clips = splitClip ({ makeClip (0, 1000) }, 0, 500);

    const auto distances = findEdgeDistances (clips, 500, 96);

    CHECK (distances.sinceEdge == 96);
    CHECK (distances.untilEdge == 96);
}

TEST_CASE ("Moving part of a split makes it an edge")
{
    auto clips = splitClip ({ makeClip (0, 1000) }, 0, 500);
    clips = moveClip (clips, 1, 600);

    CHECK (findEdgeDistances (clips, 490, 96).untilEdge == 10);
}

TEST_CASE ("A clip buried under another adds no edge to what's heard")
{
    const ClipList clips { makeClip (500, 100, 0), makeClip (0, 1000, 0) };

    const auto distances = findEdgeDistances (clips, 550, 96);

    CHECK (distances.sinceEdge == 96);
    CHECK (distances.untilEdge == 96);
}
