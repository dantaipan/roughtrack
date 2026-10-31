#include "TimelineView.h"

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
constexpr int firstTakeMenuId = 100;

const auto clipColour = juce::Colour (0xff2f4f6f);
const auto waveformColour = juce::Colour (0xffa8c8e8);
const auto laneColour = juce::Colour (0xff202225);
const auto gridColour = juce::Colour (0xff34373c);

} // namespace

TimelineView::TimelineView (rifftrack::AudioEngine& engineToShow, int laneHeightToUse)
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
    if (selected.isClip() && selected.clip >= (int) engine.getClips (selected.track).size())
        selected = {};

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

int TimelineView::laneAt (int y) const
{
    const auto lane = (y - rulerHeight) / laneHeight;
    return y >= rulerHeight && lane < rifftrack::numTracks ? lane : -1;
}

juce::Rectangle<int> TimelineView::laneBounds (int track) const
{
    return { 0, rulerHeight + track * laneHeight, getWidth(), laneHeight };
}

juce::Rectangle<float> TimelineView::clipBounds (int track, const rifftrack::Clip& clip) const
{
    const auto lane = laneBounds (track).reduced (0, 3).toFloat();
    const auto left = sampleToX (clip.timelineStart);

    return { left, lane.getY(), juce::jmax (2.0f, sampleToX (clip.timelineEnd()) - left), lane.getHeight() };
}

const rifftrack::ClipList& TimelineView::clipsToShow (int track) const
{
    return isDragging && dragging.track == track ? dragPreview : engine.getClips (track);
}

TimelineView::Hit TimelineView::hitTestClips (juce::Point<float> point) const
{
    const auto track = laneAt ((int) point.y);

    if (track < 0)
        return {};

    const auto& clips = engine.getClips (track);

    for (auto i = (int) clips.size() - 1; i >= 0; --i)
    {
        const auto bounds = clipBounds (track, clips[(size_t) i]);

        if (! bounds.contains (point))
            continue;

        auto zone = Zone::body;

        if (bounds.getWidth() > edgeGrabPixels * 3.0f)
        {
            if (point.x < bounds.getX() + edgeGrabPixels)
                zone = Zone::start;
            else if (point.x > bounds.getRight() - edgeGrabPixels)
                zone = Zone::end;
        }

        return { track, i, zone };
    }

    return { track, -1, Zone::body };
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

    for (int track = 0; track < rifftrack::numTracks; ++track)
        for (const auto& clip : engine.getClips (track))
            lastSample = juce::jmax (lastSample, clip.timelineEnd());

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

    if (engine.getState() != rifftrack::TransportState::stopped && newX > (float) getWidth())
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

    for (int track = 0; track < rifftrack::numTracks; ++track)
    {
        const auto lane = laneBounds (track);

        g.setColour (gridColour);
        g.drawHorizontalLine (lane.getBottom() - 1, 0.0f, (float) getWidth());

        const auto& clips = clipsToShow (track);

        for (size_t i = 0; i < clips.size(); ++i)
            paintClip (g, track, clips[i], selected.track == track && selected.clip == (int) i);
    }

    paintRuler (g);

    const auto x = sampleToX (engine.getPosition());
    g.setColour (juce::Colours::white);
    g.fillRect (x - 0.5f, 0.0f, 1.5f, (float) (rulerHeight + rifftrack::numTracks * laneHeight));
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
        g.drawVerticalLine ((int) x, (float) rulerHeight / 2.0f, (float) (rulerHeight + rifftrack::numTracks * laneHeight));

        const auto minutes = (int) seconds / 60;
        g.setColour (juce::Colours::lightgrey);
        g.drawText (juce::String (minutes) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2),
                    (int) x + 3, 0, rulerLabelSpacing, rulerHeight, juce::Justification::centredLeft);
    }
}

void TimelineView::paintClip (juce::Graphics& g, int track, const rifftrack::Clip& clip, bool isSelected)
{
    const auto bounds = clipBounds (track, clip);

    if (bounds.getRight() < 0.0f || bounds.getX() > (float) getWidth())
        return;

    const auto exists = clip.file.existsAsFile();

    g.setColour (exists ? clipColour : juce::Colours::darkred);
    g.fillRoundedRectangle (bounds, 3.0f);

    if (exists)
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
    if (engine.getState() == rifftrack::TransportState::stopped)
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
        isDragging = true;
    }
}

void TimelineView::mouseDrag (const juce::MouseEvent& event)
{
    if (! isDragging)
        return;

    const auto delta = pixelsToSamples ((float) event.getDistanceFromDragStartX());
    const auto& clip = dragOriginal[(size_t) dragging.clip];

    switch (dragging.zone)
    {
        case Zone::body:  dragPreview = rifftrack::moveClip (dragOriginal, dragging.clip, clip.timelineStart + delta); break;
        case Zone::start: dragPreview = rifftrack::trimClipStart (dragOriginal, dragging.clip, clip.timelineStart + delta); break;
        case Zone::end:   dragPreview = rifftrack::trimClipEnd (dragOriginal, dragging.clip, clip.timelineEnd() + delta); break;
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

    repaint();
}

void TimelineView::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCommandDown())
    {
        setZoom (pixelsPerSecond * (wheel.deltaY > 0.0f ? zoomStep : 1.0 / zoomStep), event.position.x);
        return;
    }

    const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    scrollBar.setCurrentRangeStart (viewStartSeconds - (double) delta * (double) getWidth() / pixelsPerSecond);
}

void TimelineView::mouseMagnify (const juce::MouseEvent& event, float scaleFactor)
{
    setZoom (pixelsPerSecond * (double) scaleFactor, event.position.x);
}

juce::Array<juce::File> TimelineView::findTakes (int track) const
{
    const auto prefix = juce::String (rifftrack::trackNames[(size_t) track]).replace (" ", "-") + "-take";
    auto takes = sessionDirectory.findChildFiles (juce::File::findFiles, false, prefix + "*.wav");

    takes.sort();
    return takes;
}

void TimelineView::addTake (int track, const juce::File& file)
{
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || onEdit == nullptr)
        return;

    rifftrack::Clip clip;
    clip.file = file;
    clip.timelineStart = engine.getPosition();
    clip.length = reader->lengthInSamples;
    clip.fileLength = reader->lengthInSamples;

    auto clips = engine.getClips (track);
    clips.push_back (clip);
    onEdit (track, clips, "Add Take");
}

void TimelineView::showMenu (const Hit& hit, juce::Point<float> position)
{
    juce::PopupMenu menu;

    if (hit.isClip())
    {
        menu.addItem (splitMenuId, "Split Here");
        menu.addItem (deleteMenuId, "Delete Clip");
        menu.addSeparator();
    }

    const auto takes = findTakes (hit.track);

    juce::PopupMenu takeMenu;

    for (int i = 0; i < takes.size(); ++i)
        takeMenu.addItem (firstTakeMenuId + i, takes[i].getFileName());

    menu.addSubMenu ("Add Take at Playhead", takeMenu, ! takes.isEmpty());

    const auto splitAt = xToSample (position.x);

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
                            else if (result == splitMenuId)
                                safeThis->onEdit (hit.track, rifftrack::splitClip (clips, hit.clip, splitAt), "Split Clip");
                            else if (result == deleteMenuId)
                                safeThis->deleteSelectedClip();
                        });
}

bool TimelineView::deleteSelectedClip()
{
    if (! selected.isClip() || ! canEdit() || onEdit == nullptr)
        return false;

    const auto edited = rifftrack::removeClip (engine.getClips (selected.track), selected.clip);
    const auto track = selected.track;

    selected = {};
    onEdit (track, edited, "Delete Clip");
    return true;
}
