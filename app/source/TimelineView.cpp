#include "TimelineView.h"

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
constexpr int thumbnailResolution = 512;
constexpr int rulerLabelSpacing = 70;

constexpr int splitMenuId = 1;
constexpr int deleteMenuId = 2;
constexpr int firstQuantizeMenuId = 10;
constexpr int firstTakeMenuId = 100;
constexpr int firstGrooveMenuId = 100000;
constexpr juce::int64 midiSnapTicks = roughtrack::ticksPerBeat / 4;

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

    for (auto i = (int) clips.size() - 1; i >= 0; --i)
    {
        const auto bounds = clipBounds (track, clips[(size_t) i]);

        if (! bounds.contains (point))
            continue;

        return { track, i, findZone (bounds, point) };
    }

    return { track, -1, Zone::body };
}

TimelineView::Hit TimelineView::hitTestParts (int track, juce::Point<float> point) const
{
    const auto& parts = engine.getDrumParts (track);

    for (auto i = (int) parts.size() - 1; i >= 0; --i)
    {
        const auto bounds = partBounds (track, parts[(size_t) i]);

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

        if (isDrums (track))
        {
            paintBarLines (g, lane);

            const auto& parts = partsToShow (track);

            for (size_t i = 0; i < parts.size(); ++i)
                paintPart (g, track, parts[i], selected.track == track && selected.clip == (int) i);

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
            paintClip (g, track, clips[i], selected.track == track && selected.clip == (int) i);
    }

    paintRuler (g);

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

    g.setFont (11.0f);

    for (auto seconds = std::floor (viewStartSeconds / step) * step; ; seconds += step)
    {
        const auto x = (float) ((seconds - viewStartSeconds) * pixelsPerSecond);

        if (x > (float) getWidth())
            break;

        g.setColour (gridColour);
        g.drawVerticalLine ((int) x, (float) rulerHeight / 2.0f, (float) (getHeight() - scrollBarHeight));

        const auto minutes = (int) seconds / 60;
        g.setColour (juce::Colours::lightgrey);
        g.drawText (juce::String (minutes) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2),
                    (int) x + 3, 0, rulerLabelSpacing, rulerHeight, juce::Justification::centredLeft);
    }
}

void TimelineView::paintClip (juce::Graphics& g, int track, const roughtrack::Clip& clip, bool isSelected)
{
    const auto bounds = clipBounds (track, clip);

    if (bounds.getRight() < 0.0f || bounds.getX() > (float) getWidth())
        return;

    const auto exists = clip.file.existsAsFile();

    g.setColour (! exists ? juce::Colours::darkred : (isMidi (track) ? midiColour : clipColour));
    g.fillRoundedRectangle (bounds, 3.0f);

    if (exists && isMidi (track))
    {
        paintNotes (g, clip, bounds);
    }
    else if (exists)
    {
        auto& thumbnail = thumbnailFor (clip.file);

        // Only what's on screen is drawn, so a long take costs the same as a short one.
        const auto visible = bounds.getIntersection (getLocalBounds().toFloat());
        const auto startSeconds = (double) clip.fileOffset / sampleRate() + (double) (visible.getX() - bounds.getX()) / pixelsPerSecond;
        const auto endSeconds = startSeconds + (double) visible.getWidth() / pixelsPerSecond;

        g.setColour (waveformColour);
        thumbnail.drawChannel (g, visible.toNearestInt(), startSeconds, endSeconds, 0, 1.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.setFont (10.0f);
    g.drawText (exists ? clip.file.getFileNameWithoutExtension() : "Missing: " + clip.file.getFileName(),
                bounds.reduced (5.0f, 2.0f), juce::Justification::topLeft, true);

    g.setColour (isSelected ? juce::Colours::orange : juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds, 3.0f, isSelected ? 2.0f : 1.0f);
}

void TimelineView::paintNotes (juce::Graphics& g, const roughtrack::Clip& clip, juce::Rectangle<float> bounds)
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

    g.setColour (noteColour);

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

    g.setColour (gridColour);

    for (auto bar = juce::jmax (0, xToBar (0.0f)); barToX (bar) < (float) getWidth(); ++bar)
        g.drawVerticalLine ((int) barToX (bar), (float) lane.getY(), (float) lane.getBottom());
}

void TimelineView::paintPart (juce::Graphics& g, int track, const roughtrack::DrumPart& part, bool isSelected)
{
    const auto bounds = partBounds (track, part);

    if (bounds.getRight() < 0.0f || bounds.getX() > (float) getWidth())
        return;

    const auto* groove = engine.getGrooves().find (part.groove);

    g.setColour (groove == nullptr ? juce::Colours::darkred : (groove->isFill ? fillColour : beatColour));
    g.fillRoundedRectangle (bounds, 3.0f);

    if (groove != nullptr && pixelsPerBar() >= minimumPixelsPerBarOfHits)
        paintHits (g, part, *groove, bounds);

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (10.0f);
    g.drawText (groove != nullptr ? groove->style + " " + (groove->isFill ? "fill " : "") + groove->name : "Missing groove",
                bounds.reduced (5.0f, 2.0f), juce::Justification::topLeft, true);

    g.setColour (isSelected ? juce::Colours::orange : juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds, 3.0f, isSelected ? 2.0f : 1.0f);
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
    const auto hit = hitTestClips (event.position);

    setMouseCursor (hit.isClip() && hit.zone != Zone::body ? juce::MouseCursor::LeftRightResizeCursor
                                                           : juce::MouseCursor::NormalCursor);
}

void TimelineView::mouseDown (const juce::MouseEvent& event)
{
    if (event.y < rulerHeight)
    {
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
        isDragging = true;
    }
}

void TimelineView::mouseDrag (const juce::MouseEvent& event)
{
    if (! isDragging)
        return;

    if (isDrums (dragging.track))
    {
        const auto bars = juce::roundToInt ((float) event.getDistanceFromDragStartX() / pixelsPerBar());
        const auto& part = partsOriginal[(size_t) dragging.clip];

        switch (dragging.zone)
        {
            case Zone::body:  partsPreview = roughtrack::movePart (partsOriginal, dragging.clip, part.startBar + bars); break;
            case Zone::start: partsPreview = roughtrack::trimPartStart (partsOriginal, dragging.clip, part.startBar + bars); break;
            case Zone::end:   partsPreview = roughtrack::trimPartEnd (partsOriginal, dragging.clip, part.endBar() + bars); break;
        }

        repaint();
        return;
    }

    auto delta = toClipUnits (dragging.track, pixelsToSamples ((float) event.getDistanceFromDragStartX()));

    if (isMidi (dragging.track))
        delta = (juce::int64) std::llround ((double) delta / (double) midiSnapTicks) * midiSnapTicks;

    const auto& clip = dragOriginal[(size_t) dragging.clip];

    switch (dragging.zone)
    {
        case Zone::body:  dragPreview = roughtrack::moveClip (dragOriginal, dragging.clip, clip.timelineStart + delta); break;
        case Zone::start: dragPreview = roughtrack::trimClipStart (dragOriginal, dragging.clip, clip.timelineStart + delta); break;
        case Zone::end:   dragPreview = roughtrack::trimClipEnd (dragOriginal, dragging.clip, clip.timelineEnd() + delta); break;
    }

    repaint();
}

void TimelineView::mouseUp (const juce::MouseEvent&)
{
    if (! isDragging)
        return;

    isDragging = false;

    if (dragPreview != dragOriginal && onEdit != nullptr)
        onEdit (dragging.track, dragPreview, dragging.zone == Zone::body ? "Move Clip" : "Trim Clip");

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
                                                double beatsPerMinute)
{
    std::vector<int> near, others;

    for (size_t i = 0; i < grooves.size(); ++i)
        if (grooves[i].style == style && grooves[i].isFill == fills)
            (roughtrack::isNearTempo (grooves[i], beatsPerMinute) ? near : others).push_back ((int) i);

    juce::PopupMenu menu;

    const auto addGrooves = [&] (const std::vector<int>& indices)
    {
        for (const auto index : indices)
            menu.addItem (firstGrooveMenuId + index, grooves[(size_t) index].name);
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
        const auto beats = createGrooveMenu (grooves, style, false, engine.getTempo());
        const auto fills = createGrooveMenu (grooves, style, true, engine.getTempo());

        beatMenu.addSubMenu (style, beats, beats.containsAnyActiveItems());
        fillMenu.addSubMenu (style, fills, fills.containsAnyActiveItems());
    }

    juce::PopupMenu menu;

    if (hit.isClip())
    {
        menu.addItem (splitMenuId, "Split Here");
        menu.addItem (deleteMenuId, "Delete Part");
        menu.addSeparator();
    }

    menu.addSubMenu ("Add Groove Here", beatMenu, ! grooves.empty());
    menu.addSubMenu ("Add Fill Here", fillMenu, ! grooves.empty());

    const auto bar = juce::jmax (0, xToBar (position.x));
    const auto splitBar = juce::roundToInt ((double) xToSample (position.x) / samplesPerBar());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition() }),
                        [safeThis = juce::Component::SafePointer (this), hit, bar, splitBar] (int result)
                        {
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

    const auto splitAt = toClipUnits (hit.track, xToSample (position.x));

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
                        });
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
