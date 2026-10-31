#pragma once

#include "rifftrack/AudioEngine.h"

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

/** One track's controls. This is the plain working UI for Phase 1; the console
    design arrives in Phase 6.
*/
class TrackStrip final : public juce::Component
{
public:
    TrackStrip (rifftrack::AudioEngine& engine, int trackIndex);

    void setInputChannels (int numInputChannels);

    /** Matches the controls to the engine after a session loads. */
    void refreshFromEngine();
    void clearArm();
    void updateMeter();
    void showInsert (int slot, const juce::String& pluginName, bool bypassed);

    void showChannelActive (bool hasAmp);

    std::function<void (int slot, bool wantsMenu, juce::Component& button)> onInsertClick;
    std::function<void()> onChannelClick;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void applySelectedInput();
    void showEngineInput();
    void setUpToggle (juce::TextButton&, juce::Colour onColour);

    rifftrack::AudioEngine& engine;
    const int index;

    juce::Label nameLabel;
    juce::TextButton armButton { "Arm" }, monitorButton { "Mon" }, muteButton { "M" }, soloButton { "S" }, channelButton { "Channel" };
    juce::ComboBox inputSelector;
    juce::Slider gainSlider, panSlider;
    std::array<InsertButton, rifftrack::numInsertSlots> insertButtons;
    juce::Rectangle<int> meterBounds;
    float meterLevel = 0.0f;
};
