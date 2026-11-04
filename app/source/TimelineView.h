#pragma once

#include "roughtrack/AudioEngine.h"

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

    /** One style's grooves or fills, with those suited to the tempo first. */
    static juce::PopupMenu createGrooveMenu (const std::vector<roughtrack::Groove>&, const juce::String& style, bool fills, double beatsPerMinute);

    TimelineView (roughtrack::AudioEngine&, int laneHeight);
    ~TimelineView() override;

    std::function<void (int track, const roughtrack::ClipList&, const juce::String& editName)> onEdit;
    std::function<void (int track, const roughtrack::DrumPartList&, const juce::String& editName)> onDrumEdit;
    std::function<void (juce::int64 position)> onSeek;
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
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    enum class Zone
    {
        body,
        start,
        end
    };

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
    const roughtrack::ClipList& clipsToShow (int track) const;
    Hit hitTestClips (juce::Point<float>) const;
    Hit hitTestParts (int track, juce::Point<float>) const;
    static Zone findZone (juce::Rectangle<float> bounds, juce::Point<float>);

    bool isDrums (int track) const noexcept;
    bool isMidi (int track) const noexcept;
    double samplesPerTick() const;

    /** Clips on a MIDI track are positioned in ticks. */
    juce::int64 toClipUnits (int track, juce::int64 sample) const;
    juce::int64 toSamples (int track, juce::int64 clipPosition) const;
    double samplesPerBar() const;
    float pixelsPerBar() const;
    float barToX (int bar) const;
    int xToBar (float x) const;
    juce::Rectangle<float> partBounds (int track, const roughtrack::DrumPart&) const;
    const roughtrack::DrumPartList& partsToShow (int track) const;

    juce::AudioThumbnail& thumbnailFor (const juce::File&);
    void paintRuler (juce::Graphics&);
    void paintClip (juce::Graphics&, int track, const roughtrack::Clip&, bool selected);
    void paintNotes (juce::Graphics&, const roughtrack::Clip&, juce::Rectangle<float> bounds);
    void paintPart (juce::Graphics&, int track, const roughtrack::DrumPart&, bool selected);
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
    Hit dragging;
    roughtrack::ClipList dragOriginal, dragPreview;
    roughtrack::DrumPartList partsOriginal, partsPreview;
    bool isDragging = false;
};
