#include "ConsolePanel.h"

#include "ConsoleLookAndFeel.h"

namespace
{

constexpr int headerHeight = 26;
constexpr int headerGap = 2;
constexpr int channelWidth = 104;
constexpr int masterWidth = 124;
constexpr int scrollBarHeight = 12;
constexpr int masterGap = 6;
constexpr int linesPerWheelStep = 300;
constexpr int windowHeight = 560;

} // namespace

ConsolePanel::ConsolePanel (roughtrack::AudioEngine& engineToShow)
    : engine (engineToShow), master (engineToShow)
{
    title.setText ("Console", juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, console::dimText);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (title);

    popOutButton.onClick = [this]
    {
        if (onPopOutToggle != nullptr)
            onPopOutToggle();
    };
    addAndMakeVisible (popOutButton);
    setInWindow (false);

    addAndMakeVisible (channelHolder);
    channelHolder.setInterceptsMouseClicks (false, true);

    ghostTrack.onClick = [this] (juce::Point<int> screenPosition)
    {
        if (onAddTrack != nullptr)
            onAddTrack (screenPosition);
    };
    channelHolder.addAndMakeVisible (ghostTrack);

    scrollBar.setAutoHide (false);
    scrollBar.addListener (this);
    addChildComponent (scrollBar);

    addAndMakeVisible (master);
}

TrackStrip& ConsolePanel::addChannel (int track)
{
    auto& channel = channels[(size_t) track];

    if (channel == nullptr)
    {
        channel = std::make_unique<TrackStrip> (engine, track);
        channel->setLayout (TrackStrip::Layout::channel);
        channelHolder.addAndMakeVisible (*channel);
    }

    return *channel;
}

TrackStrip* ConsolePanel::getChannel (int track) const
{
    return channels[(size_t) track].get();
}

void ConsolePanel::removeChannel (int track)
{
    channels[(size_t) track].reset();
}

void ConsolePanel::removeAllChannels()
{
    for (auto& channel : channels)
        channel.reset();
}

void ConsolePanel::setInWindow (bool isInWindow)
{
    popOutButton.setButtonText (isInWindow ? "Dock" : "Pop Out");
    popOutButton.setTooltip (isInWindow ? "Put the console back under the timeline" : "Open the console in its own window");
    title.setVisible (! isInWindow);
    resized();
}

int ConsolePanel::getMinimumHeight()
{
    return headerHeight + headerGap + TrackStrip::getMinimumChannelHeight() + scrollBarHeight;
}

int ConsolePanel::getWidthFor (int numTracks)
{
    return (numTracks + 1) * channelWidth + masterGap + masterWidth;
}

void ConsolePanel::paint (juce::Graphics& g)
{
    g.fillAll (console::panel);

    g.setColour (console::outline);
    g.fillRect (0, headerHeight - 1, getWidth(), 1);
}

void ConsolePanel::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (headerHeight).reduced (2, 3);
    popOutButton.setBounds (header.removeFromRight (78));
    title.setBounds (header);

    area.removeFromTop (headerGap);

    auto scrollBarArea = area.removeFromBottom (scrollBarHeight);
    master.setBounds (area.removeFromRight (masterWidth));
    area.removeFromRight (masterGap);
    scrollBar.setBounds (scrollBarArea.withWidth (area.getWidth()));
    channelHolder.setBounds (area);

    layOutChannels();
}

void ConsolePanel::layOutChannels()
{
    const auto& order = engine.getTrackOrder();
    const auto totalWidth = ((int) order.size() + 1) * channelWidth;

    scrollBar.setRangeLimits (0.0, juce::jmax (1, totalWidth));
    scrollBar.setCurrentRange (scrollBar.getCurrentRangeStart(), juce::jmax (1, channelHolder.getWidth()));
    scrollBar.setVisible (totalWidth > channelHolder.getWidth());

    const auto offset = juce::roundToInt (scrollBar.getCurrentRangeStart());

    for (size_t position = 0; position < order.size(); ++position)
        if (auto* channel = channels[(size_t) order[position]].get())
            channel->setBounds ((int) position * channelWidth - offset, 0, channelWidth, channelHolder.getHeight());

    ghostTrack.setBounds ((int) order.size() * channelWidth - offset, 0, channelWidth, channelHolder.getHeight());
}

void ConsolePanel::scrollBarMoved (juce::ScrollBar*, double)
{
    layOutChannels();
}

void ConsolePanel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    scrollBar.setCurrentRangeStart (scrollBar.getCurrentRangeStart() - (double) (wheel.deltaX + wheel.deltaY) * linesPerWheelStep);
}

PanelDivider::PanelDivider()
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setTooltip ("Drag to resize the timeline and the console");
}

void PanelDivider::paint (juce::Graphics& g)
{
    g.setColour (isMouseOverOrDragging() ? console::dimText : console::outline);
    g.fillRoundedRectangle (getLocalBounds().withSizeKeepingCentre (40, 2).toFloat(), 1.0f);
}

void PanelDivider::mouseDown (const juce::MouseEvent&)
{
    if (onDragStart != nullptr)
        onDragStart();
}

void PanelDivider::mouseDrag (const juce::MouseEvent& event)
{
    if (onDrag != nullptr)
        onDrag (event.getDistanceFromDragStartY());
}

ConsoleWindow::ConsoleWindow (ConsolePanel& panel)
    : DocumentWindow ("Console", console::panel, closeButton | minimiseButton)
{
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setResizeLimits (ConsolePanel::getWidthFor (1), ConsolePanel::getMinimumHeight(), 16384, 16384);
    setContentNonOwned (&panel, false);
    centreWithSize (juce::jmin (ConsolePanel::getWidthFor (8), 1200), windowHeight);
}

void ConsoleWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}

bool ConsoleWindow::keyPressed (const juce::KeyPress& key)
{
    return onKey != nullptr && onKey (key);
}
