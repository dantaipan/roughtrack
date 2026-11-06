#pragma once

#include "AboutWindow.h"
#include "ChannelWindow.h"
#include "ConsolePanel.h"
#include "PluginWindow.h"
#include "TimelineView.h"
#include "ToolbarButtons.h"
#include "TrackStrip.h"
#include "TunerRibbon.h"

#include "roughtrack/PluginLibrary.h"
#include "roughtrack/Session.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

inline constexpr auto scanChildArgument = "--scan-plugin";

juce::String describeAudioDevices();
juce::String describeMicrophoneAccess();

class MainComponent final : public juce::Component,
                            private juce::MenuBarModel,
                            private juce::ChangeListener,
                            private juce::ScrollBar::Listener,
                            private juce::Timer
{
public:
    /** A session folder given here overrides the last one used, and isn't remembered. */
    explicit MainComponent (const juce::File& sessionToOpen = {});

    /** The narrowest window that fits the whole toolbar, with the transport showing only icons. */
    static int getMinimumWidth();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Opens a track's channel window and returns it. */
    juce::Component* showChannelWindow (int track);

    /** Shows or hides the console, under the timeline or in its own window. */
    void showConsole (bool shouldShow);

    /** Moves the console between the panel under the timeline and its own window. */
    void setConsoleInWindow (bool inWindow);

    /** The console's own window, if it's been opened. */
    juce::Component* getConsoleWindow() const noexcept { return consoleWindow.get(); }

    void showTuner (bool shouldShow);

    /** Sets the main window's smallest size, which is taller while the console is docked. */
    void updateWindowLimits();

    /** Snapshots change the view without it being remembered as the user's choice. */
    void setRemembersView (bool shouldRemember) noexcept { remembersView = shouldRemember; }

    /** True while the session's plugins are still loading in the background. */
    bool isRestoringPlugins() const noexcept { return numInsertsRestoring > 0; }

    /** Hides tooltips and passing messages, for a clean picture of the window. */
    void prepareForSnapshot();

private:
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int menuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemId, int menuIndex) override;

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
    void askWhereToMixDown();
    void mixDown (const juce::File&);
    void exportStems();
    void mixdownFinished (const juce::File&, const roughtrack::AudioEngine::BounceResult&, bool isStems);
    void restoreChosenAudioDevice();
    void confirmCalibration();
    void finishCalibration();
    juce::String latencyKey() const;

    void createStrip (int track);
    void connectStrip (TrackStrip&, int track);
    bool isConsoleDocked() const;
    void resizeConsolePanel (int distanceDown);
    void addTrack (const juce::String& baseName);
    void loadDefaultInstrument (int track);
    void confirmDeleteTrack (int track);
    void deleteTrack (int track);
    void showAddTrackMenu (juce::Point<int> screenPosition);
    int dropPositionAt (float y) const;
    void showTrackDrag (float y);
    void finishTrackDrag (int track, float y);
    void layoutTracks();
    void updateTuner();
    juce::String nextTrackName (const juce::String& baseName) const;

    void openSession (const juce::File& directory);
    void loadCurrentSession();
    void saveSession();
    void showSessionMenu();
    void showSnapMenu();
    void setSnap (roughtrack::SnapGrid);
    void askForNewSession();
    void applyClipEdit (int track, const roughtrack::ClipList&, const juce::String& editName);
    void applyDrumEdit (int track, const roughtrack::DrumPartList&, const juce::String& editName);
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
    void loadInsert (int track, int slot, const roughtrack::SavedInsert&);
    void removeInsert (int track, int slot);
    void openPluginWindow (int track, int slot);
    void refreshInsert (int track, int slot);
    void restoreInserts();
    void saveInserts();
    void showPluginMessage (const juce::String&, bool isWarning = false);
    bool canChangeInserts();
    juce::File insertsFile() const;

    roughtrack::AudioEngine engine;
    juce::AudioDeviceManager deviceManager;
    juce::MidiDeviceListConnection midiDevices;
    juce::String midiInputNames;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<roughtrack::PluginLibrary> pluginLibrary;
    juce::TooltipWindow tooltips { this };

    TransportButton returnButton { "Return", TransportButton::Icon::returnToStart }, playButton { "Play", TransportButton::Icon::play },
        stopButton { "Stop", TransportButton::Icon::stop }, recordButton { "Record", TransportButton::Icon::record };
    juce::TextButton sessionButton { "Session" }, effectsButton { "Effects" }, undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton clickButton { "Click" }, snapButton;
    GhostTrack ghostTrack;
    juce::Label dropIndicator;
    juce::TextButton tunerButton { "Tuner" }, consoleButton { "Console" };
    juce::Slider tempoSlider;
    std::vector<juce::Rectangle<int>> toolbarDividers;
    juce::Label positionLabel, statusLabel;
    MessageButton messageButton;
    juce::String latestMessage;
    bool latestMessageIsWarning = false;
    juce::uint32 showMessageUntil = 0;

    std::array<std::unique_ptr<TrackStrip>, roughtrack::maxTracks> strips;
    juce::Component stripHolder;
    juce::ScrollBar trackScrollBar { true };
    int trackScrollOffset = 0;
    ConsolePanel consolePanel { engine };
    PanelDivider panelDivider;
    std::unique_ptr<ConsoleWindow> consoleWindow;
    bool consoleInWindow = false;
    bool remembersView = true;
    int consolePanelHeight = 0;
    int consolePanelHeightAtDragStart = 0;
    TunerRibbon tuner { engine };
    int numInputChannels = 0;
    std::unique_ptr<TimelineView> timeline;
    std::array<std::unique_ptr<PanelWindow>, roughtrack::maxTracks> channelWindows;
    std::unique_ptr<PanelWindow> effectsWindow;
    std::array<std::array<std::unique_ptr<PluginWindow>, roughtrack::instrumentSlot + 1>, roughtrack::maxTracks> pluginWindows;
    int numInsertsRestoring = 0;
    bool insertsNeedKeeping = false;

    juce::File currentSession;
    const bool sessionWasGiven;
    bool sessionLoaded = false;
    juce::UndoManager undoManager;
    std::array<roughtrack::ClipList, roughtrack::maxTracks> clipsBeforeRecording;
    bool recordingInProgress = false;
    juce::String lastSavedSession;
    int autosaveCountdown = 0;

    bool calibrating = false;
    bool latencyIsCalibrated = false;
    juce::Component::SafePointer<juce::DialogWindow> audioSettingsWindow, aboutWindow;
    juce::String lastAudioRestoreFrom, lastRateWarning;
    std::unique_ptr<juce::FileChooser> mixdownChooser;
};
