#include "MasterStrip.h"

#include "ConsoleLookAndFeel.h"
#include "TrackStrip.h"

namespace
{

constexpr int nameHeight = 22;
constexpr int vuHeight = 70;
constexpr int textBoxHeight = 18;
constexpr int faderWidth = 64;

} // namespace

MasterStrip::MasterStrip (roughtrack::AudioEngine& engineToControl) : engine (engineToControl)
{
    nameLabel.setText ("Master", juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (nameLabel);

    vuMeter.setTooltip ("The full mix. The light means a clip: click it to clear it.");
    addAndMakeVisible (vuMeter);

    setUpFader (fader);
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, faderWidth, textBoxHeight);
    fader.onValueChange = [this]
    {
        engine.setMasterGain (juce::Decibels::decibelsToGain ((float) fader.getValue(), -60.0f));
    };
    addAndMakeVisible (fader);

    refreshFromEngine();
}

void MasterStrip::refreshFromEngine()
{
    fader.setValue (juce::Decibels::gainToDecibels (engine.getMasterGain(), -60.0f), juce::dontSendNotification);
}

void MasterStrip::updateMeter()
{
    vuMeter.update (engine.getMasterLevel(), engine.takeMasterPeak(), false);
}

void MasterStrip::paint (juce::Graphics& g)
{
    g.setColour (console::widget);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
    drawFaderScale (g, fader, getLocalBounds().withLeft (fader.getRight()).withTrimmedRight (2));
}

void MasterStrip::resized()
{
    auto area = getLocalBounds().reduced (6);

    nameLabel.setBounds (area.removeFromTop (nameHeight));
    area.removeFromTop (4);
    vuMeter.setBounds (area.removeFromTop (vuHeight));
    area.removeFromTop (8);
    fader.setBounds (area.removeFromLeft (faderWidth));
}
