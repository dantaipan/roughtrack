#pragma once

#include "roughtrack/AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <set>

/** Decides when a hovered groove starts and stops playing. Resting on one for the
    delay starts it; moving off, or onto another, stops it and starts the wait again.
    A quick sweep down a menu never waits long enough to make a sound.
*/
class HoverDelay
{
public:
    static constexpr double delayMs = 600.0;

    enum class Change
    {
        none,
        start,
        stop
    };

    /** Given the item under the pointer, or -1 for none, and the time now. */
    Change update (int item, double nowMs);

    int getHovered() const noexcept { return hovered; }
    int getPlaying() const noexcept { return playing; }

    /** How far through the wait the hovered item is, from 0 to 1. */
    float getProgress (double nowMs) const noexcept;

private:
    int hovered = -1, playing = -1;
    double hoveredSince = 0.0;
};

class GrooveMenuItem;

/** Plays the groove under the pointer in a groove menu, through one drum track. It's
    shared by the menu's items, which only draw what it says is happening.
*/
class GroovePreview final : private juce::Timer
{
public:
    explicit GroovePreview (roughtrack::AudioEngine&);
    ~GroovePreview() override;

    /** Call as a groove menu opens for a track, and end() once it closes however it closes. */
    void begin (int track);
    void end();

    float getProgress (int groove) const noexcept;
    bool isPlaying (int groove) const noexcept;

private:
    friend class GrooveMenuItem;

    void timerCallback() override;
    void repaintItems();

    roughtrack::AudioEngine& engine;
    std::set<GrooveMenuItem*> items;
    HoverDelay delay;
    int track = -1;
    int lastHovered = -1, lastPlaying = -1;
};

/** A groove in the menu: its name, then a ring that fills while you wait on it, and
    moving bars once it plays. Clicking it chooses the groove as usual.
*/
class GrooveMenuItem final : public juce::PopupMenu::CustomComponent
{
public:
    GrooveMenuItem (std::shared_ptr<GroovePreview>, int grooveIndex, const juce::String& name);
    ~GrooveMenuItem() override;

    int getGrooveIndex() const noexcept { return grooveIndex; }

    void getIdealSize (int& idealWidth, int& idealHeight) override;
    void paint (juce::Graphics&) override;

private:
    std::shared_ptr<GroovePreview> preview;
    const int grooveIndex;
    const juce::String name;
};
