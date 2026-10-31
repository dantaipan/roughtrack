#pragma once

#include "TrackStrip.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

juce::String describeAudioDevices();
juce::String describeMicrophoneAccess();

class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void startAudio();
    void useInterfaceInputs();
    juce::String deviceDescription() const;
    void deviceChanged();
    void updateStatus();

    void showAudioSettings();
    void confirmCalibration();
    void finishCalibration();
    juce::String latencyKey() const;

    rifftrack::AudioEngine engine;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::PropertiesFile> settings;

    juce::TextButton returnButton { "Return" }, playButton { "Play" }, recordButton { "Record" }, stopButton { "Stop" };
    juce::TextButton clickButton { "Click" }, audioSettingsButton { "Audio Settings" }, calibrateButton { "Calibrate Latency" };
    juce::Slider tempoSlider;
    juce::Label positionLabel, statusLabel, takeWarningLabel;

    std::vector<std::unique_ptr<TrackStrip>> strips;

    bool calibrating = false;
    bool latencyIsCalibrated = false;
};
