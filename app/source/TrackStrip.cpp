#include "TrackStrip.h"

#include "ConsoleLookAndFeel.h"

namespace
{

constexpr int stereoInputId = 1000;
constexpr float meterDecay = 0.8f;
constexpr int gap = 3;
constexpr int rowLineHeight = 26;
constexpr int channelLineHeight = 20;
constexpr int insertHeight = 17;
constexpr int vuHeight = 50;
constexpr int panHeight = 30;
constexpr int faderWidth = 40;
constexpr int faderTextHeight = 18;
constexpr int invertWidth = 22;
constexpr std::array<int, 7> faderMarks { 6, 0, -6, -12, -24, -36, -48 };

} // namespace

void setUpFader (juce::Slider& fader)
{
    fader.setRange (-60.0, 6.0, 0.1);
    fader.setSkewFactorFromMidPoint (-14.0);
    fader.setTextValueSuffix (" dB");
    fader.setDoubleClickReturnValue (true, 0.0);
}

void drawFaderScale (juce::Graphics& g, const juce::Slider& fader, juce::Rectangle<int> area)
{
    g.setFont (juce::FontOptions (10.0f));

    for (const auto mark : faderMarks)
    {
        const auto y = fader.getY() + juce::roundToInt (fader.getPositionOfValue (mark));

        g.setColour (mark == 0 ? console::cream : console::dimText);
        g.fillRect (area.getX(), y, 5, 1);
        g.drawText (juce::String (mark), area.getX() + 7, y - 6, area.getWidth() - 7, 12, juce::Justification::centredLeft);
    }
}

TrackStrip::TrackStrip (rifftrack::AudioEngine& engineToControl, int trackIndex)
    : engine (engineToControl), index (trackIndex), type (engineToControl.getTrackType (trackIndex))
{
    nameLabel.setText (engine.getTrackName (index), juce::dontSendNotification);
    nameLabel.setEditable (false, true, false);
    nameLabel.setTooltip ("Double-click to rename, right-click for options");
    nameLabel.addMouseListener (this, false);
    nameLabel.onTextChange = [this]
    {
        engine.setTrackName (index, nameLabel.getText());

        if (onRenamed != nullptr)
            onRenamed();
    };
    addAndMakeVisible (nameLabel);

    invertButton.setButtonText (juce::String::fromUTF8 ("\xc3\x98"));
    invertButton.setTooltip ("Flip polarity");

    setUpToggle (armButton, console::red);
    setUpToggle (monitorButton, console::amber);
    setUpToggle (muteButton, console::blue);
    setUpToggle (soloButton, console::amber);
    setUpToggle (invertButton, console::amber);

    armButton.onClick = [this] { engine.setTrackArmed (index, armButton.getToggleState()); };
    monitorButton.onClick = [this] { engine.setTrackMonitoring (index, monitorButton.getToggleState()); };
    muteButton.onClick = [this] { engine.setTrackMuted (index, muteButton.getToggleState()); };
    soloButton.onClick = [this] { engine.setTrackSoloed (index, soloButton.getToggleState()); };
    invertButton.onClick = [this] { engine.setTrackInverted (index, invertButton.getToggleState()); };

    channelButton.onClick = [this]
    {
        if (onChannelClick != nullptr)
            onChannelClick();
    };
    addAndMakeVisible (channelButton);

    inputSelector.onChange = [this] { applySelectedInput(); };
    addAndMakeVisible (inputSelector);

    setUpFader (gainSlider);
    gainSlider.setValue (0.0, juce::dontSendNotification);
    gainSlider.onValueChange = [this]
    {
        engine.setTrackGain (index, juce::Decibels::decibelsToGain ((float) gainSlider.getValue(), -60.0f));
    };
    addAndMakeVisible (gainSlider);

    panSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    panSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setValue (0.0, juce::dontSendNotification);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.setTooltip ("Pan. Double-click to centre.");
    panSlider.onValueChange = [this] { engine.setTrackPan (index, (float) panSlider.getValue()); };
    addAndMakeVisible (panSlider);

    vuMeter.setTooltip ("0 is a good recording level. The light means a clip: click it to clear it.");
    addAndMakeVisible (vuMeter);

    for (int slot = 0; slot <= rifftrack::instrumentSlot; ++slot)
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

    applyLayout();
}

void TrackStrip::setLayout (Layout newLayout)
{
    if (layout != newLayout)
    {
        layout = newLayout;
        applyLayout();
    }
}

void TrackStrip::applyLayout()
{
    const auto isChannel = layout == Layout::channel;

    gainSlider.setSliderStyle (isChannel ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (isChannel ? juce::Slider::TextBoxBelow : juce::Slider::NoTextBox, false, faderWidth + 20, faderTextHeight);
    gainSlider.setPopupDisplayEnabled (! isChannel, true, this);
    nameLabel.setJustificationType (isChannel ? juce::Justification::centred : juce::Justification::centredLeft);

    updateVisibility();
    resized();
    repaint();
}

// Drums play from the timeline, so there's nothing to record or listen to.
// A MIDI track hears its keyboard through its instrument once it's armed.
void TrackStrip::updateVisibility()
{
    const auto isChannel = layout == Layout::channel;
    const auto isAudio = type == rifftrack::TrackType::audio;

    armButton.setVisible (type != rifftrack::TrackType::drums);
    monitorButton.setVisible (isAudio);
    inputSelector.setVisible (isChannel && isAudio);
    invertButton.setVisible (isChannel && isAudio);
    panSlider.setVisible (isChannel);
    vuMeter.setVisible (isChannel);
    channelButton.setVisible (isChannel);

    for (int slot = 0; slot < rifftrack::numInsertSlots; ++slot)
        insertButtons[(size_t) slot].setVisible (isChannel);

    insertButtons[rifftrack::instrumentSlot].setVisible (isChannel && type == rifftrack::TrackType::midi);
}

void TrackStrip::showChannelActive (bool hasAmp)
{
    channelButton.setColour (juce::TextButton::buttonColourId,
                             hasAmp ? console::blue.darker (0.3f) : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
}

void TrackStrip::showInsert (int slot, const juce::String& pluginName, bool bypassed)
{
    auto& button = insertButtons[(size_t) slot];
    const auto isEmpty = pluginName.isEmpty();

    const auto isInstrument = slot == rifftrack::instrumentSlot;

    button.setButtonText (isEmpty ? juce::String (isInstrument ? "+ Instrument" : "+ Insert") : pluginName);
    button.setTooltip (isEmpty ? (isInstrument ? "Choose an instrument" : "Add a plugin") : pluginName + ". Click to open, right-click for options");
    button.setAlpha (isEmpty || bypassed ? 0.5f : 1.0f);
    button.setColour (juce::TextButton::buttonColourId,
                      isEmpty || bypassed ? getLookAndFeel().findColour (juce::TextButton::buttonColourId)
                                          : console::blue.darker (0.3f));
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
        inputSelector.addItem ("In " + juce::String (channel + 1), channel + 1);

    if (numInputChannels >= 2)
        inputSelector.addItem ("St 1-2", stereoInputId);

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

void TrackStrip::mouseDown (const juce::MouseEvent& event)
{
    if (! event.mods.isPopupMenu())
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Delete Track");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameLabel),
                        [safeThis = juce::Component::SafePointer (this)] (int result)
                        {
                            if (safeThis != nullptr && result == 1 && safeThis->onDeleteRequest != nullptr)
                                safeThis->onDeleteRequest();
                        });
}

void TrackStrip::refreshFromEngine()
{
    const auto settings = engine.getTrackSettings (index);

    armButton.setToggleState (engine.isTrackArmed (index), juce::dontSendNotification);
    monitorButton.setToggleState (settings.monitoring, juce::dontSendNotification);
    muteButton.setToggleState (settings.muted, juce::dontSendNotification);
    soloButton.setToggleState (settings.soloed, juce::dontSendNotification);
    invertButton.setToggleState (settings.inverted, juce::dontSendNotification);
    nameLabel.setText (engine.getTrackName (index), juce::dontSendNotification);
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

// An armed track shows what's arriving at its input, because that's what's recorded,
// whether or not it's being monitored.
void TrackStrip::updateMeter()
{
    const auto showsInput = type == rifftrack::TrackType::audio && engine.isTrackArmed (index);
    const auto peak = showsInput ? engine.takeTrackInputPeak (index) : engine.takeTrackPeak (index);

    if (layout == Layout::channel)
    {
        vuMeter.update (showsInput ? engine.getTrackInputLevel (index) : engine.getTrackLevel (index), peak, showsInput);
        return;
    }

    meterLevel = juce::jmax (peak, meterLevel * meterDecay);
    repaint (meterBounds);
}

void TrackStrip::paint (juce::Graphics& g)
{
    if (layout == Layout::channel)
    {
        g.setColour (console::strip);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f, 0.0f), 3.0f);
        drawFaderScale (g, gainSlider, getLocalBounds().withLeft (gainSlider.getRight()).withTrimmedRight (2));
        return;
    }

    g.setColour (console::strip);
    g.fillRect (getLocalBounds().withTrimmedBottom (1));

    g.setColour (console::recess);
    g.fillRect (meterBounds);

    const auto proportion = juce::jmap (juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (meterLevel, -60.0f)),
                                        -60.0f, 0.0f, 0.0f, 1.0f);

    g.setColour (meterLevel >= 1.0f ? console::red : console::green);
    g.fillRect (meterBounds.withWidth (juce::roundToInt ((float) meterBounds.getWidth() * proportion)));
}

void TrackStrip::resized()
{
    if (layout == Layout::channel)
        layOutChannel();
    else
        layOutRow();
}

void TrackStrip::layOutRow()
{
    auto area = getLocalBounds().reduced (6, 6);
    auto top = area.removeFromTop (rowLineHeight);

    nameLabel.setBounds (top.removeFromLeft (112));

    const auto take = [&top] (int width)
    {
        auto bounds = top.removeFromLeft (width);
        top.removeFromLeft (gap);
        return bounds;
    };

    armButton.setBounds (take (40));
    monitorButton.setBounds (take (40));
    muteButton.setBounds (take (28));
    soloButton.setBounds (take (28));

    auto bottom = area.removeFromBottom (rowLineHeight);

    meterBounds = bottom.removeFromRight (100).reduced (0, 8);
    bottom.removeFromRight (gap);
    gainSlider.setBounds (bottom);
}

void TrackStrip::layOutChannel()
{
    auto area = getLocalBounds().reduced (5, 6);

    const auto line = [&area] (int height)
    {
        auto bounds = area.removeFromTop (height);
        area.removeFromTop (gap);
        return bounds;
    };

    nameLabel.setBounds (line (channelLineHeight));
    vuMeter.setBounds (line (vuHeight));

    auto inputLine = line (channelLineHeight);
    invertButton.setBounds (inputLine.removeFromRight (invertWidth));
    inputLine.removeFromRight (gap);
    inputSelector.setBounds (inputLine);

    insertButtons[rifftrack::instrumentSlot].setBounds (inputLine.withRight (invertButton.getRight()));

    for (int slot = 0; slot < rifftrack::numInsertSlots; ++slot)
        insertButtons[(size_t) slot].setBounds (line (insertHeight));

    channelButton.setBounds (line (channelLineHeight));
    panSlider.setBounds (line (panHeight));

    const auto halves = [] (juce::Rectangle<int> bounds, juce::Component& left, juce::Component& right)
    {
        left.setBounds (bounds.removeFromLeft ((bounds.getWidth() - gap) / 2));
        bounds.removeFromLeft (gap);
        right.setBounds (bounds);
    };

    halves (line (channelLineHeight), armButton, monitorButton);
    halves (line (channelLineHeight), muteButton, soloButton);

    gainSlider.setBounds (area.removeFromLeft (faderWidth + 16));
}
