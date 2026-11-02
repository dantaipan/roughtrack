#pragma once

#include "ChannelWindow.h"
#include "MasterStrip.h"
#include "PluginWindow.h"
#include "TimelineView.h"
#include "TrackStrip.h"
#include "TunerRibbon.h"

#include "rifftrack/PluginLibrary.h"
#include "rifftrack/Session.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

inline constexpr auto scanChildArgument = "--scan-plugin";

juce::String describeAudioDevices();
juce::String describeMicrophoneAccess();

class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::ScrollBar::Listener,
                            private juce::Timer
{
public:
    /** A session folder given here overrides the last one used, and isn't remembered. */
    explicit MainComponent (const juce::File& sessionToOpen = {});
    ~MainComponent() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Opens a track's channel window and returns it. */
    juce::Component* showChannelWindow (int track);

    /** Switches between the timeline and the full console. */
    void showConsole (bool shouldShow);
    void showTuner (bool shouldShow);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    void startAudio();
    void useInterfaceInputs();
    void enableMidiInputs();
    juce::String deviceDescription() const;
    void deviceChanged();
    void updateStatus();

    void showAudioSettings();
    void confirmCalibration();
    void finishCalibration();
    juce::String latencyKey() const;

    void createStrip (int track);
    void addTrack (const juce::String& baseName);
    void loadDefaultInstrument (int track);
    void confirmDeleteTrack (int track);
    void deleteTrack (int track);
    void showAddTrackMenu();
    void layoutTracks();
    void layoutConsole();
    void updateTuner();
    juce::String nextTrackName (const juce::String& baseName) const;

    void openSession (const juce::File& directory);
    void loadCurrentSession();
    void saveSession();
    void showSessionMenu();
    void askForNewSession();
    void applyClipEdit (int track, const rifftrack::ClipList&, const juce::String& editName);
    void applyDrumEdit (int track, const rifftrack::DrumPartList&, const juce::String& editName);
    void recordingFinished();
    void clipsChanged();
    void undoOrRedo (bool redo);

    void openChannelWindow (int track);
    void openEffectsWindow();
    void closeChannelWindows();
    void refreshChannels();

    void startPluginScan();
    void showInsertMenu (int track, int slot, juce::Component& button);
    void insertClicked (int track, int slot, bool wantsMenu, juce::Component& button);
    void loadInsert (int track, int slot, const rifftrack::SavedInsert&);
    void removeInsert (int track, int slot);
    void openPluginWindow (int track, int slot);
    void refreshInsert (int track, int slot);
    void restoreInserts();
    void saveInserts();
    void showPluginMessage (const juce::String&);
    bool canChangeInserts();
    juce::File insertsFile() const;

    rifftrack::AudioEngine engine;
    juce::AudioDeviceManager deviceManager;
    juce::MidiDeviceListConnection midiDevices;
    juce::String midiInputNames;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<rifftrack::PluginLibrary> pluginLibrary;
    juce::TooltipWindow tooltips { this };

    juce::TextButton returnButton { "Return" }, playButton { "Play" }, recordButton { "Record" }, stopButton { "Stop" };
    juce::TextButton sessionButton { "Session" }, effectsButton { "Effects" }, addTrackButton { "+ Track" }, undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton clickButton { "Click" }, audioSettingsButton { "Audio" }, calibrateButton { "Calibrate" };
    juce::TextButton tunerButton { "Tuner" }, consoleButton { "Console" };
    juce::Slider tempoSlider;
    juce::Label positionLabel, statusLabel, takeWarningLabel, pluginLabel;

    std::array<std::unique_ptr<TrackStrip>, rifftrack::maxTracks> strips;
    juce::Component stripHolder;
    juce::ScrollBar trackScrollBar { true }, consoleScrollBar { false };
    int trackScrollOffset = 0;
    MasterStrip masterStrip { engine };
    TunerRibbon tuner { engine };
    int numInputChannels = 0;
    std::unique_ptr<TimelineView> timeline;
    std::array<std::unique_ptr<PanelWindow>, rifftrack::maxTracks> channelWindows;
    std::unique_ptr<PanelWindow> effectsWindow;
    std::array<std::array<std::unique_ptr<PluginWindow>, rifftrack::instrumentSlot + 1>, rifftrack::maxTracks> pluginWindows;
    int numInsertsRestoring = 0;
    bool insertsNeedKeeping = false;

    juce::File currentSession;
    const bool sessionWasGiven;
    bool sessionLoaded = false;
    juce::UndoManager undoManager;
    std::array<rifftrack::ClipList, rifftrack::maxTracks> clipsBeforeRecording;
    bool recordingInProgress = false;
    juce::String lastSavedSession;
    int autosaveCountdown = 0;

    bool calibrating = false;
    bool latencyIsCalibrated = false;
};
