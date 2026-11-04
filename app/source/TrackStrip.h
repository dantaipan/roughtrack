#pragma once

#include "VuMeter.h"

#include "roughtrack/AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** A plugin slot, which tells a normal click from a right-click. */
class InsertButton final : public juce::TextButton
{
public:
    using juce::Button::clicked;

    void clicked (const juce::ModifierKeys& modifiers) override
    {
        if (onInsertClick != nullptr)
            onInsertClick (modifiers.isPopupMenu());
    }

    std::function<void (bool wantsMenu)> onInsertClick;
};

/** One track's controls. It's a short row beside the timeline, or a full channel in
    the console, and the same controls move between them so the two can't disagree.
    Double-click the name to change it, or right-click it to delete the track.
*/
class TrackStrip final : public juce::Component
{
public:
    TrackStrip (roughtrack::AudioEngine& engine, int trackIndex);

    enum class Layout
    {
        row,
        channel
    };

    void setLayout (Layout);

    void setInputChannels (int numInputChannels);

    /** Matches the controls to the engine after a session loads. */
    void refreshFromEngine();

    /** Picks up changes made elsewhere, such as in this track's other strip, leaving
        alone any control that's being used right now. Cheap enough to call every frame.
    */
    void syncFromEngine();

    /** Reads, and clears, the peak this track's meters show. A track has two strips, so
        the peak is taken once and given to both.
    */
    float takePeak();
    void updateMeter (float peak);

    /** The shortest channel that still has a usable fader. */
    static int getMinimumChannelHeight();
    void showInsert (int slot, const juce::String& pluginName, bool bypassed);

    void showChannelActive (bool hasAmp);

    std::function<void (int slot, bool wantsMenu, juce::Component& button)> onInsertClick;
    std::function<void()> onChannelClick;
    std::function<void()> onDeleteRequest;
    std::function<void()> onRenamed;

    /** Dragging a row moves the track. The position is the pointer's height in the parent. */
    std::function<void (float y)> onDragMove;
    std::function<void (float y)> onDragEnd;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void applySelectedInput();
    void showEngineInput();
    void setUpToggle (juce::TextButton&, juce::Colour onColour);
    void layOutRow();
    void layOutChannel();
    void applyLayout();
    void updateVisibility();

    roughtrack::AudioEngine& engine;
    const int index;
    const roughtrack::TrackType type;
    Layout layout = Layout::row;

    juce::Label nameLabel;
    juce::TextButton armButton { "Arm" }, monitorButton { "Mon" }, muteButton { "M" }, soloButton { "S" }, channelButton { "Channel" };
    juce::TextButton invertButton;
    juce::ComboBox inputSelector;
    juce::Slider gainSlider, panSlider;
    std::array<InsertButton, roughtrack::instrumentSlot + 1> insertButtons;
    VuMeter vuMeter;
    juce::Rectangle<int> meterBounds;
    float meterLevel = 0.0f;
    bool dragging = false;
};

/** The dB marks beside a fader. */
void drawFaderScale (juce::Graphics&, const juce::Slider& fader, juce::Rectangle<int> area);

/** Gives a fader its range, and puts 0 dB well up its travel, as on a real desk. */
void setUpFader (juce::Slider&);
