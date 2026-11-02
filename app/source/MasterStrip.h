#pragma once

#include "VuMeter.h"

#include "rifftrack/AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** The console's last channel: everything you hear goes through this fader. */
class MasterStrip final : public juce::Component
{
public:
    explicit MasterStrip (rifftrack::AudioEngine&);

    /** Matches the fader to the engine after a session loads. */
    void refreshFromEngine();
    void updateMeter();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    rifftrack::AudioEngine& engine;

    juce::Label nameLabel;
    VuMeter vuMeter;
    juce::Slider fader;
};
