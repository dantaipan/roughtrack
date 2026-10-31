#include "TrackStrip.h"

namespace
{

constexpr int stereoInputId = 1000;
constexpr float meterDecay = 0.8f;

} // namespace

TrackStrip::TrackStrip (rifftrack::AudioEngine& engineToControl, int trackIndex)
    : engine (engineToControl), index (trackIndex)
{
    nameLabel.setText (rifftrack::trackNames[(size_t) index], juce::dontSendNotification);
    addAndMakeVisible (nameLabel);

    setUpToggle (armButton, juce::Colours::red);
    setUpToggle (monitorButton, juce::Colours::orange);
    setUpToggle (muteButton, juce::Colours::steelblue);
    setUpToggle (soloButton, juce::Colours::goldenrod);

    armButton.onClick = [this] { engine.setTrackArmed (index, armButton.getToggleState()); };
    monitorButton.onClick = [this] { engine.setTrackMonitoring (index, monitorButton.getToggleState()); };
    muteButton.onClick = [this] { engine.setTrackMuted (index, muteButton.getToggleState()); };
    soloButton.onClick = [this] { engine.setTrackSoloed (index, soloButton.getToggleState()); };

    channelButton.onClick = [this]
    {
        if (onChannelClick != nullptr)
            onChannelClick();
    };
    addAndMakeVisible (channelButton);

    inputSelector.onChange = [this] { applySelectedInput(); };
    addAndMakeVisible (inputSelector);

    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    gainSlider.setRange (-60.0, 6.0, 0.1);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setValue (0.0, juce::dontSendNotification);
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    gainSlider.onValueChange = [this]
    {
        engine.setTrackGain (index, juce::Decibels::decibelsToGain ((float) gainSlider.getValue(), -60.0f));
    };
    addAndMakeVisible (gainSlider);

    panSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    panSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setValue (0.0, juce::dontSendNotification);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.onValueChange = [this] { engine.setTrackPan (index, (float) panSlider.getValue()); };
    addAndMakeVisible (panSlider);

    for (int slot = 0; slot < rifftrack::numInsertSlots; ++slot)
    {
        auto& button = insertButtons[(size_t) slot];
        button.onInsertClick = [this, slot, &button] (bool wantsMenu)
        {
            if (onInsertClick != nullptr)
                onInsertClick (slot, wantsMenu, button);
        };
        addAndMakeVisible (button);
        showInsert (slot, {}, false);
    }
}

void TrackStrip::showChannelActive (bool hasAmp)
{
    channelButton.setColour (juce::TextButton::buttonColourId,
                             hasAmp ? juce::Colours::darkslateblue : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
}

void TrackStrip::showInsert (int slot, const juce::String& pluginName, bool bypassed)
{
    auto& button = insertButtons[(size_t) slot];
    const auto isEmpty = pluginName.isEmpty();

    button.setButtonText (isEmpty ? juce::String ("+ Insert") : pluginName);
    button.setTooltip (isEmpty ? "Add a plugin" : "Click to open, right-click for options");
    button.setAlpha (isEmpty || bypassed ? 0.5f : 1.0f);
    button.setColour (juce::TextButton::buttonColourId,
                      isEmpty || bypassed ? getLookAndFeel().findColour (juce::TextButton::buttonColourId)
                                          : juce::Colours::darkslateblue);
}

void TrackStrip::setUpToggle (juce::TextButton& button, juce::Colour onColour)
{
    button.setClickingTogglesState (true);
    button.setColour (juce::TextButton::buttonOnColourId, onColour);
    addAndMakeVisible (button);
}

void TrackStrip::setInputChannels (int numInputChannels)
{
    inputSelector.clear (juce::dontSendNotification);

    for (int channel = 0; channel < numInputChannels; ++channel)
        inputSelector.addItem ("Input " + juce::String (channel + 1), channel + 1);

    if (numInputChannels >= 2)
        inputSelector.addItem ("Stereo 1-2", stereoInputId);

    showEngineInput();
}

void TrackStrip::showEngineInput()
{
    const auto settings = engine.getTrackSettings (index);
    const auto id = settings.inputChannels > 1 ? stereoInputId : settings.firstInput + 1;

    if (inputSelector.indexOfItemId (id) >= 0)
        inputSelector.setSelectedId (id, juce::sendNotificationSync);
    else if (inputSelector.getNumItems() > 0)
        inputSelector.setSelectedId (1, juce::sendNotificationSync);
}

void TrackStrip::clearArm()
{
    armButton.setToggleState (false, juce::dontSendNotification);
}

void TrackStrip::refreshFromEngine()
{
    const auto settings = engine.getTrackSettings (index);

    monitorButton.setToggleState (settings.monitoring, juce::dontSendNotification);
    muteButton.setToggleState (settings.muted, juce::dontSendNotification);
    soloButton.setToggleState (settings.soloed, juce::dontSendNotification);
    gainSlider.setValue (juce::Decibels::gainToDecibels (settings.gain, -60.0f), juce::dontSendNotification);
    panSlider.setValue (settings.pan, juce::dontSendNotification);

    showEngineInput();
}

void TrackStrip::applySelectedInput()
{
    const auto id = inputSelector.getSelectedId();

    if (id == stereoInputId)
        engine.setTrackInput (index, 0, 2);
    else if (id > 0)
        engine.setTrackInput (index, id - 1, 1);
}

void TrackStrip::updateMeter()
{
    meterLevel = juce::jmax (engine.takeTrackPeak (index), meterLevel * meterDecay);
    repaint (meterBounds);
}

void TrackStrip::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::black);
    g.fillRect (meterBounds);

    const auto proportion = juce::jmap (juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (meterLevel, -60.0f)),
                                        -60.0f, 0.0f, 0.0f, 1.0f);

    g.setColour (meterLevel >= 1.0f ? juce::Colours::red : juce::Colours::limegreen);
    g.fillRect (meterBounds.withWidth (juce::roundToInt ((float) meterBounds.getWidth() * proportion)));
}

void TrackStrip::resized()
{
    auto area = getLocalBounds().reduced (0, 4);
    auto insertRow = area.removeFromBottom (area.getHeight() / 2 - 2);
    area.removeFromBottom (4);

    const auto take = [&area] (int width)
    {
        auto bounds = area.removeFromLeft (width);
        area.removeFromLeft (6);
        return bounds;
    };

    nameLabel.setBounds (take (76));
    armButton.setBounds (take (44));
    monitorButton.setBounds (take (44));
    muteButton.setBounds (take (30));
    soloButton.setBounds (take (30));
    inputSelector.setBounds (take (110));
    panSlider.setBounds (take (80));
    meterBounds = area.removeFromRight (120).reduced (0, 8);
    area.removeFromRight (6);
    gainSlider.setBounds (area);

    channelButton.setBounds (insertRow.removeFromLeft (76));
    insertRow.removeFromLeft (6);
    const auto insertWidth = (insertRow.getWidth() - (rifftrack::numInsertSlots - 1) * 6) / rifftrack::numInsertSlots;

    for (auto& button : insertButtons)
    {
        button.setBounds (insertRow.removeFromLeft (insertWidth));
        insertRow.removeFromLeft (6);
    }
}
