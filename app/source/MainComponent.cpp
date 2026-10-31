#include "MainComponent.h"

#include "rifftrack/AudioDevices.h"
#include "rifftrack/MicrophoneAccess.h"

namespace
{

constexpr double preferredSampleRate = 48000.0;
constexpr int preferredBufferSize = 64;
constexpr int stripHeight = 44;
constexpr int transportHeight = 40;
constexpr int statusHeight = 28;
constexpr int timerHz = 30;

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "RiffTrack";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

} // namespace

juce::String describeAudioDevices()
{
    juce::String text;

    for (const auto& device : rifftrack::listAudioDevices())
    {
        text << device.name << " [" << device.typeName << "]\n"
             << "  " << device.inputChannels << " in, " << device.outputChannels << " out\n"
             << "  " << device.currentSampleRate << " Hz, " << device.currentBufferSize << " samples ("
             << juce::String (rifftrack::bufferLatencyMs (device.currentBufferSize, device.currentSampleRate), 2)
             << " ms per buffer)\n\n";
    }

    return text.isEmpty() ? "No audio devices found.\n" : text;
}

juce::String describeMicrophoneAccess()
{
    switch (rifftrack::getMicrophoneAccess())
    {
        case rifftrack::MicrophoneAccess::granted:       return "Microphone access: granted\n";
        case rifftrack::MicrophoneAccess::denied:        return "Microphone access: denied\n";
        case rifftrack::MicrophoneAccess::restricted:    return "Microphone access: restricted\n";
        case rifftrack::MicrophoneAccess::notDetermined: return "Microphone access: not yet requested\n";
    }

    return {};
}

MainComponent::MainComponent()
    : settings (std::make_unique<juce::PropertiesFile> (settingsOptions()))
{
    engine.setSessionDirectory (juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                    .getChildFile ("RiffTrack")
                                    .getChildFile ("Session"));

    returnButton.onClick = [this]
    {
        engine.stop();
        engine.setPosition (0);
    };

    playButton.onClick = [this] { engine.play(); };
    stopButton.onClick = [this] { engine.stop(); };

    recordButton.onClick = [this]
    {
        const auto result = engine.record();

        if (result.failed())
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                                                   .withTitle ("Can't record")
                                                                   .withMessage (result.getErrorMessage())
                                                                   .withButton ("OK"),
                                          nullptr);
    };

    clickButton.setClickingTogglesState (true);
    clickButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::orange);
    clickButton.onClick = [this] { engine.setClickEnabled (clickButton.getToggleState()); };

    audioSettingsButton.onClick = [this] { showAudioSettings(); };
    calibrateButton.onClick = [this] { confirmCalibration(); };

    tempoSlider.setSliderStyle (juce::Slider::IncDecButtons);
    tempoSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 24);
    tempoSlider.setRange (30.0, 300.0, 1.0);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setValue (engine.getTempo(), juce::dontSendNotification);
    tempoSlider.onValueChange = [this] { engine.setTempo (tempoSlider.getValue()); };

    for (auto* component : std::initializer_list<juce::Component*> { &returnButton, &playButton, &recordButton, &stopButton,
                                                                     &clickButton, &tempoSlider, &positionLabel,
                                                                     &audioSettingsButton, &calibrateButton, &statusLabel,
                                                                     &takeWarningLabel })
        addAndMakeVisible (component);

    for (int track = 0; track < rifftrack::numTracks; ++track)
    {
        strips.push_back (std::make_unique<TrackStrip> (engine, track));
        addAndMakeVisible (*strips.back());
    }

    setSize (980, transportHeight + rifftrack::numTracks * stripHeight + 2 * statusHeight + 32);

    takeWarningLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    statusLabel.setText ("Waiting for microphone access", juce::dontSendNotification);

    rifftrack::requestMicrophoneAccess ([safeThis = juce::Component::SafePointer (this)] (bool granted)
                                        {
                                            juce::MessageManager::callAsync ([safeThis, granted]
                                                                             {
                                                                                 if (safeThis == nullptr)
                                                                                     return;

                                                                                 if (granted)
                                                                                     safeThis->startAudio();
                                                                                 else
                                                                                     safeThis->statusLabel.setText ("Microphone access was denied. Allow RiffTrack in System Settings > Privacy & Security > Microphone.",
                                                                                                                    juce::dontSendNotification);
                                                                             });
                                        });
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.stop();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
}

void MainComponent::startAudio()
{
    const auto savedState = settings->getXmlValue ("audioDevice");
    const auto error = deviceManager.initialise (256, 2, savedState.get(), true);

    if (error.isNotEmpty())
        statusLabel.setText (error, juce::dontSendNotification);

    if (savedState == nullptr)
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.sampleRate = preferredSampleRate;
        setup.bufferSize = preferredBufferSize;
        deviceManager.setAudioDeviceSetup (setup, true);
    }

    useInterfaceInputs();

    deviceManager.addAudioCallback (&engine);
    deviceManager.addChangeListener (this);
    deviceChanged();
    startTimerHz (timerHz);
}

void MainComponent::useInterfaceInputs()
{
    auto* type = deviceManager.getCurrentDeviceTypeObject();

    if (type == nullptr)
        return;

    std::vector<std::string> inputs;

    for (const auto& name : type->getDeviceNames (true))
        inputs.push_back (name.toStdString());

    auto setup = deviceManager.getAudioDeviceSetup();

    const juce::String preferred (rifftrack::preferredInputDevice (setup.outputDeviceName.toStdString(),
                                                                   setup.inputDeviceName.toStdString(),
                                                                   inputs));

    if (preferred == setup.inputDeviceName)
        return;

    setup.inputDeviceName = preferred;
    setup.useDefaultInputChannels = true;

    const auto error = deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty())
        statusLabel.setText (error, juce::dontSendNotification);
}

juce::String MainComponent::deviceDescription() const
{
    const auto setup = deviceManager.getAudioDeviceSetup();

    if (setup.inputDeviceName == setup.outputDeviceName)
        return setup.outputDeviceName;

    return "Out: " + setup.outputDeviceName + ", In: "
           + (setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : juce::String ("none"));
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    deviceChanged();
}

void MainComponent::deviceChanged()
{
    if (const auto state = deviceManager.createStateXml())
        settings->setValue ("audioDevice", state.get());

    auto* device = deviceManager.getCurrentAudioDevice();
    const auto numInputs = device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0;

    for (auto& strip : strips)
        strip->setInputChannels (numInputs);

    latencyIsCalibrated = settings->containsKey (latencyKey());

    if (latencyIsCalibrated)
        engine.setRecordingLatency (settings->getIntValue (latencyKey()));
}

// Latency differs between interfaces and with every rate and buffer size, so
// each combination is calibrated once and remembered.
juce::String MainComponent::latencyKey() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return "latency " + deviceDescription() + " " + juce::String (device->getCurrentSampleRate()) + " "
               + juce::String (device->getCurrentBufferSizeSamples());

    return "latency none";
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager, 0, 256, 0, 2, false, false, false, false);
    selector->setSize (520, 360);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio Settings";
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.launchAsync();
}

void MainComponent::confirmCalibration()
{
    const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon)
                                                  .withTitle ("Calibrate Latency")
                                                  .withMessage ("Connect Output 1 to Input 1 with a cable and turn your monitors and headphones down. "
                                                                "RiffTrack will play three short test sweeps.")
                                                  .withButton ("Start")
                                                  .withButton ("Cancel")
                                                  .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options,
                                  [safeThis = juce::Component::SafePointer (this)] (int result)
                                  {
                                      if (safeThis == nullptr || result != 1)
                                          return;

                                      safeThis->calibrating = safeThis->engine.startCalibration (0).wasOk();
                                  });
}

void MainComponent::finishCalibration()
{
    calibrating = false;

    const auto result = engine.finishCalibration();

    if (result.succeeded)
    {
        settings->setValue (latencyKey(), result.latencySamples);
        latencyIsCalibrated = true;
    }

    const auto sampleRate = engine.getSampleRate();
    const auto message = result.succeeded
                             ? "Round-trip latency: " + juce::String (rifftrack::bufferLatencyMs (result.latencySamples, sampleRate), 2)
                                   + " ms (" + result.message + ")"
                             : result.message;

    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (result.succeeded ? juce::MessageBoxIconType::InfoIcon
                                                                                           : juce::MessageBoxIconType::WarningIcon)
                                                           .withTitle ("Calibrate Latency")
                                                           .withMessage (message)
                                                           .withButton ("OK"),
                                  nullptr);
}

void MainComponent::timerCallback()
{
    if (calibrating && (engine.isCalibrationFinished() || deviceManager.getCurrentAudioDevice() == nullptr))
        finishCalibration();

    for (auto& strip : strips)
        strip->updateMeter();

    updateStatus();
}

void MainComponent::updateStatus()
{
    const auto sampleRate = engine.getSampleRate();
    const auto seconds = sampleRate > 0.0 ? (double) engine.getPosition() / sampleRate : 0.0;

    positionLabel.setText (juce::String (seconds, 2) + " s", juce::dontSendNotification);

    const auto state = engine.getState();
    recordButton.setColour (juce::TextButton::buttonColourId,
                            state == rifftrack::TransportState::recording ? juce::Colours::red
                                                                          : getLookAndFeel().findColour (juce::TextButton::buttonColourId));

    const auto health = engine.getLastTakeHealth();

    takeWarningLabel.setText (health.isClean() ? juce::String()
                                               : "Last take has audio gaps (" + juce::String (health.dropouts) + " dropouts, "
                                                     + juce::String (health.droppedSamples) + " samples lost). Record it again if it matters.",
                              juce::dontSendNotification);

    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        statusLabel.setText ("No audio device", juce::dontSendNotification);
        return;
    }

    const auto stats = engine.getStats();
    const auto latency = rifftrack::bufferLatencyMs (engine.getRecordingLatency(), sampleRate);

    juce::String status;
    status << deviceDescription() << "  |  " << sampleRate << " Hz, " << device->getCurrentBufferSizeSamples() << " samples  |  "
           << "round trip " << juce::String (latency, 2) << " ms (" << (latencyIsCalibrated ? "calibrated" : "reported") << ")  |  "
           << "dropouts " << stats.dropouts << "  |  disk " << stats.playbackUnderrunSamples + stats.droppedRecordingSamples;

    statusLabel.setText (status, juce::dontSendNotification);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto transport = area.removeFromTop (transportHeight).reduced (0, 4);

    for (auto* button : { &returnButton, &playButton, &recordButton, &stopButton, &clickButton })
    {
        button->setBounds (transport.removeFromLeft (72));
        transport.removeFromLeft (6);
    }

    tempoSlider.setBounds (transport.removeFromLeft (170));
    calibrateButton.setBounds (transport.removeFromRight (140));
    transport.removeFromRight (6);
    audioSettingsButton.setBounds (transport.removeFromRight (120));
    positionLabel.setBounds (transport);

    statusLabel.setBounds (area.removeFromBottom (statusHeight));
    takeWarningLabel.setBounds (area.removeFromBottom (statusHeight));

    for (auto& strip : strips)
        strip->setBounds (area.removeFromTop (stripHeight));
}
