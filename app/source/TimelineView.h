#pragma once

#include "GroovePreview.h"

#include "roughtrack/AudioEngine.h"
#include "roughtrack/Grid.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

/** Shows each track's clips as waveforms, lined up with its strip, and edits them. */
class TimelineView final : public juce::Component,
                           private juce::ChangeListener,
                           private juce::ScrollBar::Listener
{
public:
    static constexpr int rulerHeight = 36;
    static constexpr int scrollBarHeight = 14;

    /** One style's grooves or fills, with those suited to the tempo first. Given a preview,
        each one plays when the pointer rests on it.
    */
    /** The menu item that turns looping on and off, ticked while it's on. */
    static juce::PopupMenu::Item createLoopItem (bool isLooping);

    static juce::PopupMenu createGrooveMenu (const std::vector<roughtrack::Groove>&,
                                             const juce::String& style,
                                             bool fills,
                                             double beatsPerMinute,
                                             const std::shared_ptr<GroovePreview>& preview = {});

    TimelineView (roughtrack::AudioEngine&, int laneHeight);
    ~TimelineView() override;

    std::function<void (int track, const roughtrack::ClipList&, const juce::String& editName)> onEdit;
    std::function<void (int track, const roughtrack::DrumPartList&, const juce::String& editName)> onDrumEdit;
    std::function<void (juce::int64 position)> onSeek;

    /** A loop range marked or adjusted in the ruler, in samples, when the drag ends. */
    std::function<void (juce::Range<juce::int64>)> onCycleEdit;

    /** Clear Loop, from right-clicking the ruler: takes the loop off the timeline. */
    std::function<void()> onCycleClear;
    std::function<void (const juce::String&)> onMessage;
    std::function<void (float pixels)> onScrollLanes;

    void setSessionDirectory (const juce::File&);

    /** Call whenever clips change outside this view. */
    void refresh();
    void updatePlayhead();

    /** Scrolls the lanes to stay level with the track strips beside them. */
    void setVerticalOffset (int pixels);
    bool deleteSelectedClip();
    void quantizeClip (int track, int clip, int gridTicks);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    /** Which part of a clip a drag holds. */
    enum class Zone
    {
        body,
        start,
        end,
        fadeIn,
        fadeOut
    };

    /** Where a clip drag lands: the clips with one moved or trimmed by a distance, the
        dragged edge snapped to the grid. A looping clip's end turns into the repeats it
        was playing. A fade handle sets that fade, off the grid. unitsPerBeat is in the
        clips' own units, samples or ticks.
    */
    static roughtrack::ClipList dragClip (const roughtrack::ClipList&,
                                          int index,
                                          Zone,
                                          juce::int64 distance,
                                          roughtrack::SnapGrid,
                                          double unitsPerBeat,
                                          juce::int64 loopEnd);

    /** What moves, trims and splits land on. */
    void setSnap (roughtrack::SnapGrid);
    roughtrack::SnapGrid getSnap() const noexcept { return snap; }

private:

    struct Hit
    {
        int track = -1;
        int clip = -1;
        Zone zone = Zone::body;

        bool isClip() const noexcept { return clip >= 0; }
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    double sampleRate() const;
    float sampleToX (juce::int64 sample) const;
    juce::int64 xToSample (float x) const;
    juce::int64 pixelsToSamples (float pixels) const;
    int trackAt (int y) const;
    int lanesHeight() const;
    juce::Rectangle<int> laneBounds (int track) const;
    juce::Rectangle<float> clipBounds (int track, const roughtrack::Clip&) const;

    /** Where the clip or part stops, counting its loop, in the track's units, for what's on screen. */
    juce::int64 loopEndOf (int track, int index) const;

    /** All of a clip or part, from its start to the end of its loop. */
    juce::Rectangle<float> extentBounds (int track, int index) const;
    const roughtrack::ClipList& clipsToShow (int track) const;
    Hit hitTestClips (juce::Point<float>) const;
    Hit hitTestParts (int track, juce::Point<float>) const;
    static Zone findZone (juce::Rectangle<float> bounds, juce::Point<float>);

    /** The handles on an audio clip's top edge that set its fades, at the inner end of each fade. */
    juce::Rectangle<float> fadeHandleBounds (int track, const roughtrack::Clip&, Zone) const;
    void paintFades (juce::Graphics&, int track, const roughtrack::Clip&, bool fadesOut, bool showHandles);

    bool isDrums (int track) const noexcept;
    bool isMidi (int track) const noexcept;
    double samplesPerTick() const;

    /** Clips on a MIDI track are positioned in ticks. */
    juce::int64 toClipUnits (int track, juce::int64 sample) const;
    juce::int64 toSamples (int track, juce::int64 clipPosition) const;
    double samplesPerBar() const;

    /** How long a beat is in the track's own units: samples, or ticks on a MIDI track. */
    double unitsPerBeat (int track) const;
    float pixelsPerBar() const;
    float barToX (int bar) const;
    int xToBar (float x) const;
    juce::Rectangle<float> partBounds (int track, const roughtrack::DrumPart&) const;
    const roughtrack::DrumPartList& partsToShow (int track) const;

    juce::AudioThumbnail& thumbnailFor (const juce::File&);
    void paintRuler (juce::Graphics&);
    void paintCycle (juce::Graphics&);

    /** What a press on the bar numbers does: marks a new range, or takes hold of the range. */
    enum class CycleDrag
    {
        none,
        pending,
        create,
        start,
        end,
        move
    };

    CycleDrag cycleZoneAt (juce::Point<float>) const;
    juce::Range<juce::int64> cycleToShow() const;
    juce::int64 snapSample (juce::int64 sample, const juce::ModifierKeys&) const;
    juce::int64 shortestCycle (const juce::ModifierKeys&) const;
    void dragCycle (const juce::MouseEvent&);
    void paintClip (juce::Graphics&, int track, const roughtrack::Clip&, juce::int64 loopEnd, bool selected, bool hovered);
    void paintClipBody (juce::Graphics&, int track, const roughtrack::Clip&, bool isRepeat);
    void paintNotes (juce::Graphics&, const roughtrack::Clip&, juce::Rectangle<float> bounds, float alpha = 1.0f);
    void paintPart (juce::Graphics&, int track, const roughtrack::DrumPart&, int loopEnd, bool selected);
    void paintBarLines (juce::Graphics&, juce::Rectangle<int> lane);
    void paintHits (juce::Graphics&, const roughtrack::DrumPart&, const roughtrack::Groove&, juce::Rectangle<float> bounds);
    void showMenu (const Hit&, juce::Point<float>);
    void showDrumMenu (const Hit&, juce::Point<float>);
    juce::Array<juce::File> findTakes (int track) const;
    void addTake (int track, const juce::File&);
    void updateScrollBar();
    void setZoom (double newPixelsPerSecond, float aroundX);
    bool canEdit();

    roughtrack::AudioEngine& engine;
    std::shared_ptr<GroovePreview> groovePreview = std::make_shared<GroovePreview> (engine);
    const int laneHeight;

    juce::File sessionDirectory;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache { 64 };
    std::map<juce::File, std::unique_ptr<juce::AudioThumbnail>> thumbnails;

    juce::ScrollBar scrollBar { false };
    juce::TextButton zoomInButton { "+" }, zoomOutButton { "-" };

    double pixelsPerSecond = 40.0;
    double viewStartSeconds = 0.0;
    float playheadX = -1.0f;
    int verticalOffset = 0;

    Hit selected;
    Hit hovered;
    Hit dragging;
    juce::int64 dragLoopEnd = 0;
    roughtrack::SnapGrid snap = roughtrack::SnapGrid::bar;

    /** Where the dragged edge will land, in samples, or -1 while nothing's being dragged. */
    juce::int64 landingSample = -1;
    roughtrack::ClipList dragOriginal, dragPreview;
    roughtrack::DrumPartList partsOriginal, partsPreview;
    bool isDragging = false;

    CycleDrag cycleDrag = CycleDrag::none;
    juce::Range<juce::int64> cycleOriginal, cyclePreview;
    juce::int64 cycleAnchor = 0;
};
