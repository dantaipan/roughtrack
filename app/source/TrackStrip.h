#pragma once

#include "rifftrack/AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** One track's controls. This is the plain working UI for Phase 1; the console
    design arrives in Phase 6.
*/
class TrackStrip final : public juce::Component
{
public:
    TrackStrip (rifftrack::AudioEngine& engine, int trackIndex);

    void setInputChannels (int numInputChannels);
    void updateMeter();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void applySelectedInput();
    void setUpToggle (juce::TextButton&, juce::Colour onColour);

    rifftrack::AudioEngine& engine;
    const int index;

    juce::Label nameLabel;
    juce::TextButton armButton { "Arm" }, monitorButton { "Mon" }, muteButton { "M" }, soloButton { "S" };
    juce::ComboBox inputSelector;
    juce::Slider gainSlider, panSlider;
    juce::Rectangle<int> meterBounds;
    float meterLevel = 0.0f;
};
