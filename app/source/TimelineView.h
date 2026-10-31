#pragma once

#include "rifftrack/AudioEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

/** Shows each track's clips as waveforms, lined up with its strip, and edits them. */
class TimelineView final : public juce::Component,
                           private juce::ChangeListener,
                           private juce::ScrollBar::Listener
{
public:
    static constexpr int rulerHeight = 24;
    static constexpr int scrollBarHeight = 14;

    TimelineView (rifftrack::AudioEngine&, int laneHeight);
    ~TimelineView() override;

    std::function<void (int track, const rifftrack::ClipList&, const juce::String& editName)> onEdit;
    std::function<void (juce::int64 position)> onSeek;
    std::function<void (const juce::String&)> onMessage;

    void setSessionDirectory (const juce::File&);

    /** Call whenever clips change outside this view. */
    void refresh();
    void updatePlayhead();
    bool deleteSelectedClip();

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
    int laneAt (int y) const;
    juce::Rectangle<int> laneBounds (int track) const;
    juce::Rectangle<float> clipBounds (int track, const rifftrack::Clip&) const;
    const rifftrack::ClipList& clipsToShow (int track) const;
    Hit hitTestClips (juce::Point<float>) const;

    juce::AudioThumbnail& thumbnailFor (const juce::File&);
    void paintRuler (juce::Graphics&);
    void paintClip (juce::Graphics&, int track, const rifftrack::Clip&, bool selected);
    void showMenu (const Hit&, juce::Point<float>);
    juce::Array<juce::File> findTakes (int track) const;
    void addTake (int track, const juce::File&);
    void updateScrollBar();
    void setZoom (double newPixelsPerSecond, float aroundX);
    bool canEdit();

    rifftrack::AudioEngine& engine;
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

    Hit selected;
    Hit dragging;
    rifftrack::ClipList dragOriginal, dragPreview;
    bool isDragging = false;
};
