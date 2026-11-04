#pragma once

#include "MasterStrip.h"
#include "ToolbarButtons.h"
#include "TrackStrip.h"

#include "roughtrack/AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** The mixing desk: a channel for every track, then the master fader. It sits under the
    timeline, or in its own window. The timeline's rows control the same tracks, and each
    side follows the other through the engine.
*/
class ConsolePanel final : public juce::Component, private juce::ScrollBar::Listener
{
public:
    explicit ConsolePanel (roughtrack::AudioEngine&);

    /** Makes the track's channel if it doesn't have one yet. */
    TrackStrip& addChannel (int track);
    TrackStrip* getChannel (int track) const;
    void removeChannel (int track);
    void removeAllChannels();

    /** Puts the channels in the engine's track order. */
    void layOutChannels();

    MasterStrip& getMaster() noexcept { return master; }

    /** Changes the header button between Pop Out and Dock. */
    void setInWindow (bool isInWindow);

    /** The shortest the panel can be and still show a usable fader. */
    static int getMinimumHeight();
    static int getWidthFor (int numTracks);

    std::function<void()> onPopOutToggle;
    std::function<void (juce::Point<int> screenPosition)> onAddTrack;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    roughtrack::AudioEngine& engine;
    std::array<std::unique_ptr<TrackStrip>, roughtrack::maxTracks> channels;
    juce::Label title;
    juce::TextButton popOutButton;
    juce::Component channelHolder;
    GhostTrack ghostTrack;
    juce::ScrollBar scrollBar { false };
    MasterStrip master;
};

/** The thin bar between the timeline and the docked console, dragged to resize them. */
class PanelDivider final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PanelDivider();

    std::function<void()> onDragStart;

    /** Given how far the pointer has moved down since the drag began. */
    std::function<void (int distanceDown)> onDrag;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
};

/** The console's own window. Closing it hides the console, which comes back here. */
class ConsoleWindow final : public juce::DocumentWindow
{
public:
    explicit ConsoleWindow (ConsolePanel&);

    void closeButtonPressed() override;
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void()> onClose;

    /** Space, X and the rest still reach the main window while this one has focus. */
    std::function<bool (const juce::KeyPress&)> onKey;
};
