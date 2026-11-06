#include "TestHelpers.h"

#include "roughtrack/AudioEngine.h"
#include "roughtrack/Loops.h"
#include "roughtrack/Session.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace roughtrack;
using Catch::Approx;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;
constexpr float rampStep = 1.0e-5f;

Clip makeClip (juce::int64 start, juce::int64 length, bool loops = false, juce::int64 offset = 0)
{
    Clip clip;
    clip.file = juce::File ("/take.wav");
    clip.timelineStart = start;
    clip.fileOffset = offset;
    clip.length = length;
    clip.fileLength = 100000;
    clip.loopsToNext = loops;
    return clip;
}

DrumPart makePart (int startBar, int bars, bool loops = false)
{
    DrumPart part;
    part.groove = "beat";
    part.startBar = startBar;
    part.bars = bars;
    part.loopsToNext = loops;
    return part;
}

/** Each sample holds its own position, so what plays shows which part of the file was read. */
juce::File writeRamp (const juce::File& directory, int length)
{
    juce::AudioBuffer<float> take (1, length);

    for (int i = 0; i < length; ++i)
        take.setSample (0, i, (float) (i + 1) * rampStep);

    const auto file = directory.getChildFile ("ramp.wav");
    test::writeWav (file, take, sampleRate);
    return file;
}

struct Rig
{
    Rig()
    {
        test::useTracks (engine, 2);
        engine.setSessionDirectory (temp.directory);
        engine.prepare (sampleRate, blockSize);
        engine.setRecordingLatency (0);
    }

    std::vector<float> play (int numSamples)
    {
        REQUIRE (engine.play());

        std::vector<float> input ((size_t) blockSize), left ((size_t) blockSize), right ((size_t) blockSize), out;
        const float* inputs[] { input.data() };

        while ((int) out.size() < numSamples)
        {
            float* outputs[] { left.data(), right.data() };
            engine.processBlock (inputs, 1, outputs, 2, blockSize);
            out.insert (out.end(), left.begin(), left.end());
        }

        engine.stop();
        return out;
    }

    test::TempDirectory temp;
    AudioEngine engine;
};

} // namespace

TEST_CASE ("A loop stops where the next clip starts, wherever that clip is in the list")
{
    // The later clip was recorded first, so it's earlier in the list.
    const ClipList clips { makeClip (5000, 1000), makeClip (0, 1000, true) };

    CHECK (findLoopEnd (clips, 1, 100000) == 5000);

    const auto played = expandLoops (clips, 100000);

    REQUIRE (played.size() == 6);
    CHECK (played[0].timelineStart == 5000);

    for (int pass = 0; pass < 5; ++pass)
    {
        CHECK (played[(size_t) pass + 1].timelineStart == pass * 1000);
        CHECK (played[(size_t) pass + 1].length == 1000);
    }
}

TEST_CASE ("The last pass is cut short where the next clip starts")
{
    const auto played = expandLoops ({ makeClip (0, 1000, true, 200), makeClip (2500, 1000) }, 100000);

    REQUIRE (played.size() == 4);
    CHECK (played[2].timelineStart == 2000);
    CHECK (played[2].length == 500);
    CHECK (played[2].fileOffset == 200);
    CHECK_FALSE (played[2].loopsToNext);
}

TEST_CASE ("A last clip loops to the end of the song")
{
    CHECK (findLoopEnd ({ makeClip (0, 1000, true) }, 0, 4200) == 4200);

    const auto played = expandLoops ({ makeClip (0, 1000, true) }, 4200);

    REQUIRE (played.size() == 5);
    CHECK (played.back().length == 200);
}

TEST_CASE ("A loop never ends before its own end")
{
    // Nothing else in the session makes the song end where the clip does.
    CHECK (expandLoops ({ makeClip (0, 1000, true) }, 1000).size() == 1);

    // A clip starting inside the first pass leaves no room to repeat.
    CHECK (expandLoops ({ makeClip (0, 1000, true), makeClip (600, 1000) }, 100000).size() == 2);
}

TEST_CASE ("Clips that don't loop play as they are")
{
    const ClipList clips { makeClip (0, 1000), makeClip (3000, 500) };
    CHECK (expandLoops (clips, 100000) == clips);
}

TEST_CASE ("A looping drum part runs on to the next part")
{
    const auto played = expandLoops ({ makePart (0, 2, true), makePart (7, 1), makePart (12, 2, true) }, 20);

    REQUIRE (played.size() == 3);
    CHECK (played[0].bars == 7);
    CHECK (played[1].bars == 1);
    CHECK (played[2].bars == 8);
    CHECK_FALSE (played[0].loopsToNext);
}

TEST_CASE ("Dragging a loop's end turns it into the clips it was playing")
{
    const auto baked = bakeLoop ({ makeClip (0, 1000, true), makeClip (9000, 100) }, 0, 3400);

    REQUIRE (baked.size() == 5);

    for (int pass = 0; pass < 4; ++pass)
    {
        CHECK (baked[(size_t) pass].timelineStart == pass * 1000);
        CHECK_FALSE (baked[(size_t) pass].loopsToNext);
    }

    CHECK (baked[3].length == 400);
    CHECK (baked[4].timelineStart == 9000);
}

TEST_CASE ("Dragging a loop's end back inside its first pass just shortens it")
{
    const auto baked = bakeLoop ({ makeClip (0, 1000, true) }, 0, 600);

    REQUIRE (baked.size() == 1);
    CHECK (baked[0].length == 600);
    CHECK_FALSE (baked[0].loopsToNext);
}

TEST_CASE ("A looping audio clip plays its start again on each pass")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, 4800);

    auto clip = makeClip (0, 4800, true);
    clip.file = file;
    clip.fileLength = 4800;

    // Another track sets where the song ends.
    auto other = makeClip (4800 * 2, 4800);
    other.file = file;
    other.fileLength = 4800;

    REQUIRE (rig.engine.setClips (0, { clip }).wasOk());
    REQUIRE (rig.engine.setClips (1, { other }).wasOk());
    rig.engine.setTrackMuted (1, true);

    CHECK (rig.engine.getLoopEnd (0, 0) == 4800 * 3);

    const auto out = rig.play (4800 * 3);

    // Halfway through each pass is halfway through the take, away from the fades at the joins.
    for (int pass = 0; pass < 3; ++pass)
        CHECK (out[(size_t) (pass * 4800 + 2400)] == Approx (2401 * rampStep).margin (1.0e-4));
}

TEST_CASE ("The mixdown runs to the end of the loops")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (0, { makeClip (0, 1000, true), makeClip (8000, 500) }).wasOk());

    CHECK (rig.engine.getSessionLength() == 8500);
}

TEST_CASE ("Loops are saved with the session, and old sessions don't loop")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, 1000);

    auto clip = makeClip (0, 1000, true);
    clip.file = file;
    REQUIRE (rig.engine.setClips (0, { clip }).wasOk());

    auto xml = createSessionXml (rig.engine, rig.temp.directory);

    AudioEngine reopened;
    reopened.setSessionDirectory (rig.temp.directory);
    reopened.prepare (sampleRate, blockSize);
    applySessionXml (reopened, *xml, rig.temp.directory);

    REQUIRE (reopened.getClips (0).size() == 1);
    CHECK (reopened.getClips (0)[0].loopsToNext);

    // A session from before looping has no loop attribute at all.
    for (auto* track : xml->getChildWithTagNameIterator ("TRACK"))
        for (auto* clipXml : track->getChildWithTagNameIterator ("CLIP"))
            clipXml->removeAttribute ("loop");

    AudioEngine older;
    older.setSessionDirectory (rig.temp.directory);
    older.prepare (sampleRate, blockSize);
    applySessionXml (older, *xml, rig.temp.directory);

    REQUIRE (older.getClips (0).size() == 1);
    CHECK_FALSE (older.getClips (0)[0].loopsToNext);
}
