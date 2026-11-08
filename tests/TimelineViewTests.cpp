#include "TestHelpers.h"
#include "TimelineView.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace roughtrack;

namespace
{

constexpr int laneHeight = 76;
constexpr double sampleRate = 48000.0;
constexpr juce::int64 samplesPerPixel = 1200;

Clip makeClip (juce::int64 start, juce::int64 length, juce::int64 offset = 0, juce::int64 fileLength = 960000)
{
    Clip clip;
    clip.file = juce::File ("/nonexistent/take.wav");
    clip.timelineStart = start;
    clip.length = length;
    clip.fileOffset = offset;
    clip.fileLength = fileLength;
    return clip;
}

/** Drives the real view with mouse events. It starts at 40 pixels a second,
    so one pixel is 1200 samples at 48 kHz.
*/
struct Rig
{
    Rig() : view (engine, laneHeight)
    {
        test::useTracks (engine);
        engine.prepare (sampleRate, 64);
        view.setSize (800, TimelineView::rulerHeight + 7 * laneHeight + TimelineView::scrollBarHeight);

        // These check that drags move by exactly what the pointer did; snapping has its own tests.
        view.setSnap (SnapGrid::off);

        view.onEdit = [this] (int track, const ClipList& clips, const juce::String& name)
        {
            lastEdit = name;
            ++numEdits;
            engine.setClips (track, clips);
        };

        view.onSeek = [this] (juce::int64 position) { seeks.push_back (position); };
        view.onCycleEdit = [this] (juce::Range<juce::int64> range) { cycleEdits.push_back (range); };
        view.onMessage = [this] (const juce::String& message) { lastMessage = message; };
    }

    juce::MouseEvent event (juce::Point<float> position, juce::Point<float> downAt, juce::ModifierKeys modifiers = {})
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 position, modifiers, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &view, &view, juce::Time::getCurrentTime(), downAt, juce::Time::getCurrentTime(), 1, false);
    }

    void drag (juce::Point<float> from, juce::Point<float> to)
    {
        view.mouseDown (event (from, from, juce::ModifierKeys::leftButtonModifier));
        view.mouseDrag (event (to, from, juce::ModifierKeys::leftButtonModifier));
        view.mouseUp (event (to, from));
    }

    static float laneY (int track)
    {
        return (float) (TimelineView::rulerHeight + track * laneHeight + laneHeight / 2);
    }

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    AudioEngine engine;
    TimelineView view;

    juce::String lastEdit, lastMessage;
    int numEdits = 0;
    std::vector<juce::int64> seeks;
    std::vector<juce::Range<juce::int64>> cycleEdits;
};

/** Halfway down the bar numbers, under the time in the ruler. */
constexpr float barNumbersY = (float) TimelineView::rulerHeight * 0.75f;

/** At 120 bpm and 40 pixels a second, a bar is 96000 samples and 80 pixels. */
constexpr juce::int64 bar = 96000;

} // namespace

TEST_CASE ("Dragging a clip's body moves it")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (2, { makeClip (120000, 480000) }).wasOk());

    rig.drag ({ 300.0f, Rig::laneY (2) }, { 350.0f, Rig::laneY (2) });

    CHECK (rig.lastEdit == "Move Clip");
    CHECK (rig.engine.getClips (2)[0].timelineStart == 120000 + 50 * samplesPerPixel);
    CHECK (rig.engine.getClips (2)[0].length == 480000);
}

TEST_CASE ("Dragging a clip's left edge trims its start")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (2, { makeClip (120000, 480000) }).wasOk());

    rig.drag ({ 102.0f, Rig::laneY (2) }, { 122.0f, Rig::laneY (2) });

    const auto& clip = rig.engine.getClips (2)[0];

    CHECK (rig.lastEdit == "Trim Clip");
    CHECK (clip.timelineStart == 120000 + 20 * samplesPerPixel);
    CHECK (clip.fileOffset == 20 * samplesPerPixel);
    CHECK (clip.timelineEnd() == 600000);
}

TEST_CASE ("Dragging a clip's right edge trims its end")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (2, { makeClip (120000, 480000) }).wasOk());

    rig.drag ({ 498.0f, Rig::laneY (2) }, { 448.0f, Rig::laneY (2) });

    const auto& clip = rig.engine.getClips (2)[0];

    CHECK (clip.timelineStart == 120000);
    CHECK (clip.length == 480000 - 50 * samplesPerPixel);
}

TEST_CASE ("Clicking without dragging changes nothing")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (2, { makeClip (120000, 480000) }).wasOk());

    rig.drag ({ 300.0f, Rig::laneY (2) }, { 300.0f, Rig::laneY (2) });

    CHECK (rig.numEdits == 0);
}

TEST_CASE ("A drag lands on the clip in the lane under the mouse")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (2, { makeClip (120000, 480000) }).wasOk());
    REQUIRE (rig.engine.setClips (3, { makeClip (120000, 480000) }).wasOk());

    rig.drag ({ 300.0f, Rig::laneY (3) }, { 350.0f, Rig::laneY (3) });

    CHECK (rig.engine.getClips (2)[0].timelineStart == 120000);
    CHECK (rig.engine.getClips (3)[0].timelineStart != 120000);
}

TEST_CASE ("Clips can't be dragged while the transport runs")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (2, { makeClip (120000, 480000) }).wasOk());
    REQUIRE (rig.engine.play());

    rig.drag ({ 300.0f, Rig::laneY (2) }, { 350.0f, Rig::laneY (2) });

    CHECK (rig.numEdits == 0);
    CHECK (rig.lastMessage.isNotEmpty());
    rig.engine.stop();
}

TEST_CASE ("Clicking the ruler moves the playhead there")
{
    Rig rig;
    const juce::Point<float> point { 200.0f, 10.0f };

    rig.view.mouseDown (rig.event (point, point, juce::ModifierKeys::leftButtonModifier));

    REQUIRE (rig.seeks.size() == 1);
    CHECK (rig.seeks[0] == 200 * samplesPerPixel);
}

TEST_CASE ("The selected clip can be deleted")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (2, { makeClip (120000, 240000), makeClip (480000, 240000) }).wasOk());

    CHECK_FALSE (rig.view.deleteSelectedClip());

    rig.drag ({ 500.0f, Rig::laneY (2) }, { 500.0f, Rig::laneY (2) });

    CHECK (rig.view.deleteSelectedClip());
    REQUIRE (rig.engine.getClips (2).size() == 1);
    CHECK (rig.engine.getClips (2)[0].timelineStart == 120000);
    CHECK_FALSE (rig.view.deleteSelectedClip());
}

TEST_CASE ("Lanes follow the tracks in use, not their numbers")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (2, { makeClip (120000, 480000) }).wasOk());
    REQUIRE (rig.engine.removeTrack (0).wasOk());
    REQUIRE (rig.engine.removeTrack (1).wasOk());
    rig.view.refresh();

    rig.drag ({ 300.0f, Rig::laneY (0) }, { 350.0f, Rig::laneY (0) });

    CHECK (rig.engine.getClips (2)[0].timelineStart != 120000);
}

TEST_CASE ("A click below the last track hits nothing")
{
    Rig rig;

    for (int track = 1; track < 7; ++track)
        REQUIRE (rig.engine.removeTrack (track).wasOk());

    rig.view.refresh();
    rig.drag ({ 300.0f, Rig::laneY (3) }, { 350.0f, Rig::laneY (3) });

    CHECK (rig.numEdits == 0);
}

TEST_CASE ("Scrolling the lanes keeps clicks on the right track")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (3, { makeClip (120000, 480000) }).wasOk());

    rig.view.setVerticalOffset (laneHeight * 2);
    rig.drag ({ 300.0f, Rig::laneY (1) }, { 350.0f, Rig::laneY (1) });

    CHECK (rig.engine.getClips (3)[0].timelineStart != 120000);
}

TEST_CASE ("Drum parts drag by whole bars")
{
    Rig rig;
    test::TempDirectory temp;

    test::writeTestDrums (temp.directory);
    rig.engine.setDrumFolder (temp.directory);
    REQUIRE (rig.engine.loadDrums().wasOk());

    const auto track = rig.engine.addTrack ("Drums", TrackType::drums);
    const auto y = (float) (TimelineView::rulerHeight + laneHeight * 7 + laneHeight / 2);

    rig.view.setSize (800, TimelineView::rulerHeight + 8 * laneHeight + TimelineView::scrollBarHeight);
    rig.view.onDrumEdit = [&] (int edited, const DrumPartList& parts, const juce::String& name)
    {
        rig.lastEdit = name;
        rig.engine.setDrumParts (edited, parts);
    };

    REQUIRE (rig.engine.setDrumParts (track, { { "beat", 2, 2, 0 } }).wasOk());

    // At 120 BPM a bar lasts two seconds, which is 80 pixels.
    rig.drag ({ 200.0f, y }, { 310.0f, y });

    CHECK (rig.lastEdit == "Move Part");
    CHECK (rig.engine.getDrumParts (track)[0].startBar == 3);
}

TEST_CASE ("Dragging a drum part's right edge makes it longer")
{
    Rig rig;
    test::TempDirectory temp;

    test::writeTestDrums (temp.directory);
    rig.engine.setDrumFolder (temp.directory);
    REQUIRE (rig.engine.loadDrums().wasOk());

    const auto track = rig.engine.addTrack ("Drums", TrackType::drums);
    const auto y = (float) (TimelineView::rulerHeight + laneHeight * 7 + laneHeight / 2);

    rig.view.setSize (800, TimelineView::rulerHeight + 8 * laneHeight + TimelineView::scrollBarHeight);
    rig.view.onDrumEdit = [&] (int edited, const DrumPartList& parts, const juce::String&) { rig.engine.setDrumParts (edited, parts); };

    REQUIRE (rig.engine.setDrumParts (track, { { "beat", 0, 2, 0 } }).wasOk());

    rig.drag ({ 158.0f, y }, { 320.0f, y });

    CHECK (rig.engine.getDrumParts (track)[0].bars == 4);
}

namespace
{

Groove makeGroove (const juce::String& name, int bpm, bool isFill = false, const juce::String& style = "Blues")
{
    Groove groove;
    groove.name = name;
    groove.style = style;
    groove.recordedBpm = bpm;
    groove.isFill = isFill;
    return groove;
}

/** The menu's lines in order, with section headers in brackets. */
juce::StringArray listMenu (const juce::PopupMenu& menu)
{
    juce::StringArray lines;

    for (juce::PopupMenu::MenuItemIterator item (menu); item.next();)
        lines.add (item.getItem().isSectionHeader ? "[" + item.getItem().text + "]" : item.getItem().text);

    return lines;
}

const std::vector<Groove> menuGrooves { makeGroove ("Slow", 72), makeGroove ("Close", 92), makeGroove ("Exact", 96), makeGroove ("Edge", 100),
                                        makeGroove ("Past edge", 101), makeGroove ("Fill", 95, true), makeGroove ("Rock", 96, false, "Rock") };

} // namespace

TEST_CASE ("The groove menu lists what suits the tempo first")
{
    CHECK (listMenu (TimelineView::createGrooveMenu (menuGrooves, "Blues", false, 96.0))
           == juce::StringArray { "[Near 96 BPM]", "Close", "Exact", "Edge", "[Other Tempos]", "Slow", "Past edge" });
}

TEST_CASE ("The groove menu follows the tempo")
{
    CHECK (listMenu (TimelineView::createGrooveMenu (menuGrooves, "Blues", false, 72.0))
           == juce::StringArray { "[Near 72 BPM]", "Slow", "[Other Tempos]", "Close", "Exact", "Edge", "Past edge" });
}

TEST_CASE ("The groove menu has no headings when nothing is near the tempo")
{
    CHECK (listMenu (TimelineView::createGrooveMenu (menuGrooves, "Blues", false, 200.0))
           == juce::StringArray { "Slow", "Close", "Exact", "Edge", "Past edge" });
}

TEST_CASE ("The groove menu keeps fills and grooves apart")
{
    CHECK (listMenu (TimelineView::createGrooveMenu (menuGrooves, "Blues", true, 96.0)) == juce::StringArray { "[Near 96 BPM]", "Fill" });
}

TEST_CASE ("The groove menu leaves nothing out at any tempo")
{
    for (const auto tempo : { 30.0, 72.0, 96.0, 300.0 })
    {
        auto numGrooves = 0;

        for (const auto& line : listMenu (TimelineView::createGrooveMenu (menuGrooves, "Blues", false, tempo)))
            numGrooves += line.startsWith ("[") ? 0 : 1;

        CHECK (numGrooves == 5);
    }
}


TEST_CASE ("A hovered groove waits before it plays")
{
    HoverDelay delay;

    CHECK (delay.update (3, 0.0) == HoverDelay::Change::none);
    CHECK (delay.update (3, HoverDelay::delayMs - 1.0) == HoverDelay::Change::none);
    CHECK (delay.getProgress (HoverDelay::delayMs / 2.0) == Catch::Approx (0.5f));

    CHECK (delay.update (3, HoverDelay::delayMs) == HoverDelay::Change::start);
    CHECK (delay.getPlaying() == 3);

    // It only starts once.
    CHECK (delay.update (3, HoverDelay::delayMs * 2.0) == HoverDelay::Change::none);
}

TEST_CASE ("Sweeping past grooves plays nothing")
{
    HoverDelay delay;

    for (int item = 0; item < 10; ++item)
        CHECK (delay.update (item, item * 100.0) == HoverDelay::Change::none);

    CHECK (delay.getPlaying() == -1);
}

TEST_CASE ("Moving to another groove stops the first and waits again")
{
    HoverDelay delay;
    delay.update (1, 0.0);
    REQUIRE (delay.update (1, HoverDelay::delayMs) == HoverDelay::Change::start);

    CHECK (delay.update (2, 1000.0) == HoverDelay::Change::stop);
    CHECK (delay.update (2, 1000.0 + HoverDelay::delayMs - 1.0) == HoverDelay::Change::none);
    CHECK (delay.update (2, 1000.0 + HoverDelay::delayMs) == HoverDelay::Change::start);
}

TEST_CASE ("Leaving the grooves stops the preview")
{
    HoverDelay delay;
    delay.update (4, 0.0);
    REQUIRE (delay.update (4, HoverDelay::delayMs) == HoverDelay::Change::start);

    CHECK (delay.update (-1, 2000.0) == HoverDelay::Change::stop);
    CHECK (delay.getPlaying() == -1);
    CHECK (delay.getProgress (3000.0) == 0.0f);
}

TEST_CASE ("Groove menus with a preview still list their grooves")
{
    test::TempDirectory temp;
    AudioEngine engine;
    auto preview = std::make_shared<GroovePreview> (engine);

    CHECK (listMenu (TimelineView::createGrooveMenu (menuGrooves, "Blues", false, 96.0, preview))
           == juce::StringArray { "[Near 96 BPM]", "Close", "Exact", "Edge", "[Other Tempos]", "Slow", "Past edge" });
}

TEST_CASE ("The loop menu item is ticked while a part loops")
{
    const auto on = TimelineView::createLoopItem (true);
    const auto off = TimelineView::createLoopItem (false);

    CHECK (on.text == "Loop Until Next Part");
    CHECK (on.isTicked);
    CHECK_FALSE (off.isTicked);
    CHECK (on.itemID == off.itemID);
}

TEST_CASE ("A MIDI clip recorded off the grid lands on it when trimmed")
{
    // Record was pressed a little after bar 2 and Stop a little before bar 5.
    Clip clip;
    clip.file = juce::File ("/keys.mid");
    clip.timelineStart = 3840 + 137;
    clip.length = 3 * 3840 - 300;
    clip.fileLength = clip.length;

    // Each edge is dragged inward, short of a bar line, and lands on it.
    auto clips = TimelineView::dragClip ({ clip }, 0, TimelineView::Zone::start, 3500, SnapGrid::bar, ticksPerBeat, 0);
    CHECK (clips[0].timelineStart == 2 * 3840);

    clips = TimelineView::dragClip (clips, 0, TimelineView::Zone::end, -3500, SnapGrid::bar, ticksPerBeat, 0);
    CHECK (clips[0].timelineEnd() == 3 * 3840);
    CHECK (clips[0].length == 3840);
}

TEST_CASE ("A moved clip's start lands on the grid, and Off leaves it where it's dropped")
{
    Clip clip;
    clip.file = juce::File ("/take.wav");
    clip.timelineStart = 1234;
    clip.length = 96000;
    clip.fileLength = 96000;

    CHECK (TimelineView::dragClip ({ clip }, 0, TimelineView::Zone::body, 50000, SnapGrid::beat, 24000.0, 0)[0].timelineStart == 48000);
    CHECK (TimelineView::dragClip ({ clip }, 0, TimelineView::Zone::body, 50000, SnapGrid::off, 24000.0, 0)[0].timelineStart == 51234);
}

TEST_CASE ("Dragging a loop's end snaps where the repeats stop")
{
    Clip clip;
    clip.file = juce::File ("/take.wav");
    clip.length = 96000;
    clip.fileLength = 96000;
    clip.loopsToNext = true;

    // The loop ran to 500000; dragged back a little, it stops on the nearest bar.
    const auto baked = TimelineView::dragClip ({ clip }, 0, TimelineView::Zone::end, -30000, SnapGrid::bar, 24000.0, 500000);

    CHECK (baked.back().timelineEnd() == 480000);
    CHECK_FALSE (baked.front().loopsToNext);
}

TEST_CASE ("Dragging along the bar numbers marks a loop on whole bars")
{
    Rig rig;
    rig.view.setSnap (SnapGrid::bar);

    rig.drag ({ 100.0f, barNumbersY }, { 250.0f, barNumbersY });

    REQUIRE (rig.cycleEdits.size() == 1);
    CHECK (rig.cycleEdits[0] == juce::Range<juce::int64> { bar, 3 * bar });
    CHECK (rig.seeks.empty());

    // Dragging backwards marks it just the same.
    rig.drag ({ 250.0f, barNumbersY }, { 100.0f, barNumbersY });
    CHECK (rig.cycleEdits.back() == juce::Range<juce::int64> { bar, 3 * bar });
}

TEST_CASE ("Clicking the bar numbers moves the playhead instead")
{
    Rig rig;
    rig.drag ({ 100.0f, barNumbersY }, { 101.0f, barNumbersY });

    CHECK (rig.cycleEdits.empty());
    REQUIRE (rig.seeks.size() == 1);
    CHECK (rig.seeks[0] == 100 * samplesPerPixel);
}

TEST_CASE ("A loop's edges and middle can be dragged")
{
    Rig rig;
    rig.view.setSnap (SnapGrid::bar);
    rig.engine.setCycleRange ({ bar, 3 * bar });

    SECTION ("its end")
    {
        rig.drag ({ 240.0f, barNumbersY }, { 330.0f, barNumbersY });
        REQUIRE (rig.cycleEdits.size() == 1);
        CHECK (rig.cycleEdits[0] == juce::Range<juce::int64> { bar, 4 * bar });
    }

    SECTION ("its start, which stops a bar short of the end")
    {
        rig.drag ({ 80.0f, barNumbersY }, { 300.0f, barNumbersY });
        REQUIRE (rig.cycleEdits.size() == 1);
        CHECK (rig.cycleEdits[0] == juce::Range<juce::int64> { 2 * bar, 3 * bar });
    }

    SECTION ("the whole loop")
    {
        rig.drag ({ 160.0f, barNumbersY }, { 210.0f, barNumbersY });
        REQUIRE (rig.cycleEdits.size() == 1);
        CHECK (rig.cycleEdits[0] == juce::Range<juce::int64> { 2 * bar, 4 * bar });
    }

    SECTION ("but a click in the middle just moves the playhead")
    {
        rig.drag ({ 160.0f, barNumbersY }, { 160.0f, barNumbersY });
        CHECK (rig.cycleEdits.empty());
        CHECK (rig.seeks.size() == 1);
    }
}

TEST_CASE ("Without snapping a loop is marked anywhere, but never too short to hear")
{
    Rig rig;
    rig.drag ({ 100.0f, barNumbersY }, { 104.0f, barNumbersY });

    REQUIRE (rig.cycleEdits.size() == 1);
    CHECK (rig.cycleEdits[0].getStart() == 100 * samplesPerPixel);
    CHECK (rig.cycleEdits[0].getLength() == AudioEngine::minimumCycleSamples);
}

TEST_CASE ("Clicking the time above the bar numbers moves the playhead at once")
{
    Rig rig;
    rig.view.mouseDown (rig.event ({ 120.0f, 5.0f }, { 120.0f, 5.0f }, juce::ModifierKeys::leftButtonModifier));

    REQUIRE (rig.seeks.size() == 1);
    CHECK (rig.seeks[0] == 120 * samplesPerPixel);
}

namespace
{
/** On a clip's top edge, where its fade handles sit. */
float handleY (int track)
{
    return (float) (TimelineView::rulerHeight + track * laneHeight + 3 + 3);
}
} // namespace

TEST_CASE ("Dragging a clip's top corners sets its fades")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (2, { makeClip (120000, 480000) }).wasOk());

    // The clip runs from x 100 to 500, so with no fades the handles are in its corners.
    rig.drag ({ 103.0f, handleY (2) }, { 123.0f, handleY (2) });

    CHECK (rig.lastEdit == "Fade In");
    CHECK (rig.engine.getClips (2)[0].fadeIn == 20 * samplesPerPixel);
    CHECK (rig.engine.getClips (2)[0].timelineStart == 120000);

    rig.drag ({ 497.0f, handleY (2) }, { 467.0f, handleY (2) });

    CHECK (rig.lastEdit == "Fade Out");
    CHECK (rig.engine.getClips (2)[0].fadeOut == 30 * samplesPerPixel);
    CHECK (rig.engine.getClips (2)[0].length == 480000);

    // The fade-in handle now sits where that fade ends, and dragging it back shortens it.
    rig.drag ({ 120.0f, handleY (2) }, { 110.0f, handleY (2) });
    CHECK (rig.engine.getClips (2)[0].fadeIn == 10 * samplesPerPixel);
}

TEST_CASE ("Below the fade handles, a clip's edges still trim")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (2, { makeClip (120000, 480000) }).wasOk());

    rig.drag ({ 102.0f, Rig::laneY (2) }, { 122.0f, Rig::laneY (2) });

    CHECK (rig.lastEdit == "Trim Clip");
    CHECK (rig.engine.getClips (2)[0].fadeIn == 0);
}

TEST_CASE ("A fade drag that doesn't move changes nothing")
{
    const ClipList clips { makeClip (0, 48000) };

    CHECK (TimelineView::dragClip (clips, 0, TimelineView::Zone::fadeIn, 0, SnapGrid::bar, 24000.0, 0) == clips);
    CHECK (TimelineView::dragClip (clips, 0, TimelineView::Zone::fadeOut, -4800, SnapGrid::bar, 24000.0, 0)[0].fadeOut == 4800);
}
