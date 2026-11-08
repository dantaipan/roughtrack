#include "TimelineView.h"

#include "roughtrack/Loops.h"

#include <algorithm>
#include <cmath>

namespace
{

constexpr double fallbackSampleRate = 48000.0;
constexpr double minimumPixelsPerSecond = 5.0;
constexpr double maximumPixelsPerSecond = 2000.0;
constexpr double zoomStep = 1.5;
constexpr double minimumTimelineSeconds = 120.0;
constexpr double spareSeconds = 30.0;
constexpr float edgeGrabPixels = 6.0f;
constexpr float fadeHandleSize = 7.0f;
constexpr int thumbnailResolution = 512;
constexpr int rulerLabelSpacing = 70;

constexpr int splitMenuId = 1;
constexpr int deleteMenuId = 2;
constexpr int loopMenuId = 3;
constexpr float repeatAlpha = 0.45f;
constexpr float loopIconSize = 9.0f;
constexpr int firstQuantizeMenuId = 10;
constexpr int firstTakeMenuId = 100;
constexpr int firstGrooveMenuId = 100000;

struct QuantizeGrid
{
    const char* name;
    int ticks;
};

constexpr QuantizeGrid quantizeGrids[] { { "Quarter Notes", roughtrack::ticksPerBeat },
                                         { "Eighth Notes", roughtrack::ticksPerBeat / 2 },
                                         { "Sixteenth Notes", roughtrack::ticksPerBeat / 4 },
                                         { "Eighth Triplets (Shuffle)", roughtrack::ticksPerBeat / 3 },
                                         { "Sixteenth Triplets", roughtrack::ticksPerBeat / 6 } };


constexpr float noteHeight = 2.0f;
constexpr float minimumPixelsPerBarLine = 8.0f;
constexpr float minimumPixelsPerBeatLine = 12.0f;
constexpr float minimumPixelsPerBarNumber = 24.0f;
constexpr float minimumPixelsPerBarOfHits = 40.0f;

const auto clipColour = juce::Colour (0xff2f4f6f);
const auto waveformColour = juce::Colour (0xffa8c8e8);
const auto beatColour = juce::Colour (0xff3f5f3f);
const auto fillColour = juce::Colour (0xff6f5a2f);
const auto hitColour = juce::Colour (0xffc8e8c0);
const auto midiColour = juce::Colour (0xff4f3f6f);
const auto noteColour = juce::Colour (0xffd8c8f0);
const auto laneColour = juce::Colour (0xff202225);
const auto gridColour = juce::Colour (0xff34373c);

} // namespace

TimelineView::TimelineView (roughtrack::AudioEngine& engineToShow, int laneHeightToUse)
    : engine (engineToShow), laneHeight (laneHeightToUse)
{
    formats.registerBasicFormats();

    scrollBar.addListener (this);
    scrollBar.setAutoHide (false);
    addAndMakeVisible (scrollBar);

    zoomInButton.onClick = [this] { setZoom (pixelsPerSecond * zoomStep, (float) getWidth() / 2.0f); };
    zoomOutButton.onClick = [this] { setZoom (pixelsPerSecond / zoomStep, (float) getWidth() / 2.0f); };
    addAndMakeVisible (zoomInButton);
    addAndMakeVisible (zoomOutButton);

    updateScrollBar();
}

TimelineView::~TimelineView()
{
    scrollBar.removeListener (this);

    for (auto& [file, thumbnail] : thumbnails)
        thumbnail->removeChangeListener (this);
}

void TimelineView::setSessionDirectory (const juce::File& directory)
{
    sessionDirectory = directory;
    selected = {};
    viewStartSeconds = 0.0;
    refresh();
}

void TimelineView::refresh()
{
    if (selected.isClip())
    {
        const auto count = isDrums (selected.track) ? engine.getDrumParts (selected.track).size() : engine.getClips (selected.track).size();

        if (! engine.isTrackActive (selected.track) || selected.clip >= (int) count)
            selected = {};
    }

    updateScrollBar();
    repaint();
}

double TimelineView::sampleRate() const
{
    return engine.getSampleRate() > 0.0 ? engine.getSampleRate() : fallbackSampleRate;
}

float TimelineView::sampleToX (juce::int64 sample) const
{
    return (float) (((double) sample / sampleRate() - viewStartSeconds) * pixelsPerSecond);
}

juce::int64 TimelineView::xToSample (float x) const
{
    return (juce::int64) std::llround (((double) x / pixelsPerSecond + viewStartSeconds) * sampleRate());
}

juce::int64 TimelineView::pixelsToSamples (float pixels) const
{
    return (juce::int64) std::llround ((double) pixels / pixelsPerSecond * sampleRate());
}

bool TimelineView::isDrums (int track) const noexcept
{
    return engine.getTrackType (track) == roughtrack::TrackType::drums;
}

bool TimelineView::isMidi (int track) const noexcept
{
    return engine.getTrackType (track) == roughtrack::TrackType::midi;
}

double TimelineView::samplesPerTick() const
{
    return 60.0 * sampleRate() / engine.getTempo() / roughtrack::ticksPerBeat;
}

juce::int64 TimelineView::toClipUnits (int track, juce::int64 sample) const
{
    return isMidi (track) ? (juce::int64) std::llround ((double) sample / samplesPerTick()) : sample;
}

juce::int64 TimelineView::toSamples (int track, juce::int64 clipPosition) const
{
    return isMidi (track) ? (juce::int64) std::llround ((double) clipPosition * samplesPerTick()) : clipPosition;
}

double TimelineView::unitsPerBeat (int track) const
{
    return isMidi (track) ? (double) roughtrack::ticksPerBeat : 60.0 * sampleRate() / engine.getTempo();
}

void TimelineView::setSnap (roughtrack::SnapGrid newSnap)
{
    snap = newSnap;
}

double TimelineView::samplesPerBar() const
{
    return roughtrack::beatsPerBar * 60.0 * sampleRate() / engine.getTempo();
}

float TimelineView::pixelsPerBar() const
{
    return (float) (samplesPerBar() / sampleRate() * pixelsPerSecond);
}

float TimelineView::barToX (int bar) const
{
    return sampleToX ((juce::int64) std::llround (bar * samplesPerBar()));
}

int TimelineView::xToBar (float x) const
{
    return (int) std::floor ((double) xToSample (x) / samplesPerBar());
}

int TimelineView::trackAt (int y) const
{
    const auto& order = engine.getTrackOrder();
    const auto lane = (y - rulerHeight + verticalOffset) / laneHeight;

    return y >= rulerHeight && juce::isPositiveAndBelow (lane, (int) order.size()) ? order[(size_t) lane] : -1;
}

int TimelineView::lanesHeight() const
{
    return (int) engine.getTrackOrder().size() * laneHeight;
}

void TimelineView::setVerticalOffset (int pixels)
{
    verticalOffset = pixels;
    repaint();
}

juce::Rectangle<int> TimelineView::laneBounds (int track) const
{
    const auto& order = engine.getTrackOrder();
    const auto lane = (int) std::distance (order.begin(), std::find (order.begin(), order.end(), track));

    return { 0, rulerHeight + lane * laneHeight - verticalOffset, getWidth(), laneHeight };
}

juce::Rectangle<float> TimelineView::clipBounds (int track, const roughtrack::Clip& clip) const
{
    const auto lane = laneBounds (track).reduced (0, 3).toFloat();
    const auto left = sampleToX (toSamples (track, clip.timelineStart));

    return { left, lane.getY(), juce::jmax (2.0f, sampleToX (toSamples (track, clip.timelineEnd())) - left), lane.getHeight() };
}

juce::Rectangle<float> TimelineView::partBounds (int track, const roughtrack::DrumPart& part) const
{
    const auto lane = laneBounds (track).reduced (0, 3).toFloat();
    const auto left = barToX (part.startBar);

    return { left, lane.getY(), juce::jmax (2.0f, barToX (part.endBar()) - left), lane.getHeight() };
}

juce::int64 TimelineView::loopEndOf (int track, int index) const
{
    if (isDrums (track))
    {
        const auto& parts = partsToShow (track);
        const auto& part = parts[(size_t) index];
        return part.loopsToNext ? roughtrack::findLoopEnd (parts, index, (int) engine.getSongEnd (track)) : part.endBar();
    }

    const auto& clips = clipsToShow (track);
    const auto& clip = clips[(size_t) index];
    return clip.loopsToNext ? roughtrack::findLoopEnd (clips, index, engine.getSongEnd (track)) : clip.timelineEnd();
}

juce::Rectangle<float> TimelineView::extentBounds (int track, int index) const
{
    const auto loopEnd = loopEndOf (track, index);

    if (isDrums (track))
    {
        const auto bounds = partBounds (track, partsToShow (track)[(size_t) index]);
        return bounds.withRight (juce::jmax (bounds.getRight(), barToX ((int) loopEnd)));
    }

    const auto bounds = clipBounds (track, clipsToShow (track)[(size_t) index]);
    return bounds.withRight (juce::jmax (bounds.getRight(), sampleToX (toSamples (track, loopEnd))));
}

namespace
{

/** A circular arrow, shown on anything that loops. */
void drawLoopIcon (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto radius = area.getWidth() / 2.0f - 1.0f;
    juce::Path arc;
    arc.addCentredArc (area.getCentreX(), area.getCentreY(), radius, radius, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.2f, true);
    g.strokePath (arc, juce::PathStrokeType (1.3f));

    // The arrowhead, at the end of the arc, pointing on round.
    const auto tip = arc.getCurrentPosition();
    juce::Path head;
    head.addTriangle (tip.x - 2.5f, tip.y - 1.0f, tip.x + 1.5f, tip.y - 2.5f, tip.x + 0.5f, tip.y + 1.8f);
    g.fillPath (head);
}

} // namespace

const roughtrack::DrumPartList& TimelineView::partsToShow (int track) const
{
    return isDragging && dragging.track == track ? partsPreview : engine.getDrumParts (track);
}

const roughtrack::ClipList& TimelineView::clipsToShow (int track) const
{
    return isDragging && dragging.track == track ? dragPreview : engine.getClips (track);
}

TimelineView::Hit TimelineView::hitTestClips (juce::Point<float> point) const
{
    const auto track = trackAt ((int) point.y);

    if (track < 0)
        return {};

    if (isDrums (track))
        return hitTestParts (track, point);

    const auto& clips = engine.getClips (track);

    // A loop's repeats are part of its clip, so they select it and drag it too.
    for (auto i = (int) clips.size() - 1; i >= 0; --i)
    {
        const auto bounds = extentBounds (track, i);

        if (! bounds.contains (point))
            continue;

        // The fade handles sit on the trim edges' corners, so they're found first.
        if (! isMidi (track))
            for (const auto zone : { Zone::fadeIn, Zone::fadeOut })
                if (fadeHandleBounds (track, clips[(size_t) i], zone).expanded (2.0f).contains (point))
                    return { track, i, zone };

        return { track, i, findZone (bounds, point) };
    }

    return { track, -1, Zone::body };
}

TimelineView::Hit TimelineView::hitTestParts (int track, juce::Point<float> point) const
{
    const auto& parts = engine.getDrumParts (track);

    for (auto i = (int) parts.size() - 1; i >= 0; --i)
    {
        const auto bounds = extentBounds (track, i);

        if (bounds.contains (point))
            return { track, i, findZone (bounds, point) };
    }

    return { track, -1, Zone::body };
}

TimelineView::Zone TimelineView::findZone (juce::Rectangle<float> bounds, juce::Point<float> point)
{
    if (bounds.getWidth() > edgeGrabPixels * 3.0f)
    {
        if (point.x < bounds.getX() + edgeGrabPixels)
            return Zone::start;

        if (point.x > bounds.getRight() - edgeGrabPixels)
            return Zone::end;
    }

    return Zone::body;
}

juce::Rectangle<float> TimelineView::fadeHandleBounds (int track, const roughtrack::Clip& clip, Zone zone) const
{
    const auto bounds = clipBounds (track, clip);
    const auto x = zone == Zone::fadeIn ? sampleToX (clip.timelineStart + clip.fadeIn)
                                        : sampleToX (clip.timelineEnd() - clip.fadeOut);

    // Kept inside the clip, so a handle with no fade sits in its corner.
    const auto left = juce::jlimit (bounds.getX(), juce::jmax (bounds.getX(), bounds.getRight() - fadeHandleSize), x - fadeHandleSize / 2.0f);
    return { left, bounds.getY(), fadeHandleSize, fadeHandleSize };
}

void TimelineView::paintFades (juce::Graphics& g, int track, const roughtrack::Clip& clip, bool fadesOut, bool showHandles)
{
    const auto bounds = clipBounds (track, clip);
    const auto fadeOut = fadesOut ? clip.fadeOut : (juce::int64) 0;

    // The fade's curve, with what it takes away shaded above it.
    const auto drawFade = [&] (juce::int64 from, juce::int64 length, bool rising)
    {
        const auto x1 = sampleToX (from);
        const auto x2 = sampleToX (from + length);

        if (x2 - x1 < 1.0f || x2 < 0.0f || x1 > (float) getWidth())
            return;

        juce::Path curve;
        const auto steps = juce::jlimit (2, 64, (int) (x2 - x1) / 3);

        for (int i = 0; i <= steps; ++i)
        {
            const auto t = (float) i / (float) steps;
            const auto gain = std::sin (juce::MathConstants<float>::halfPi * (rising ? t : 1.0f - t));
            const auto point = juce::Point<float> (x1 + t * (x2 - x1), bounds.getBottom() - gain * bounds.getHeight());

            if (i == 0)
                curve.startNewSubPath (point);
            else
                curve.lineTo (point);
        }

        auto shade = curve;
        shade.lineTo (x2, bounds.getY());
        shade.lineTo (x1, bounds.getY());
        shade.closeSubPath();

        g.saveState();
        g.reduceClipRegion (bounds.toNearestInt());
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (shade);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.strokePath (curve, juce::PathStrokeType (1.0f));
        g.restoreState();
    };

    if (clip.fadeIn > 0)
        drawFade (clip.timelineStart, clip.fadeIn, true);

    if (fadeOut > 0)
        drawFade (clip.timelineEnd() - fadeOut, fadeOut, false);

    if (! showHandles)
        return;

    for (const auto zone : { Zone::fadeIn, Zone::fadeOut })
    {
        const auto handle = fadeHandleBounds (track, clip, zone).reduced (0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRect (handle);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRect (handle, 1.0f);
    }
}

juce::AudioThumbnail& TimelineView::thumbnailFor (const juce::File& file)
{
    auto& thumbnail = thumbnails[file];

    if (thumbnail == nullptr)
    {
        thumbnail = std::make_unique<juce::AudioThumbnail> (thumbnailResolution, formats, thumbnailCache);
        thumbnail->addChangeListener (this);
        thumbnail->setSource (new juce::FileInputSource (file));
    }

    return *thumbnail;
}

void TimelineView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}

void TimelineView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    viewStartSeconds = newRangeStart;
    repaint();
}

void TimelineView::updateScrollBar()
{
    auto lastSample = (juce::int64) 0;

    for (const auto track : engine.getTrackOrder())
    {
        for (const auto& clip : engine.getClips (track))
            lastSample = juce::jmax (lastSample, toSamples (track, clip.timelineEnd()));

        for (const auto& part : engine.getDrumParts (track))
            lastSample = juce::jmax (lastSample, (juce::int64) std::llround (part.endBar() * samplesPerBar()));
    }

    const auto total = juce::jmax (minimumTimelineSeconds, (double) lastSample / sampleRate() + spareSeconds);

    scrollBar.setRangeLimits (0.0, total, juce::dontSendNotification);
    scrollBar.setCurrentRange (viewStartSeconds, (double) juce::jmax (1, getWidth()) / pixelsPerSecond, juce::dontSendNotification);
    viewStartSeconds = scrollBar.getCurrentRangeStart();
}

void TimelineView::setZoom (double newPixelsPerSecond, float aroundX)
{
    const auto anchorSeconds = viewStartSeconds + (double) aroundX / pixelsPerSecond;

    pixelsPerSecond = juce::jlimit (minimumPixelsPerSecond, maximumPixelsPerSecond, newPixelsPerSecond);
    viewStartSeconds = juce::jmax (0.0, anchorSeconds - (double) aroundX / pixelsPerSecond);

    updateScrollBar();
    repaint();
}

void TimelineView::updatePlayhead()
{
    const auto newX = sampleToX (engine.getPosition());

    if (engine.getState() != roughtrack::TransportState::stopped && newX > (float) getWidth())
    {
        viewStartSeconds = (double) engine.getPosition() / sampleRate();
        updateScrollBar();
        repaint();
        return;
    }

    if (! juce::approximatelyEqual (newX, playheadX))
    {
        const auto left = juce::jmin (newX, playheadX);
        const auto right = juce::jmax (newX, playheadX);

        playheadX = newX;
        repaint ((int) left - 2, 0, (int) (right - left) + 5, getHeight());
    }
}

void TimelineView::paint (juce::Graphics& g)
{
    g.fillAll (laneColour);

    for (const auto track : engine.getTrackOrder())
    {
        const auto lane = laneBounds (track);

        g.setColour (gridColour);
        g.drawHorizontalLine (lane.getBottom() - 1, 0.0f, (float) getWidth());

        // Every lane shows the bars, so audio and MIDI can be lined up by eye as well as by snapping.
        paintBarLines (g, lane);

        if (isDrums (track))
        {
            const auto& parts = partsToShow (track);

            for (size_t i = 0; i < parts.size(); ++i)
                paintPart (g, track, parts[i], (int) loopEndOf (track, (int) i), selected.track == track && selected.clip == (int) i);

            if (parts.empty())
            {
                g.setColour (juce::Colours::grey);
                g.setFont (12.0f);
                g.drawText ("Right-click to add a groove", lane.reduced (10, 0), juce::Justification::centredLeft, true);
            }

            continue;
        }

        const auto& clips = clipsToShow (track);

        for (size_t i = 0; i < clips.size(); ++i)
            paintClip (g, track, clips[i], loopEndOf (track, (int) i),
                       selected.track == track && selected.clip == (int) i,
                       hovered.track == track && hovered.clip == (int) i);
    }

    // Where the edge being dragged will land.
    if (landingSample >= 0)
    {
        g.setColour (juce::Colours::orange.withAlpha (0.7f));
        g.fillRect (sampleToX (landingSample) - 0.5f, (float) rulerHeight, 1.0f, (float) (getHeight() - rulerHeight - scrollBarHeight));
    }

    paintRuler (g);
    paintCycle (g);

    const auto x = sampleToX (engine.getPosition());
    g.setColour (juce::Colours::white);
    g.fillRect (x - 0.5f, 0.0f, 1.5f, (float) (getHeight() - scrollBarHeight));
}

void TimelineView::paintRuler (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff17181a));
    g.fillRect (0, 0, getWidth(), rulerHeight);

    auto step = 1.0;

    for (auto candidate : { 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0 })
    {
        step = candidate;

        if (candidate * pixelsPerSecond >= rulerLabelSpacing)
            break;
    }

    const auto half = rulerHeight / 2;
    g.setFont (11.0f);

    // Minutes and seconds along the top half.
    for (auto seconds = std::floor (viewStartSeconds / step) * step; ; seconds += step)
    {
        const auto x = (float) ((seconds - viewStartSeconds) * pixelsPerSecond);

        if (x > (float) getWidth())
            break;

        g.setColour (gridColour);
        g.drawVerticalLine ((int) x, (float) half / 2.0f, (float) half);

        const auto minutes = (int) seconds / 60;
        g.setColour (juce::Colours::lightgrey);
        g.drawText (juce::String (minutes) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2),
                    (int) x + 3, 0, rulerLabelSpacing, half, juce::Justification::centredLeft);
    }

    // Bar numbers along the bottom half, counting from 1 like the position display.
    auto barStep = 1;

    while (pixelsPerBar() * (float) barStep < minimumPixelsPerBarNumber && barStep < 1024)
        barStep *= 2;

    g.setColour (gridColour.brighter (0.4f));
    g.drawHorizontalLine (half, 0.0f, (float) getWidth());

    for (auto bar = juce::jmax (0, xToBar (0.0f) / barStep * barStep); barToX (bar) < (float) getWidth(); bar += barStep)
    {
        const auto x = barToX (bar);

        g.setColour (gridColour.brighter (0.4f));
        g.drawVerticalLine ((int) x, (float) half, (float) rulerHeight);

        g.setColour (juce::Colours::orange.withAlpha (0.85f));
        g.drawText (juce::String (bar + 1), (int) x + 3, half, 40, rulerHeight - half, juce::Justification::centredLeft);
    }
}

juce::Range<juce::int64> TimelineView::cycleToShow() const
{
    return cycleDrag != CycleDrag::none && cycleDrag != CycleDrag::pending ? cyclePreview : engine.getCycleRange();
}

void TimelineView::paintCycle (juce::Graphics& g)
{
    const auto range = cycleToShow();

    if (range.isEmpty())
        return;

    const auto half = rulerHeight / 2;
    const auto x1 = sampleToX (range.getStart());
    const auto x2 = sampleToX (range.getEnd());
    const auto on = engine.isCycleEnabled() || cycleDrag == CycleDrag::create;
    const auto colour = on ? juce::Colours::orange : juce::Colours::grey;

    // A band behind the bar numbers, solid while it's on and faint while it's off.
    const juce::Rectangle<float> band { x1, (float) half + 1.0f, x2 - x1, (float) (rulerHeight - half) - 2.0f };
    g.setColour (colour.withAlpha (on ? 0.35f : 0.18f));
    g.fillRoundedRectangle (band, 3.0f);
    g.setColour (colour.withAlpha (on ? 0.9f : 0.5f));
    g.drawRoundedRectangle (band, 3.0f, 1.0f);

    // While it's on, its edges run down through the lanes.
    if (on)
    {
        const auto lanesBottom = (float) (getHeight() - scrollBarHeight);
        g.setColour (juce::Colours::orange.withAlpha (0.35f));

        for (const auto x : { x1, x2 })
            g.fillRect (x - 0.5f, (float) rulerHeight, 1.0f, lanesBottom - (float) rulerHeight);
    }
}

TimelineView::CycleDrag TimelineView::cycleZoneAt (juce::Point<float> point) const
{
    constexpr float edgeGrab = 5.0f;
    const auto range = engine.getCycleRange();

    if (point.y < (float) (rulerHeight / 2) || point.y >= (float) rulerHeight || range.isEmpty())
        return CycleDrag::pending;

    const auto x1 = sampleToX (range.getStart());
    const auto x2 = sampleToX (range.getEnd());

    if (std::abs (point.x - x1) <= edgeGrab)
        return CycleDrag::start;

    if (std::abs (point.x - x2) <= edgeGrab)
        return CycleDrag::end;

    return point.x > x1 && point.x < x2 ? CycleDrag::move : CycleDrag::pending;
}

juce::int64 TimelineView::snapSample (juce::int64 sample, const juce::ModifierKeys& mods) const
{
    // Command places freely, as it does for clips.
    const auto grid = mods.isCommandDown() ? roughtrack::SnapGrid::off : snap;
    return juce::jmax<juce::int64> (0, roughtrack::snapToGrid (sample, grid, samplesPerBar() / roughtrack::beatsPerBar));
}

juce::int64 TimelineView::shortestCycle (const juce::ModifierKeys& mods) const
{
    const auto grid = mods.isCommandDown() ? roughtrack::SnapGrid::off : snap;
    const auto step = (juce::int64) std::llround (roughtrack::gridLength (grid, samplesPerBar() / roughtrack::beatsPerBar));
    return juce::jmax (step, roughtrack::AudioEngine::minimumCycleSamples);
}

void TimelineView::dragCycle (const juce::MouseEvent& event)
{
    constexpr int dragThreshold = 3;

    if (cycleDrag == CycleDrag::pending)
    {
        if (std::abs (event.getDistanceFromDragStartX()) < dragThreshold)
            return;

        cycleDrag = CycleDrag::create;
    }

    const auto shortest = shortestCycle (event.mods);
    const auto distance = pixelsToSamples ((float) event.getDistanceFromDragStartX());
    const auto& original = cycleOriginal;

    switch (cycleDrag)
    {
        case CycleDrag::create:
        {
            const auto from = snapSample (cycleAnchor, event.mods);
            const auto to = snapSample (xToSample (event.position.x), event.mods);
            const auto start = juce::jmin (from, to);
            cyclePreview = { start, juce::jmax (juce::jmax (from, to), start + shortest) };
            break;
        }

        case CycleDrag::start:
            cyclePreview = original.withStart (juce::jmin (snapSample (original.getStart() + distance, event.mods), original.getEnd() - shortest));
            break;

        case CycleDrag::end:
            cyclePreview = original.withEnd (juce::jmax (snapSample (original.getEnd() + distance, event.mods), original.getStart() + shortest));
            break;

        case CycleDrag::move:
            cyclePreview = original.movedToStartAt (snapSample (original.getStart() + distance, event.mods));
            break;

        case CycleDrag::none:
        case CycleDrag::pending:
            break;
    }

    repaint();
}

void TimelineView::paintClip (juce::Graphics& g, int track, const roughtrack::Clip& clip, juce::int64 loopEnd, bool isSelected, bool isHovered)
{
    const auto bounds = clipBounds (track, clip);
    const auto extent = bounds.withRight (juce::jmax (bounds.getRight(), sampleToX (toSamples (track, loopEnd))));

    if (extent.getRight() < 0.0f || extent.getX() > (float) getWidth())
        return;

    const auto repeats = clip.loopsToNext && clip.length > 0 && loopEnd > clip.timelineEnd();

    // The repeats look like the clip, dimmed, each marked where it starts over.
    if (repeats)
    {
        for (auto start = clip.timelineEnd(); start < loopEnd; start += clip.length)
        {
            auto repeat = clip;
            repeat.timelineStart = start;
            repeat.length = juce::jmin (clip.length, loopEnd - start);

            const auto repeatBounds = clipBounds (track, repeat);

            if (repeatBounds.getX() > (float) getWidth())
                break;

            paintClipBody (g, track, repeat, true);

            // A loop fades out once, at the end of its last repeat.
            if (! isMidi (track) && repeat.timelineEnd() >= loopEnd)
            {
                repeat.fadeIn = 0;
                paintFades (g, track, roughtrack::setClipFades ({ repeat }, 0, 0, clip.fadeOut)[0], true, false);
            }

            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRect (repeatBounds.getX(), repeatBounds.getY() + 3.0f, 1.0f, repeatBounds.getHeight() - 6.0f);
        }
    }

    paintClipBody (g, track, clip, false);

    if (! isMidi (track))
        paintFades (g, track, clip, ! repeats, (isSelected || isHovered) && clip.file.existsAsFile());

    g.setColour (isSelected ? juce::Colours::orange : juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (extent, 3.0f, isSelected ? 2.0f : 1.0f);
}

void TimelineView::paintClipBody (juce::Graphics& g, int track, const roughtrack::Clip& clip, bool isRepeat)
{
    const auto bounds = clipBounds (track, clip);

    if (bounds.getRight() < 0.0f || bounds.getX() > (float) getWidth())
        return;

    const auto exists = clip.file.existsAsFile();
    const auto alpha = isRepeat ? repeatAlpha : 1.0f;

    g.setColour ((! exists ? juce::Colours::darkred : (isMidi (track) ? midiColour : clipColour)).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (bounds, 3.0f);

    if (exists && isMidi (track))
    {
        paintNotes (g, clip, bounds, alpha);
    }
    else if (exists)
    {
        auto& thumbnail = thumbnailFor (clip.file);

        // Only what's on screen is drawn, so a long take costs the same as a short one.
        const auto visible = bounds.getIntersection (getLocalBounds().toFloat());
        const auto startSeconds = (double) clip.fileOffset / sampleRate() + (double) (visible.getX() - bounds.getX()) / pixelsPerSecond;
        const auto endSeconds = startSeconds + (double) visible.getWidth() / pixelsPerSecond;

        g.setColour (waveformColour.withMultipliedAlpha (alpha));
        thumbnail.drawChannel (g, visible.toNearestInt(), startSeconds, endSeconds, 0, 1.0f);
    }

    if (isRepeat)
        return;

    auto label = bounds.reduced (5.0f, 2.0f);

    // The fade-in handle sits at the top left, so the name starts after it.
    if (! isMidi (track))
        label.setLeft (juce::jmax (label.getX(), fadeHandleBounds (track, clip, Zone::fadeIn).getRight() + 3.0f));

    g.setColour (juce::Colours::white.withAlpha (0.8f));

    if (clip.loopsToNext)
    {
        drawLoopIcon (g, label.removeFromLeft (loopIconSize).removeFromTop (loopIconSize).translated (0.0f, 1.0f));
        label.removeFromLeft (3.0f);
    }

    g.setFont (10.0f);
    g.drawText (exists ? clip.file.getFileNameWithoutExtension() : "Missing: " + clip.file.getFileName(), label, juce::Justification::topLeft, true);
}

void TimelineView::paintNotes (juce::Graphics& g, const roughtrack::Clip& clip, juce::Rectangle<float> bounds, float alpha)
{
    const auto* take = engine.findMidiTake (clip.file);

    if (take == nullptr || take->notes.empty())
        return;

    auto lowest = 127, highest = 0;

    for (const auto& note : take->notes)
    {
        lowest = juce::jmin (lowest, note.pitch);
        highest = juce::jmax (highest, note.pitch);
    }

    const auto area = bounds.reduced (0.0f, 4.0f).withTrimmedTop (10.0f);
    const auto pixelsPerTick = bounds.getWidth() / (float) clip.length;
    const auto offsetTicks = clip.fileOffset;
    const auto rowHeight = area.getHeight() / (float) juce::jmax (1, highest - lowest);

    g.setColour (noteColour.withMultipliedAlpha (alpha));

    for (const auto& note : take->notes)
    {
        const auto x = bounds.getX() + (float) (note.tick - offsetTicks) * pixelsPerTick;
        const auto width = juce::jmax (1.0f, (float) note.length * pixelsPerTick);

        if (x + width < bounds.getX() || x > bounds.getRight() || x > (float) getWidth() || x + width < 0.0f)
            continue;

        const auto left = juce::jmax (x, bounds.getX());
        const auto right = juce::jmin (x + width, bounds.getRight());

        g.fillRect (left, area.getBottom() - noteHeight - (float) (note.pitch - lowest) * rowHeight, right - left, noteHeight);
    }
}

void TimelineView::paintBarLines (juce::Graphics& g, juce::Rectangle<int> lane)
{
    if (pixelsPerBar() < minimumPixelsPerBarLine)
        return;

    const auto pixelsPerBeat = pixelsPerBar() / (float) roughtrack::beatsPerBar;
    const auto showBeats = pixelsPerBeat >= minimumPixelsPerBeatLine;

    for (auto bar = juce::jmax (0, xToBar (0.0f)); barToX (bar) < (float) getWidth(); ++bar)
    {
        g.setColour (gridColour);
        g.drawVerticalLine ((int) barToX (bar), (float) lane.getY(), (float) lane.getBottom());

        if (! showBeats)
            continue;

        g.setColour (gridColour.withAlpha (0.45f));

        for (int beat = 1; beat < roughtrack::beatsPerBar; ++beat)
            g.drawVerticalLine ((int) (barToX (bar) + (float) beat * pixelsPerBeat), (float) lane.getY(), (float) lane.getBottom());
    }
}

void TimelineView::paintPart (juce::Graphics& g, int track, const roughtrack::DrumPart& part, int loopEnd, bool isSelected)
{
    const auto bounds = partBounds (track, part);
    const auto extent = bounds.withRight (juce::jmax (bounds.getRight(), barToX (loopEnd)));

    if (extent.getRight() < 0.0f || extent.getX() > (float) getWidth())
        return;

    const auto* groove = engine.getGrooves().find (part.groove);
    const auto colour = groove == nullptr ? juce::Colours::darkred : (groove->isFill ? fillColour : beatColour);

    // The bars it loops on through, dimmed, marked each time the part starts over.
    if (part.loopsToNext && loopEnd > part.endBar())
    {
        const auto repeats = extent.withLeft (bounds.getRight());

        g.setColour (colour.withMultipliedAlpha (repeatAlpha));
        g.fillRoundedRectangle (repeats, 3.0f);

        if (groove != nullptr && pixelsPerBar() >= minimumPixelsPerBarOfHits)
        {
            auto looped = part;
            looped.bars = loopEnd - part.startBar;

            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (repeats.toNearestInt());
            g.setOpacity (repeatAlpha);
            paintHits (g, looped, *groove, extent);
        }

        g.setColour (juce::Colours::white.withAlpha (0.35f));

        for (auto bar = part.endBar(); bar < loopEnd; bar += juce::jmax (1, part.bars))
            g.fillRect (barToX (bar), repeats.getY() + 3.0f, 1.0f, repeats.getHeight() - 6.0f);
    }

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 3.0f);

    if (groove != nullptr && pixelsPerBar() >= minimumPixelsPerBarOfHits)
        paintHits (g, part, *groove, bounds);

    auto label = bounds.reduced (5.0f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.85f));

    if (part.loopsToNext)
    {
        drawLoopIcon (g, label.removeFromLeft (loopIconSize).removeFromTop (loopIconSize).translated (0.0f, 1.0f));
        label.removeFromLeft (3.0f);
    }

    g.setFont (10.0f);
    g.drawText (groove != nullptr ? groove->style + " " + (groove->isFill ? "fill " : "") + groove->name : "Missing groove",
                label, juce::Justification::topLeft, true);

    g.setColour (isSelected ? juce::Colours::orange : juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (extent, 3.0f, isSelected ? 2.0f : 1.0f);
}

// Kicks along the bottom and snares above them show the pattern at a glance.
void TimelineView::paintHits (juce::Graphics& g, const roughtrack::DrumPart& part, const roughtrack::Groove& groove, juce::Rectangle<float> bounds)
{
    const auto pixelsPerBeat = pixelsPerBar() / roughtrack::beatsPerBar;
    const auto firstBar = juce::jmax (part.startBar, xToBar (0.0f));
    const auto kickY = bounds.getBottom() - 8.0f;
    const auto snareY = bounds.getBottom() - 18.0f;

    g.setColour (hitColour.withAlpha (0.8f));

    for (auto bar = firstBar; bar < part.endBar() && barToX (bar) < (float) getWidth(); ++bar)
    {
        const auto barInGroove = juce::negativeAwareModulo (bar - part.startBar + part.offsetBars, groove.bars);

        for (auto i = groove.barStarts[(size_t) barInGroove]; i < groove.barStarts[(size_t) barInGroove + 1]; ++i)
        {
            const auto& hit = groove.hits[(size_t) i];

            if (hit.piece != roughtrack::DrumPiece::kick && hit.piece != roughtrack::DrumPiece::snare)
                continue;

            const auto x = barToX (bar) + (float) (hit.beat - barInGroove * roughtrack::beatsPerBar) * pixelsPerBeat;
            g.fillRect (x, hit.piece == roughtrack::DrumPiece::kick ? kickY : snareY, 1.5f, 6.0f);
        }
    }
}

void TimelineView::resized()
{
    auto area = getLocalBounds();
    scrollBar.setBounds (area.removeFromBottom (scrollBarHeight));

    auto ruler = area.removeFromTop (rulerHeight);
    zoomInButton.setBounds (ruler.removeFromRight (rulerHeight).reduced (2));
    zoomOutButton.setBounds (ruler.removeFromRight (rulerHeight).reduced (2));

    updateScrollBar();
}

bool TimelineView::canEdit()
{
    if (engine.getState() == roughtrack::TransportState::stopped)
        return true;

    if (onMessage != nullptr)
        onMessage ("Stop the transport to edit.");

    return false;
}

void TimelineView::mouseMove (const juce::MouseEvent& event)
{
    if (event.y < rulerHeight)
    {
        const auto zone = cycleZoneAt (event.position);
        setMouseCursor (zone == CycleDrag::start || zone == CycleDrag::end ? juce::MouseCursor::LeftRightResizeCursor
                                                                           : juce::MouseCursor::NormalCursor);
        return;
    }

    const auto hit = hitTestClips (event.position);

    setMouseCursor (hit.isClip() && hit.zone != Zone::body ? juce::MouseCursor::LeftRightResizeCursor
                                                           : juce::MouseCursor::NormalCursor);

    // The fade handles show on the clip under the mouse.
    const auto over = hit.isClip() ? Hit { hit.track, hit.clip, Zone::body } : Hit {};

    if (over.track != hovered.track || over.clip != hovered.clip)
    {
        hovered = over;
        repaint();
    }
}

void TimelineView::mouseExit (const juce::MouseEvent&)
{
    if (hovered.isClip())
    {
        hovered = {};
        repaint();
    }
}

void TimelineView::mouseDown (const juce::MouseEvent& event)
{
    if (event.y < rulerHeight)
    {
        // Right-clicking the ruler offers to take the loop away, so it isn't shown when unwanted.
        if (event.mods.isPopupMenu())
        {
            if (engine.getCycleRange().isEmpty())
                return;

            juce::PopupMenu menu;
            menu.addItem ("Clear Loop", [safeThis = juce::Component::SafePointer (this)]
            {
                if (safeThis != nullptr && safeThis->onCycleClear != nullptr)
                    safeThis->onCycleClear();
            });

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition() }));
            return;
        }

        // A press on the bar numbers might be the start of marking a loop, so it waits to
        // see. One on the time above moves the playhead at once.
        if (event.y >= rulerHeight / 2 && ! event.mods.isPopupMenu())
        {
            cycleDrag = cycleZoneAt (event.position);
            cycleOriginal = engine.getCycleRange();
            cyclePreview = cycleOriginal;
            cycleAnchor = juce::jmax<juce::int64> (0, xToSample (event.position.x));
            return;
        }

        if (onSeek != nullptr)
            onSeek (juce::jmax<juce::int64> (0, xToSample (event.position.x)));

        return;
    }

    const auto hit = hitTestClips (event.position);

    if (hit.track < 0)
        return;

    selected = hit.isClip() ? hit : Hit {};
    repaint();

    if (event.mods.isPopupMenu())
    {
        showMenu (hit, event.position);
        return;
    }

    if (hit.isClip() && canEdit())
    {
        dragging = hit;
        dragOriginal = engine.getClips (hit.track);
        dragPreview = dragOriginal;
        partsOriginal = engine.getDrumParts (hit.track);
        partsPreview = partsOriginal;
        dragLoopEnd = engine.getLoopEnd (hit.track, hit.clip);
        isDragging = true;
    }
}

void TimelineView::mouseDrag (const juce::MouseEvent& event)
{
    if (cycleDrag != CycleDrag::none)
    {
        dragCycle (event);
        return;
    }

    if (! isDragging)
        return;

    // Command places freely, off the grid.
    const auto grid = event.mods.isCommandDown() ? roughtrack::SnapGrid::off : snap;

    if (isDrums (dragging.track))
    {
        // Parts are whole bars, so they always land on bar lines.
        const auto bars = juce::roundToInt ((float) event.getDistanceFromDragStartX() / pixelsPerBar());
        const auto& part = partsOriginal[(size_t) dragging.clip];

        switch (dragging.zone)
        {
            case Zone::body:  partsPreview = roughtrack::movePart (partsOriginal, dragging.clip, part.startBar + bars); break;
            case Zone::start: partsPreview = roughtrack::trimPartStart (partsOriginal, dragging.clip, part.startBar + bars); break;
            case Zone::end:
                // A loop's end is where its loop stops. Dragging it sets the end by hand, so it stops looping.
                if (part.loopsToNext && bars != 0)
                {
                    auto unlooped = partsOriginal;
                    unlooped[(size_t) dragging.clip].loopsToNext = false;
                    partsPreview = roughtrack::trimPartEnd (unlooped, dragging.clip, (int) dragLoopEnd + bars);
                }
                else
                {
                    partsPreview = part.loopsToNext ? partsOriginal : roughtrack::trimPartEnd (partsOriginal, dragging.clip, part.endBar() + bars);
                }
                break;

            case Zone::fadeIn:
            case Zone::fadeOut:
                break;
        }

        const auto& moved = partsPreview[(size_t) dragging.clip];
        landingSample = (juce::int64) std::llround ((dragging.zone == Zone::start || dragging.zone == Zone::body ? moved.startBar : moved.endBar())
                                                    * samplesPerBar());
        repaint();
        return;
    }

    const auto distance = toClipUnits (dragging.track, pixelsToSamples ((float) event.getDistanceFromDragStartX()));
    dragPreview = dragClip (dragOriginal, dragging.clip, dragging.zone, distance, grid, unitsPerBeat (dragging.track), dragLoopEnd);

    // The edge being dragged is the one to show. A trim can stop short of where it's aimed,
    // so it's read back from the result; a loop's end lands exactly where it was aimed.
    const auto& moved = dragPreview[(size_t) dragging.clip];

    // A fade shows itself as it's dragged, so there's no landing line.
    if (dragging.zone == Zone::fadeIn || dragging.zone == Zone::fadeOut)
    {
        landingSample = -1;
        repaint();
        return;
    }

    const auto loopEndDragged = dragging.zone == Zone::end && dragOriginal[(size_t) dragging.clip].loopsToNext;

    const auto edge = loopEndDragged ? roughtrack::snapToGrid (dragLoopEnd + distance, grid, unitsPerBeat (dragging.track))
                                     : (dragging.zone == Zone::end ? moved.timelineEnd() : moved.timelineStart);
    landingSample = toSamples (dragging.track, edge);

    repaint();
}

roughtrack::ClipList TimelineView::dragClip (const roughtrack::ClipList& clips,
                                             int index,
                                             Zone zone,
                                             juce::int64 distance,
                                             roughtrack::SnapGrid grid,
                                             double beat,
                                             juce::int64 loopEnd)
{
    const auto& clip = clips[(size_t) index];

    // A click that doesn't move anything mustn't nudge a clip onto the grid.
    if (distance == 0)
        return clips;

    // The edge snaps where it lands, not by how far it moved, so an edge that started off
    // the grid ends up on it.
    const auto snapped = [grid, beat] (juce::int64 position) { return roughtrack::snapToGrid (position, grid, beat); };

    switch (zone)
    {
        case Zone::body:
            return roughtrack::moveClip (clips, index, snapped (clip.timelineStart + distance));

        case Zone::start:
            return roughtrack::trimClipStart (clips, index, snapped (clip.timelineStart + distance));

        case Zone::end:
            // Dragging a loop's end keeps what it was playing as separate clips, up to where it's let go.
            if (clip.loopsToNext)
                return distance != 0 ? roughtrack::bakeLoop (clips, index, snapped (loopEnd + distance)) : clips;

            return roughtrack::trimClipEnd (clips, index, snapped (clip.timelineEnd() + distance));

        // Fades are usually short, so they're set freely rather than on the grid.
        case Zone::fadeIn:
            return roughtrack::setClipFades (clips, index, clip.fadeIn + distance, clip.fadeOut);

        case Zone::fadeOut:
            return roughtrack::setClipFades (clips, index, clip.fadeIn, clip.fadeOut - distance);
    }

    return clips;
}

void TimelineView::mouseUp (const juce::MouseEvent&)
{
    if (cycleDrag != CycleDrag::none)
    {
        const auto drag = std::exchange (cycleDrag, CycleDrag::none);

        // A press that never became a drag is a click, which moves the playhead.
        if (drag == CycleDrag::pending || (drag == CycleDrag::move && cyclePreview == cycleOriginal))
        {
            if (onSeek != nullptr)
                onSeek (cycleAnchor);
        }
        else if ((drag == CycleDrag::create || cyclePreview != cycleOriginal) && onCycleEdit != nullptr)
        {
            onCycleEdit (cyclePreview);
        }

        repaint();
        return;
    }

    if (! isDragging)
        return;

    isDragging = false;
    landingSample = -1;

    if (dragPreview != dragOriginal && onEdit != nullptr)
    {
        const auto* name = dragging.zone == Zone::body      ? "Move Clip"
                           : dragging.zone == Zone::fadeIn  ? "Fade In"
                           : dragging.zone == Zone::fadeOut ? "Fade Out"
                                                            : "Trim Clip";
        onEdit (dragging.track, dragPreview, name);
    }

    if (partsPreview != partsOriginal && onDrumEdit != nullptr)
        onDrumEdit (dragging.track, partsPreview, dragging.zone == Zone::body ? "Move Part" : "Trim Part");

    repaint();
}

void TimelineView::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCommandDown())
    {
        setZoom (pixelsPerSecond * (wheel.deltaY > 0.0f ? zoomStep : 1.0 / zoomStep), event.position.x);
        return;
    }

    const auto mostlyVertical = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX);

    if (mostlyVertical && lanesHeight() > getHeight() - rulerHeight - scrollBarHeight && onScrollLanes != nullptr)
    {
        onScrollLanes (wheel.deltaY);
        return;
    }

    const auto delta = mostlyVertical ? wheel.deltaY : wheel.deltaX;
    scrollBar.setCurrentRangeStart (viewStartSeconds - (double) delta * (double) getWidth() / pixelsPerSecond);
}

void TimelineView::mouseMagnify (const juce::MouseEvent& event, float scaleFactor)
{
    setZoom (pixelsPerSecond * (double) scaleFactor, event.position.x);
}

juce::Array<juce::File> TimelineView::findTakes (int track) const
{
    auto takes = sessionDirectory.findChildFiles (juce::File::findFiles, false, isMidi (track) ? "*.mid" : "*.wav");

    takes.sort();
    return takes;
}

void TimelineView::addTake (int track, const juce::File& file)
{
    if (isMidi (track))
    {
        const auto take = engine.loadMidiTake (file);

        if (take == nullptr || onEdit == nullptr)
            return;

        roughtrack::Clip clip;
        clip.file = file;
        clip.timelineStart = toClipUnits (track, engine.getPosition());
        clip.length = take->length;
        clip.fileLength = take->length;

        auto clips = engine.getClips (track);
        clips.push_back (clip);
        onEdit (track, clips, "Add Take");
        return;
    }

    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || onEdit == nullptr)
        return;

    roughtrack::Clip clip;
    clip.file = file;
    clip.timelineStart = engine.getPosition();
    clip.length = reader->lengthInSamples;
    clip.fileLength = reader->lengthInSamples;

    auto clips = engine.getClips (track);
    clips.push_back (clip);
    onEdit (track, clips, "Add Take");
}

juce::PopupMenu TimelineView::createGrooveMenu (const std::vector<roughtrack::Groove>& grooves,
                                                const juce::String& style,
                                                bool fills,
                                                double beatsPerMinute,
                                                const std::shared_ptr<GroovePreview>& preview)
{
    std::vector<int> near, others;

    for (size_t i = 0; i < grooves.size(); ++i)
        if (grooves[i].style == style && grooves[i].isFill == fills)
            (roughtrack::isNearTempo (grooves[i], beatsPerMinute) ? near : others).push_back ((int) i);

    juce::PopupMenu menu;

    const auto addGrooves = [&] (const std::vector<int>& indices)
    {
        for (const auto index : indices)
        {
            juce::PopupMenu::Item item (grooves[(size_t) index].name);
            item.itemID = firstGrooveMenuId + index;

            if (preview != nullptr)
                item.customComponent = new GrooveMenuItem (preview, index, item.text);

            menu.addItem (std::move (item));
        }
    };

    if (! near.empty())
    {
        menu.addSectionHeader ("Near " + juce::String (juce::roundToInt (beatsPerMinute)) + " BPM");
        addGrooves (near);

        if (! others.empty())
            menu.addSectionHeader ("Other Tempos");
    }

    addGrooves (others);
    return menu;
}

void TimelineView::showDrumMenu (const Hit& hit, juce::Point<float> position)
{
    const auto& grooves = engine.getGrooves().getGrooves();

    juce::StringArray styles;

    for (const auto& groove : grooves)
        styles.addIfNotAlreadyThere (groove.style);

    juce::PopupMenu beatMenu, fillMenu;

    for (const auto& style : styles)
    {
        const auto beats = createGrooveMenu (grooves, style, false, engine.getTempo(), groovePreview);
        const auto fills = createGrooveMenu (grooves, style, true, engine.getTempo(), groovePreview);

        beatMenu.addSubMenu (style, beats, beats.containsAnyActiveItems());
        fillMenu.addSubMenu (style, fills, fills.containsAnyActiveItems());
    }

    juce::PopupMenu menu;

    if (hit.isClip())
    {
        menu.addItem (splitMenuId, "Split Here");
        menu.addItem (deleteMenuId, "Delete Part");
        menu.addItem (createLoopItem (engine.getDrumParts (hit.track)[(size_t) hit.clip].loopsToNext));
        menu.addSeparator();
    }

    menu.addSubMenu ("Add Groove Here", beatMenu, ! grooves.empty());
    menu.addSubMenu ("Add Fill Here", fillMenu, ! grooves.empty());

    const auto bar = juce::jmax (0, xToBar (position.x));
    const auto splitBar = juce::roundToInt ((double) xToSample (position.x) / samplesPerBar());

    groovePreview->begin (hit.track);

    // Three levels deep, a style's grooves can open to the left over the first level. As
    // separate windows on macOS they'd go behind it, so they're drawn inside the main window,
    // where the newest is always on top.
    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetScreenArea ({ juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition() })
                            .withParentComponent (getTopLevelComponent()),
                        [safeThis = juce::Component::SafePointer (this), preview = groovePreview, hit, bar, splitBar] (int result)
                        {
                            // However the menu closed, nothing keeps playing.
                            preview->end();

                            if (safeThis == nullptr || result == 0 || ! safeThis->canEdit() || safeThis->onDrumEdit == nullptr)
                                return;

                            const auto& parts = safeThis->engine.getDrumParts (hit.track);

                            if (result >= firstGrooveMenuId)
                            {
                                const auto& groove = safeThis->engine.getGrooves().getGrooves()[(size_t) (result - firstGrooveMenuId)];
                                auto edited = parts;
                                edited.push_back ({ groove.id, bar, groove.bars, 0 });
                                safeThis->onDrumEdit (hit.track, edited, groove.isFill ? "Add Fill" : "Add Groove");
                            }
                            else if (! hit.isClip() || hit.clip >= (int) parts.size())
                                return;
                            else if (result == splitMenuId)
                                safeThis->onDrumEdit (hit.track, roughtrack::splitPart (parts, hit.clip, splitBar), "Split Part");
                            else if (result == deleteMenuId)
                                safeThis->deleteSelectedClip();
                            else if (result == loopMenuId)
                            {
                                auto edited = parts;
                                auto& part = edited[(size_t) hit.clip];
                                part.loopsToNext = ! part.loopsToNext;
                                safeThis->onDrumEdit (hit.track, edited, part.loopsToNext ? "Loop Until Next Part" : "Stop Looping");
                            }
                        });
}

void TimelineView::showMenu (const Hit& hit, juce::Point<float> position)
{
    if (isDrums (hit.track))
    {
        showDrumMenu (hit, position);
        return;
    }

    juce::PopupMenu menu;

    if (hit.isClip())
    {
        menu.addItem (splitMenuId, "Split Here");
        menu.addItem (deleteMenuId, "Delete Clip");
        menu.addItem (createLoopItem (engine.getClips (hit.track)[(size_t) hit.clip].loopsToNext));

        if (isMidi (hit.track))
        {
            juce::PopupMenu quantizeMenu;
            auto id = firstQuantizeMenuId;

            for (const auto& grid : quantizeGrids)
                quantizeMenu.addItem (id++, grid.name);

            menu.addSubMenu ("Quantize", quantizeMenu);
        }

        menu.addSeparator();
    }

    const auto takes = findTakes (hit.track);

    juce::PopupMenu takeMenu;

    for (int i = 0; i < takes.size(); ++i)
        takeMenu.addItem (firstTakeMenuId + i, takes[i].getFileName());

    menu.addSubMenu ("Add Take at Playhead", takeMenu, ! takes.isEmpty());

    const auto splitAt = roughtrack::snapToGrid (toClipUnits (hit.track, xToSample (position.x)), snap, unitsPerBeat (hit.track));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition() }),
                        [safeThis = juce::Component::SafePointer (this), hit, splitAt, takes] (int result)
                        {
                            if (safeThis == nullptr || result == 0 || ! safeThis->canEdit() || safeThis->onEdit == nullptr)
                                return;

                            const auto& clips = safeThis->engine.getClips (hit.track);

                            if (result >= firstTakeMenuId)
                                safeThis->addTake (hit.track, takes[result - firstTakeMenuId]);
                            else if (! hit.isClip() || hit.clip >= (int) clips.size())
                                return;
                            else if (result >= firstQuantizeMenuId)
                                safeThis->quantizeClip (hit.track, hit.clip, quantizeGrids[result - firstQuantizeMenuId].ticks);
                            else if (result == splitMenuId)
                                safeThis->onEdit (hit.track, roughtrack::splitClip (clips, hit.clip, splitAt), "Split Clip");
                            else if (result == deleteMenuId)
                                safeThis->deleteSelectedClip();
                            else if (result == loopMenuId)
                            {
                                auto edited = clips;
                                auto& clip = edited[(size_t) hit.clip];
                                clip.loopsToNext = ! clip.loopsToNext;
                                safeThis->onEdit (hit.track, edited, clip.loopsToNext ? "Loop Until Next Part" : "Stop Looping");
                            }
                        });
}

juce::PopupMenu::Item TimelineView::createLoopItem (bool isLooping)
{
    juce::PopupMenu::Item item ("Loop Until Next Part");
    item.itemID = loopMenuId;
    item.isTicked = isLooping;
    return item;
}

void TimelineView::quantizeClip (int track, int clip, int gridTicks)
{
    const auto edited = engine.quantizeClip (track, clip, gridTicks);

    if (edited != engine.getClips (track) && onEdit != nullptr)
        onEdit (track, edited, "Quantize");
}

bool TimelineView::deleteSelectedClip()
{
    if (! selected.isClip() || ! canEdit() || onEdit == nullptr)
        return false;

    if (isDrums (selected.track))
    {
        const auto editedParts = roughtrack::removePart (engine.getDrumParts (selected.track), selected.clip);
        const auto drumTrack = selected.track;

        selected = {};

        if (onDrumEdit != nullptr)
            onDrumEdit (drumTrack, editedParts, "Delete Part");

        return true;
    }

    const auto edited = roughtrack::removeClip (engine.getClips (selected.track), selected.clip);
    const auto track = selected.track;

    selected = {};
    onEdit (track, edited, "Delete Clip");
    return true;
}
