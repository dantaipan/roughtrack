#include "MainComponent.h"

#include "roughtrack/AudioDevices.h"
#include "roughtrack/MicrophoneAccess.h"

#include "ConsoleLookAndFeel.h"

namespace
{

constexpr double preferredSampleRate = 48000.0;
constexpr int preferredBufferSize = 64;
constexpr int stripHeight = 76;
constexpr int transportHeight = 40;
constexpr int statusHeight = 28;
constexpr int timerHz = 30;

constexpr int firstPluginMenuId = 1;
constexpr int rescanMenuId = 100000;
constexpr int openMenuId = 100001;
constexpr int bypassMenuId = 100002;
constexpr int removeMenuId = 100003;

constexpr int autosaveTicks = 2 * timerHz;
constexpr int stripWidth = 300;
constexpr int channelWidth = 104;
constexpr int masterWidth = 124;
constexpr int tunerHeight = 72;
constexpr int transportButtonWidth = 66;
constexpr int toolbarGap = 4;
constexpr int toolbarGroupGap = 20;
constexpr int tempoWidth = 140;
constexpr int positionWidth = 160;
constexpr int trackScrollBarWidth = 12;
constexpr int linesPerWheelStep = 300;
constexpr int rowsShownAtStart = 7;
constexpr double cpuWarningLevel = 0.8;
constexpr int effectsWidth = 420;
constexpr int firstSessionMenuId = 1;
constexpr int newSessionMenuId = 100000;
constexpr int mixDownMenuId = 100001;
constexpr int stemsMenuId = 100002;
constexpr int audioSettingsMenuId = 1;
constexpr int calibrateMenuId = 2;
constexpr int aboutMenuId = 3;
constexpr int transportLabelledWidth = 82;
constexpr int transportIconWidth = 40;
constexpr int windowMargin = 12;
constexpr int messageShowMs = 6000;
constexpr auto defaultSessionName = "Session";
constexpr auto sessionFileName = "session.xml";

// Ships with macOS, so a MIDI track can make a sound with nothing else installed.
constexpr auto defaultInstrumentName = "DLSMusicDevice";

juce::File drumsFolder()
{
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile).getChildFile ("Contents/Resources/Drums");
}

juce::File sessionsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("RoughTrack");
}

/** Plugins grouped by maker, with ids that index the library's list. */
juce::PopupMenu createPluginMenu (const std::vector<juce::PluginDescription>& plugins)
{
    juce::PopupMenu menu;
    juce::PopupMenu maker;

    for (size_t i = 0; i < plugins.size(); ++i)
    {
        const auto& plugin = plugins[i];

        maker.addItem (firstPluginMenuId + (int) i, plugin.name + " (" + (plugin.pluginFormatName == "AudioUnit" ? "AU" : plugin.pluginFormatName) + ")");

        if (i + 1 == plugins.size() || plugins[i + 1].manufacturerName != plugin.manufacturerName)
        {
            menu.addSubMenu (plugin.manufacturerName.isNotEmpty() ? plugin.manufacturerName : juce::String ("Unknown"), maker);
            maker = {};
        }
    }

    return menu;
}

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "RoughTrack";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

/** The app was called RiffTrack until 0.7.0. Moves its sessions folder, settings and plugin list
    to the new names, once, so nothing is lost. Anything already under the new name is left alone. */
void moveFilesFromOldName()
{
    const auto moveIfNew = [] (const juce::File& from, const juce::File& to)
    {
        if (from.exists() && ! to.exists())
            from.moveFileTo (to);
    };

    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    moveIfNew (music.getChildFile ("RiffTrack"), sessionsFolder());

    const auto settingsFile = settingsOptions().getDefaultFile();
    moveIfNew (settingsFile.getSiblingFile ("RiffTrack.settings"), settingsFile);
    moveIfNew (settingsFile.getSiblingFile ("RiffTrack.plugins.xml"), settingsFile.getSiblingFile ("RoughTrack.plugins.xml"));
}

std::unique_ptr<juce::PropertiesFile> openSettings()
{
    moveFilesFromOldName();
    return std::make_unique<juce::PropertiesFile> (settingsOptions());
}

} // namespace

juce::String describeAudioDevices()
{
    juce::String text;

    for (const auto& device : roughtrack::listAudioDevices())
    {
        text << device.name << " [" << device.typeName << "]\n"
             << "  " << device.inputChannels << " in, " << device.outputChannels << " out\n"
             << "  " << device.currentSampleRate << " Hz, " << device.currentBufferSize << " samples ("
             << juce::String (roughtrack::bufferLatencyMs (device.currentBufferSize, device.currentSampleRate), 2)
             << " ms per buffer)\n\n";
    }

    return text.isEmpty() ? "No audio devices found.\n" : text;
}

juce::String describeMicrophoneAccess()
{
    switch (roughtrack::getMicrophoneAccess())
    {
        case roughtrack::MicrophoneAccess::granted:       return "Microphone access: granted\n";
        case roughtrack::MicrophoneAccess::denied:        return "Microphone access: denied\n";
        case roughtrack::MicrophoneAccess::restricted:    return "Microphone access: restricted\n";
        case roughtrack::MicrophoneAccess::notDetermined: return "Microphone access: not yet requested\n";
    }

    return {};
}

MainComponent::MainComponent (const juce::File& sessionToOpen)
    : settings (openSettings()),
      pluginLibrary (std::make_unique<roughtrack::PluginLibrary> (settings->getFile().getSiblingFile ("RoughTrack.plugins.xml"))),
      timeline (std::make_unique<TimelineView> (engine, stripHeight)),
      sessionWasGiven (sessionToOpen != juce::File())
{
    currentSession = sessionWasGiven ? sessionToOpen
                                     : sessionsFolder().getChildFile (settings->getValue ("lastSession", defaultSessionName));
    engine.setSessionDirectory (currentSession);
    engine.setDrumFolder (drumsFolder());
    timeline->setSessionDirectory (currentSession);

    timeline->onDrumEdit = [this] (int track, const roughtrack::DrumPartList& parts, const juce::String& name) { applyDrumEdit (track, parts, name); };
    timeline->onEdit = [this] (int track, const roughtrack::ClipList& clips, const juce::String& name) { applyClipEdit (track, clips, name); };
    timeline->onMessage = [this] (const juce::String& message) { showPluginMessage (message); };
    timeline->onSeek = [this] (juce::int64 position)
    {
        if (engine.getState() == roughtrack::TransportState::stopped)
            engine.setPosition (position);
        else
            showPluginMessage ("Stop the transport to move the playhead.");
    };
    addAndMakeVisible (*timeline);

    setWantsKeyboardFocus (true);
    undoButton.onClick = [this] { undoOrRedo (false); };
    redoButton.onClick = [this] { undoOrRedo (true); };
    sessionButton.onClick = [this] { showSessionMenu(); };
    effectsButton.onClick = [this] { openEffectsWindow(); };

    returnButton.onClick = [this]
    {
        engine.stop();
        engine.setPosition (0);
    };

    playButton.onClick = [this] { engine.play(); };
    stopButton.onClick = [this] { engine.stop(); };

    recordButton.onClick = [this]
    {
        if (engine.getState() == roughtrack::TransportState::recording)
            return;

        for (int track = 0; track < roughtrack::maxTracks; ++track)
            clipsBeforeRecording[(size_t) track] = engine.getClips (track);

        const auto result = engine.record();
        recordingInProgress = result.wasOk();

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


    tunerButton.setClickingTogglesState (true);
    tunerButton.setColour (juce::TextButton::buttonOnColourId, console::green);
    tunerButton.setTooltip ("Reads the armed track's input, or the first audio track's");
    tunerButton.onClick = [this] { showTuner (tunerButton.getToggleState()); };

    consoleButton.setClickingTogglesState (true);
    consoleButton.setColour (juce::TextButton::buttonOnColourId, console::amber);
    consoleButton.setTooltip ("Switch between the timeline and the full console (X)");
    consoleButton.onClick = [this] { showConsole (consoleButton.getToggleState()); };

    tempoSlider.setSliderStyle (juce::Slider::IncDecButtons);
    tempoSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 24);
    tempoSlider.setRange (30.0, 300.0, 1.0);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setValue (engine.getTempo(), juce::dontSendNotification);
    tempoSlider.onValueChange = [this]
    {
        engine.setTempo (tempoSlider.getValue());
        timeline->refresh();
    };

    for (auto* component : std::initializer_list<juce::Component*> { &returnButton, &playButton, &recordButton, &stopButton,
                                                                     &clickButton, &tempoSlider, &positionLabel,
                                                                     &statusLabel,
                                                                     &messageButton, &sessionButton, &effectsButton, &undoButton, &redoButton,
                                                                     &tunerButton, &consoleButton })
        addAndMakeVisible (component);

    positionLabel.setColour (juce::Label::backgroundColourId, console::recess);
    positionLabel.setColour (juce::Label::textColourId, console::amber);
    positionLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::plain));
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setTooltip ("Bar and beat, then minutes and seconds");

    addAndMakeVisible (stripHolder);
    stripHolder.setInterceptsMouseClicks (false, true);

    addChildComponent (masterStrip);
    addChildComponent (tuner);

    trackScrollBar.setAutoHide (false);
    trackScrollBar.addListener (this);
    addAndMakeVisible (trackScrollBar);

    consoleScrollBar.setAutoHide (false);
    consoleScrollBar.addListener (this);
    addChildComponent (consoleScrollBar);

    ghostTrack.onClick = [this] (juce::Point<int> screenPosition) { showAddTrackMenu (screenPosition); };
    stripHolder.addAndMakeVisible (ghostTrack);

    dropIndicator.setColour (juce::Label::backgroundColourId, console::amber);
    dropIndicator.setInterceptsMouseClicks (false, false);
    stripHolder.addChildComponent (dropIndicator);

   #if JUCE_MAC
    juce::PopupMenu appleMenu;
    appleMenu.addItem (aboutMenuId, "About RoughTrack");
    juce::MenuBarModel::setMacMainMenu (this, &appleMenu);
   #endif

    timeline->onScrollLanes = [this] (float wheelDelta)
    {
        trackScrollBar.setCurrentRangeStart (trackScrollBar.getCurrentRangeStart() - (double) wheelDelta * linesPerWheelStep);
    };

    // Before setSize, so the first layout is already the saved view.
    consoleButton.setToggleState (settings->getBoolValue ("consoleView"), juce::dontSendNotification);

    setSize (1400, transportHeight + TimelineView::rulerHeight + rowsShownAtStart * stripHeight + TimelineView::scrollBarHeight + statusHeight + 32);

    statusLabel.setText ("Waiting for microphone access", juce::dontSendNotification);

    roughtrack::requestMicrophoneAccess ([safeThis = juce::Component::SafePointer (this)] (bool granted)
                                        {
                                            juce::MessageManager::callAsync ([safeThis, granted]
                                                                             {
                                                                                 if (safeThis == nullptr)
                                                                                     return;

                                                                                 if (granted)
                                                                                     safeThis->startAudio();
                                                                                 else
                                                                                     safeThis->statusLabel.setText ("Microphone access was denied. Allow RoughTrack in System Settings > Privacy & Security > Microphone.",
                                                                                                                    juce::dontSendNotification);
                                                                             });
                                        });
}

MainComponent::~MainComponent()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif

    stopTimer();
    engine.stop();
    saveSession();
    saveInserts();
    closeChannelWindows();

    for (auto& track : pluginWindows)
        for (auto& window : track)
            window.reset();

    midiDevices = {};
    deviceManager.removeMidiInputDeviceCallback ({}, &engine);
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

    // Keyboards can come and go while the app runs.
    deviceManager.addMidiInputDeviceCallback ({}, &engine);
    midiDevices = juce::MidiDeviceListConnection::make ([this] { enableMidiInputs(); });
    enableMidiInputs();
    deviceChanged();
    startTimerHz (timerHz);

    loadCurrentSession();

    if (! pluginLibrary->hasBeenScanned())
        startPluginScan();
}

void MainComponent::enableMidiInputs()
{
    juce::StringArray names;

    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        deviceManager.setMidiInputDeviceEnabled (device.identifier, true);
        names.add (device.name);
    }

    midiInputNames = names.joinIntoString (", ");
}

void MainComponent::openChannelWindow (int track)
{
    auto& window = channelWindows[(size_t) track];

    if (window == nullptr)
    {
        const auto folder = juce::File (settings->getValue ("ampFolder",
                                                            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getFullPathName()));

        auto* panel = new ChannelPanel (engine, track, folder);

        panel->onLoaded = [this] (const juce::File& newFolder)
        {
            settings->setValue ("ampFolder", newFolder.getFullPathName());
            refreshChannels();
            saveSession();
        };

        window = std::make_unique<PanelWindow> (engine.getTrackName (track) + " Channel", panel);
    }

    window->setVisible (true);
    window->toFront (true);
}

juce::Component* MainComponent::showChannelWindow (int track)
{
    openChannelWindow (track);
    return channelWindows[(size_t) track].get();
}

void MainComponent::openEffectsWindow()
{
    if (effectsWindow == nullptr)
    {
        auto* panel = new ParameterPanel (
            (int) roughtrack::AuxParameter::count,
            [] (int i) -> const roughtrack::ParameterInfo& { return roughtrack::getParameterInfo ((roughtrack::AuxParameter) i); },
            [this] (int i) { return engine.getAux().getParameter ((roughtrack::AuxParameter) i); },
            [this] (int i, float value) { engine.getAux().setParameter ((roughtrack::AuxParameter) i, value); });

        panel->setSize (effectsWidth, panel->getIdealHeight());
        effectsWindow = std::make_unique<PanelWindow> ("Effects", panel);
    }

    effectsWindow->setVisible (true);
    effectsWindow->toFront (true);
}

void MainComponent::closeChannelWindows()
{
    for (auto& window : channelWindows)
        window.reset();

    effectsWindow.reset();
}

void MainComponent::refreshChannels()
{
    for (const auto track : engine.getTrackOrder())
        strips[(size_t) track]->showChannelActive (engine.getChannel (track).hasAmp());
}

void MainComponent::loadCurrentSession()
{
    const auto sessionFile = currentSession.getChildFile (sessionFileName);
    const auto xml = juce::parseXML (sessionFile);

    // Autosave would replace a damaged file, so it's kept for recovery.
    if (sessionFile.existsAsFile() && xml == nullptr)
    {
        sessionFile.copyFileTo (sessionFile.withFileExtension ("damaged.xml"));
        showPluginMessage ("The session file couldn't be read. A copy was kept beside it.");
    }

    sessionLoaded = true;

    if (xml != nullptr)
    {
        const auto result = roughtrack::applySessionXml (engine, *xml, currentSession);

        if (result.failed())
            showPluginMessage (result.getErrorMessage());
    }

    if (engine.getTrackOrder().empty())
        engine.addTrack (nextTrackName ("Audio"));

    for (const auto track : engine.getTrackOrder())
        createStrip (track);

    layoutTracks();

    tempoSlider.setValue (engine.getTempo(), juce::dontSendNotification);
    clickButton.setToggleState (engine.isClickEnabled(), juce::dontSendNotification);
    masterStrip.refreshFromEngine();
    refreshChannels();

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName ("RoughTrack - " + currentSession.getFileName());

    timeline->setSessionDirectory (currentSession);
    clipsChanged();
    restoreInserts();
}

// Nothing is saved until a session has loaded, or quitting early, such as with
// microphone access denied, would write an empty session over the real one.
void MainComponent::saveSession()
{
    if (! sessionLoaded || engine.getState() != roughtrack::TransportState::stopped)
        return;

    const auto text = roughtrack::createSessionXml (engine, currentSession)->toString();

    if (text == lastSavedSession)
        return;

    currentSession.createDirectory();

    if (currentSession.getChildFile (sessionFileName).replaceWithText (text))
        lastSavedSession = text;
    else
        showPluginMessage ("Couldn't save the session to " + currentSession.getFullPathName());
}

void MainComponent::openSession (const juce::File& directory)
{
    if (numInsertsRestoring > 0)
    {
        showPluginMessage ("Plugins are still loading. Try again in a moment.");
        return;
    }

    engine.stop();
    saveSession();
    saveInserts();

    for (auto& track : pluginWindows)
        for (auto& window : track)
            window.reset();

    engine.removeAllTracks();

    for (auto& strip : strips)
        strip.reset();

    closeChannelWindows();
    engine.resetChannels();

    undoManager.clearUndoHistory();
    insertsNeedKeeping = false;
    sessionLoaded = false;
    lastSavedSession.clear();

    currentSession = directory;
    currentSession.createDirectory();
    engine.setSessionDirectory (currentSession);
    engine.setPosition (0);
    settings->setValue ("lastSession", currentSession.getFileName());

    loadCurrentSession();
    showPluginMessage ("Opened " + currentSession.getFileName() + ".");
}

void MainComponent::showSessionMenu()
{
    juce::PopupMenu menu;
    menu.addItem (newSessionMenuId, "New Session...");
    menu.addItem (mixDownMenuId, "Mix Down to WAV...");
    menu.addItem (stemsMenuId, "Export Stems...");
    menu.addSeparator();

    auto folders = sessionsFolder().findChildFiles (juce::File::findDirectories, false);
    folders.sort();

    for (int i = 0; i < folders.size(); ++i)
        menu.addItem (firstSessionMenuId + i, folders[i].getFileName(), true, folders[i] == currentSession);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sessionButton),
                        [safeThis = juce::Component::SafePointer (this), folders] (int result)
                        {
                            if (safeThis == nullptr || result == 0)
                                return;

                            if (result == newSessionMenuId)
                                safeThis->askForNewSession();
                            else if (result == mixDownMenuId)
                                safeThis->askWhereToMixDown();
                            else if (result == stemsMenuId)
                                safeThis->exportStems();
                            else if (folders[result - firstSessionMenuId] != safeThis->currentSession)
                                safeThis->openSession (folders[result - firstSessionMenuId]);
                        });
}

namespace
{

/** Runs a mixdown off the message thread, with a progress bar and a Cancel button. */
class MixdownThread final : public juce::ThreadWithProgressWindow
{
public:
    using Result = roughtrack::AudioEngine::BounceResult;
    using Job = std::function<Result (const std::function<bool (double)>&)>;
    using Finished = std::function<void (const Result&)>;

    MixdownThread (const juce::String& title, Job jobToRun, Finished whenFinished)
        : ThreadWithProgressWindow (title, true, true), job (std::move (jobToRun)), finished (std::move (whenFinished))
    {
    }

    void run() override
    {
        result = job ([this] (double progress)
        {
            setProgress (progress);
            return ! threadShouldExit();
        });
    }

    void threadComplete (bool) override
    {
        finished (result);
        delete this;
    }

private:
    Job job;
    Finished finished;
    Result result;
};

} // namespace

void MainComponent::askWhereToMixDown()
{
    if (engine.getState() != roughtrack::TransportState::stopped)
    {
        showPluginMessage ("Stop the transport to mix down.");
        return;
    }

    if (engine.getSessionLength() <= 0)
    {
        showPluginMessage ("There's nothing to mix down yet.");
        return;
    }

    const auto folder = currentSession.getChildFile ("Mixdowns");
    folder.createDirectory();

    mixdownChooser = std::make_unique<juce::FileChooser> ("Mix Down",
                                                          folder.getNonexistentChildFile (currentSession.getFileName(), ".wav", true),
                                                          "*.wav");

    mixdownChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                     | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [safeThis = juce::Component::SafePointer (this)] (const juce::FileChooser& chooser)
                                 {
                                     const auto file = chooser.getResult();

                                     if (safeThis != nullptr && file != juce::File())
                                         safeThis->mixDown (file.withFileExtension ("wav"));
                                 });
}

void MainComponent::mixDown (const juce::File& file)
{
    if (engine.getState() != roughtrack::TransportState::stopped)
        return;

    const auto end = engine.getSessionLength();

    auto* thread = new MixdownThread ("Mixing down " + file.getFileName(),
                                      [this, file, end] (const auto& shouldContinue) { return engine.bounce (file, 0, end, shouldContinue); },
                                      [safeThis = juce::Component::SafePointer (this), file] (const auto& result)
                                      {
                                          if (safeThis != nullptr)
                                              safeThis->mixdownFinished (file, result, false);
                                      });
    thread->launchThread();
}

void MainComponent::exportStems()
{
    if (engine.getState() != roughtrack::TransportState::stopped)
    {
        showPluginMessage ("Stop the transport to export stems.");
        return;
    }

    if (engine.getSessionLength() <= 0)
    {
        showPluginMessage ("There's nothing to export yet.");
        return;
    }

    const auto folder = currentSession.getChildFile ("Mixdowns").getNonexistentChildFile (currentSession.getFileName() + " stems", {}, true);
    const auto end = engine.getSessionLength();

    auto* thread = new MixdownThread ("Exporting stems",
                                      [this, folder, end] (const auto& shouldContinue) { return engine.bounceStems (folder, 0, end, shouldContinue); },
                                      [safeThis = juce::Component::SafePointer (this), folder] (const auto& result)
                                      {
                                          if (safeThis != nullptr)
                                              safeThis->mixdownFinished (folder, result, true);
                                      });
    thread->launchThread();
}

void MainComponent::mixdownFinished (const juce::File& file, const roughtrack::AudioEngine::BounceResult& result, bool isStems)
{
    if (result.result.failed())
    {
        showPluginMessage (result.result.getErrorMessage());
        return;
    }

    const auto seconds = (int) std::round ((double) result.numSamples / engine.getSampleRate());
    const auto peak = juce::Decibels::gainToDecibels (result.peak, -100.0f);
    const auto length = juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2);

    if (isStems)
        showPluginMessage ("Exported " + juce::String (file.getNumberOfChildFiles (juce::File::findFiles, "*.wav")) + " stems to "
                           + file.getFileName() + ", " + length + " long, the loudest peaking at " + juce::String (peak, 1) + " dBFS.");
    else
        showPluginMessage ("Mixed down to " + file.getFileName() + ", " + length + " long, peaking at " + juce::String (peak, 1) + " dBFS.");

    file.revealToUser();

    // A float mix can go over full scale, but the 24-bit file can't hold it.
    if (result.peak >= 1.0f)
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                                               .withTitle (isStems ? "A stem clips" : "The mix clips")
                                                               .withMessage ((isStems ? "The loudest stem peaks at +" : "The mix peaks at +") + juce::String (peak, 1)
                                                                             + " dBFS, over full scale, so the loudest parts are clipped. "
                                                                               "Lower the master fader by at least " + juce::String (std::ceil (peak * 10.0f) / 10.0f, 1)
                                                                             + " dB and mix down again.")
                                                               .withButton ("OK"),
                                      nullptr);
}

void MainComponent::askForNewSession()
{
    auto* window = new juce::AlertWindow ("New Session", "Name this session:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", {});
    window->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create ([safeThis = juce::Component::SafePointer (this), window] (int result)
                             {
                                 const auto name = juce::File::createLegalFileName (window->getTextEditorContents ("name").trim());

                                 if (safeThis == nullptr || result != 1 || name.isEmpty())
                                     return;

                                 const auto folder = sessionsFolder().getChildFile (name);

                                 if (folder.exists())
                                     safeThis->showPluginMessage ("There's already a session called " + name + ".");
                                 else
                                     safeThis->openSession (folder);
                             }),
                             true);
}

void MainComponent::applyClipEdit (int track, const roughtrack::ClipList& clips, const juce::String& editName)
{
    if (engine.getState() != roughtrack::TransportState::stopped)
    {
        showPluginMessage ("Stop the transport to edit.");
        return;
    }

    undoManager.beginNewTransaction (editName);
    undoManager.perform (new roughtrack::ClipEdit (engine, track, engine.getClips (track), clips));
    clipsChanged();
}

void MainComponent::applyDrumEdit (int track, const roughtrack::DrumPartList& parts, const juce::String& editName)
{
    if (engine.getState() != roughtrack::TransportState::stopped)
    {
        showPluginMessage ("Stop the transport to edit.");
        return;
    }

    undoManager.beginNewTransaction (editName);
    undoManager.perform (new roughtrack::DrumEdit (engine, track, engine.getDrumParts (track), parts));
    clipsChanged();
}

void MainComponent::recordingFinished()
{
    recordingInProgress = false;
    undoManager.beginNewTransaction ("Record");

    for (int track = 0; track < roughtrack::maxTracks; ++track)
        if (engine.getClips (track) != clipsBeforeRecording[(size_t) track])
            undoManager.perform (new roughtrack::ClipEdit (engine, track, clipsBeforeRecording[(size_t) track], engine.getClips (track)));

    clipsChanged();

    if (const auto health = engine.getLastTakeHealth(); ! health.isClean())
        showPluginMessage ("The last take has audio gaps (" + juce::String (health.dropouts) + " dropouts, "
                               + juce::String (health.droppedSamples) + " samples lost). Record it again if it matters.",
                           true);
}

void MainComponent::clipsChanged()
{
    timeline->refresh();
    undoButton.setEnabled (undoManager.canUndo());
    redoButton.setEnabled (undoManager.canRedo());
    undoButton.setTooltip (undoManager.getUndoDescription());
    redoButton.setTooltip (undoManager.getRedoDescription());
    saveSession();
}

void MainComponent::undoOrRedo (bool redo)
{
    if (engine.getState() != roughtrack::TransportState::stopped)
    {
        showPluginMessage ("Stop the transport to undo.");
        return;
    }

    if (redo)
        undoManager.redo();
    else
        undoManager.undo();

    clipsChanged();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto command = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress ('z', command, 0))
    {
        undoOrRedo (false);
        return true;
    }

    if (key == juce::KeyPress ('z', command | juce::ModifierKeys::shiftModifier, 0))
    {
        undoOrRedo (true);
        return true;
    }

    if (key == juce::KeyPress (',', command, 0))
    {
        showAudioSettings();
        return true;
    }

    if (key == juce::KeyPress ('x'))
    {
        showConsole (! consoleButton.getToggleState());
        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::spaceKey))
    {
        if (engine.getState() == roughtrack::TransportState::stopped)
            engine.play();
        else
            engine.stop();

        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::deleteKey) || key == juce::KeyPress (juce::KeyPress::backspaceKey))
        return timeline->deleteSelectedClip();

    return false;
}

juce::File MainComponent::insertsFile() const
{
    return currentSession.getChildFile ("inserts.xml");
}

void MainComponent::startPluginScan()
{
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();

    const auto command = [executable] (const roughtrack::PluginScanTarget& target, const juce::File& resultFile)
    {
        return juce::StringArray { executable, scanChildArgument, target.format, target.identifier, resultFile.getFullPathName() };
    };

    const auto workDirectory = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("RoughTrackScan");

    const auto safeThis = juce::Component::SafePointer (this);

    pluginLibrary->startScan (command,
                              workDirectory,
                              [safeThis] (const juce::String& message)
                              {
                                  if (safeThis != nullptr)
                                      safeThis->showPluginMessage (message);
                              },
                              [safeThis] (const roughtrack::PluginScanResult& result)
                              {
                                  if (safeThis == nullptr)
                                      return;

                                  juce::String message;
                                  message << (int) safeThis->pluginLibrary->getPlugins().size() << " effects available.";

                                  if (! result.failures.empty())
                                  {
                                      message << " " << (int) result.failures.size() << " failed: ";

                                      for (const auto& failure : result.failures)
                                          message << juce::File::createFileWithoutCheckingPath (failure.target.identifier).getFileName() << " (" << failure.reason << ") ";
                                  }

                                  safeThis->showPluginMessage (message);
                              });
}

// A message shows in the status line for a few seconds, and stays behind the message icon.
void MainComponent::showPluginMessage (const juce::String& message, bool isWarning)
{
    const auto warning = isWarning || message.startsWith ("Couldn't");

    messageButton.add (message, warning);
    latestMessage = message;
    latestMessageIsWarning = warning;
    showMessageUntil = juce::Time::getMillisecondCounter() + messageShowMs;
    updateStatus();
}

bool MainComponent::canChangeInserts()
{
    if (engine.getState() == roughtrack::TransportState::stopped)
        return true;

    showPluginMessage ("Stop the transport to change plugins.");
    return false;
}

void MainComponent::insertClicked (int track, int slot, bool wantsMenu, juce::Component& button)
{
    if (engine.getInsert (track, slot) != nullptr && ! wantsMenu)
        openPluginWindow (track, slot);
    else
        showInsertMenu (track, slot, button);
}

void MainComponent::showInsertMenu (int track, int slot, juce::Component& button)
{
    const auto isInstrument = slot == roughtrack::instrumentSlot;
    const auto& plugins = isInstrument ? pluginLibrary->getInstruments() : pluginLibrary->getPlugins();
    const auto loaded = engine.getInsert (track, slot) != nullptr;

    juce::PopupMenu menu;

    if (loaded)
    {
        menu.addItem (openMenuId, "Open");
        if (! isInstrument)
            menu.addItem (bypassMenuId, "Bypass", true, engine.isInsertBypassed (track, slot));

        menu.addItem (removeMenuId, "Remove");
        menu.addSeparator();
        menu.addSubMenu ("Replace With", createPluginMenu (plugins));
    }
    else
    {
        menu = createPluginMenu (plugins);
    }

    menu.addSeparator();
    menu.addItem (rescanMenuId, "Rescan Plugins", ! pluginLibrary->isScanning());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button),
                        [safeThis = juce::Component::SafePointer (this), track, slot] (int result)
                        {
                            if (safeThis == nullptr || result == 0)
                                return;

                            if (result == rescanMenuId)
                                safeThis->startPluginScan();
                            else if (result == openMenuId)
                                safeThis->openPluginWindow (track, slot);
                            else if (result == removeMenuId)
                                safeThis->removeInsert (track, slot);
                            else if (result == bypassMenuId)
                            {
                                safeThis->engine.setInsertBypassed (track, slot, ! safeThis->engine.isInsertBypassed (track, slot));
                                safeThis->refreshInsert (track, slot);
                                safeThis->saveInserts();
                            }
                            else
                            {
                                roughtrack::SavedInsert insert;
                                insert.track = track;
                                insert.slot = slot;
                                insert.description = (slot == roughtrack::instrumentSlot ? safeThis->pluginLibrary->getInstruments()
                                                                                         : safeThis->pluginLibrary->getPlugins())[(size_t) (result - firstPluginMenuId)];

                                if (safeThis->canChangeInserts())
                                    safeThis->loadInsert (track, slot, insert);
                            }
                        });
}

void MainComponent::loadInsert (int track, int slot, const roughtrack::SavedInsert& insert)
{
    showPluginMessage ("Loading " + insert.description.name + "...");

    const auto sampleRate = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : preferredSampleRate;
    auto* device = deviceManager.getCurrentAudioDevice();
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : preferredBufferSize;

    pluginLibrary->createInstance (insert.description,
                                   sampleRate,
                                   blockSize,
                                   [safeThis = juce::Component::SafePointer (this), track, slot, insert] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                                                                                          const juce::String& error)
                                   {
                                       if (safeThis == nullptr)
                                           return;

                                       auto& self = *safeThis;
                                       const auto wasRestoring = self.numInsertsRestoring > 0;
                                       auto failure = instance == nullptr ? (error.isNotEmpty() ? error : juce::String ("It didn't load."))
                                                                          : juce::String();

                                       if (failure.isEmpty() && ! self.canChangeInserts())
                                           failure = "Stop the transport to change plugins.";

                                       if (failure.isEmpty())
                                       {
                                           self.pluginWindows[(size_t) track][(size_t) slot].reset();

                                           const auto result = self.engine.setInsert (track, slot, std::move (instance), insert.state);
                                           failure = result.getErrorMessage();
                                       }

                                       if (failure.isEmpty())
                                       {
                                           self.engine.setInsertBypassed (track, slot, insert.bypassed);
                                           self.showPluginMessage (insert.description.name + " loaded, latency "
                                                                   + juce::String (self.engine.getInsert (track, slot)->getLatencySamples()) + " samples.");
                                       }
                                       else
                                       {
                                           self.insertsNeedKeeping = self.insertsNeedKeeping || wasRestoring;
                                           self.showPluginMessage ("Couldn't load " + insert.description.name + ": " + failure);
                                       }

                                       if (wasRestoring)
                                           --self.numInsertsRestoring;

                                       self.refreshInsert (track, slot);
                                       self.saveInserts();
                                   });
}

void MainComponent::removeInsert (int track, int slot)
{
    if (! canChangeInserts())
        return;

    pluginWindows[(size_t) track][(size_t) slot].reset();
    engine.removeInsert (track, slot);
    refreshInsert (track, slot);
    saveInserts();
}

void MainComponent::openPluginWindow (int track, int slot)
{
    auto* plugin = engine.getInsert (track, slot);

    if (plugin == nullptr)
        return;

    auto& window = pluginWindows[(size_t) track][(size_t) slot];

    if (window == nullptr)
        window = std::make_unique<PluginWindow> (*plugin, [this] { saveInserts(); });

    window->setVisible (true);
    window->toFront (true);
}

void MainComponent::refreshInsert (int track, int slot)
{
    auto* plugin = engine.getInsert (track, slot);

    if (strips[(size_t) track] == nullptr)
        return;

    strips[(size_t) track]->showInsert (slot,
                                        plugin != nullptr ? plugin->getName() : juce::String(),
                                        engine.isInsertBypassed (track, slot));
}

void MainComponent::restoreInserts()
{
    const auto xml = juce::parseXML (insertsFile());

    if (xml == nullptr)
        return;

    const auto inserts = roughtrack::parseSavedInserts (*xml);
    numInsertsRestoring = (int) inserts.size();

    // A track holding only a plugin wasn't saved as in use by older sessions.
    for (const auto& insert : inserts)
    {
        if (! engine.isTrackActive (insert.track))
        {
            const auto isLegacyTrack = insert.track < (int) roughtrack::legacyTrackNames.size();
            engine.activateTrack (insert.track, isLegacyTrack ? roughtrack::legacyTrackNames[(size_t) insert.track] : "Audio");
            createStrip (insert.track);
            layoutTracks();
        }

        loadInsert (insert.track, insert.slot, insert);
    }
}

// A plugin that's missing today might be back tomorrow, so its saved settings
// are kept rather than overwritten by a session that couldn't load it.
void MainComponent::saveInserts()
{
    if (! sessionLoaded || numInsertsRestoring > 0 || insertsNeedKeeping)
        return;

    insertsFile().getParentDirectory().createDirectory();

    if (const auto xml = engine.createInsertsXml())
        insertsFile().replaceWithText (xml->toString());
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

    const juce::String preferred (roughtrack::preferredInputDevice (setup.outputDeviceName.toStdString(),
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
    // Only a choice made in Audio Settings is remembered. Anything else, like a
    // fallback while the interface is unplugged, mustn't replace it.
    if (audioSettingsWindow != nullptr || ! settings->containsKey ("audioDevice"))
        if (const auto state = deviceManager.createStateXml())
            settings->setValue ("audioDevice", state.get());

    auto* device = deviceManager.getCurrentAudioDevice();
    numInputChannels = device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0;

    for (auto& strip : strips)
        if (strip != nullptr)
            strip->setInputChannels (numInputChannels);

    // Pitch is worked out from the sample rate, so a new rate means starting again.
    if (tuner.isVisible() && device != nullptr)
        tuner.start (device->getCurrentSampleRate());

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
    options.dialogBackgroundColour = console::panel;
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    audioSettingsWindow = options.launchAsync();
}

void MainComponent::restoreChosenAudioDevice()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    const auto chosenXml = settings->getXmlValue ("audioDevice");

    if (audioSettingsWindow != nullptr || calibrating || type == nullptr || chosenXml == nullptr
        || engine.getState() != roughtrack::TransportState::stopped)
        return;

    const roughtrack::AudioDeviceChoice chosen { chosenXml->getStringAttribute ("audioOutputDeviceName").toStdString(),
                                                chosenXml->getDoubleAttribute ("audioDeviceRate"),
                                                chosenXml->getIntAttribute ("audioDeviceBufferSize") };

    const auto setup = deviceManager.getAudioDeviceSetup();
    const roughtrack::AudioDeviceChoice current { setup.outputDeviceName.toStdString(),
                                                 device != nullptr ? device->getCurrentSampleRate() : 0.0,
                                                 device != nullptr ? device->getCurrentBufferSizeSamples() : 0 };

    std::vector<std::string> outputs;

    for (const auto& name : type->getDeviceNames (false))
        outputs.push_back (name.toStdString());

    if (! roughtrack::shouldRestoreAudioDevice (chosen, current, outputs))
    {
        lastAudioRestoreFrom = {};
        return;
    }

    // One try from each state, so a device that refuses the choice isn't restarted forever.
    const auto from = juce::String (current.outputDevice) + " " + juce::String (current.sampleRate) + " " + juce::String (current.bufferSize);

    if (from == lastAudioRestoreFrom)
        return;

    lastAudioRestoreFrom = from;

    auto restored = setup;
    restored.outputDeviceName = chosenXml->getStringAttribute ("audioOutputDeviceName");
    restored.inputDeviceName = chosenXml->getStringAttribute ("audioInputDeviceName");
    restored.sampleRate = chosen.sampleRate;
    restored.bufferSize = chosen.bufferSize;
    restored.useDefaultInputChannels = true;
    restored.useDefaultOutputChannels = true;

    const auto error = deviceManager.setAudioDeviceSetup (restored, false);

    showPluginMessage (error.isEmpty() ? "Back on " + restored.outputDeviceName + " at " + juce::String (chosen.bufferSize) + " samples."
                                       : "Couldn't return to " + restored.outputDeviceName + ": " + error);
}

void MainComponent::confirmCalibration()
{
    const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon)
                                                  .withTitle ("Calibrate Latency")
                                                  .withMessage ("Connect Output 1 to Input 1 with a cable and turn your monitors and headphones down. "
                                                                "RoughTrack will play three short test sweeps.")
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
                             ? "Round-trip latency: " + juce::String (roughtrack::bufferLatencyMs (result.latencySamples, sampleRate), 2)
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
    if (recordingInProgress && engine.getState() == roughtrack::TransportState::stopped)
        recordingFinished();

    timeline->updatePlayhead();

    if (--autosaveCountdown <= 0)
    {
        autosaveCountdown = autosaveTicks;
        saveSession();
        restoreChosenAudioDevice();
    }

    if (calibrating && (engine.isCalibrationFinished() || deviceManager.getCurrentAudioDevice() == nullptr))
        finishCalibration();

    for (auto& strip : strips)
        if (strip != nullptr)
            strip->updateMeter();

    masterStrip.updateMeter();

    if (tuner.isVisible())
        updateTuner();

    updateStatus();
}

// The string being tuned is the one about to be recorded, so the tuner follows Arm.
void MainComponent::updateTuner()
{
    auto track = -1;

    for (const auto candidate : engine.getTrackOrder())
    {
        if (engine.getTrackType (candidate) != roughtrack::TrackType::audio)
            continue;

        if (track < 0 || engine.isTrackArmed (candidate))
            track = candidate;

        if (engine.isTrackArmed (candidate))
            break;
    }

    const auto input = track >= 0 ? engine.getTrackSettings (track).firstInput : 0;

    engine.setTunerInput (input);
    tuner.setSource ((track >= 0 ? engine.getTrackName (track) + ", " : juce::String()) + "Input " + juce::String (input + 1));
    tuner.update (1.0 / timerHz);
}

void MainComponent::showTuner (bool shouldShow)
{
    tunerButton.setToggleState (shouldShow, juce::dontSendNotification);

    if (shouldShow)
    {
        tuner.start (engine.getSampleRate() > 0.0 ? engine.getSampleRate() : preferredSampleRate);
        updateTuner();
    }
    else
    {
        engine.setTunerInput (-1);
    }

    tuner.setVisible (shouldShow);
    resized();
}

void MainComponent::showConsole (bool shouldShow)
{
    consoleButton.setToggleState (shouldShow, juce::dontSendNotification);
    settings->setValue ("consoleView", shouldShow);
    resized();
}

void MainComponent::prepareForSnapshot()
{
    tooltips.hideTip();
    tooltips.setMillisecondsBeforeTipAppears (std::numeric_limits<int>::max());
    messageButton.markAllSeen();
    showMessageUntil = 0;
    updateStatus();
}

void MainComponent::updateStatus()
{
    const auto sampleRate = engine.getSampleRate();
    const auto seconds = sampleRate > 0.0 ? (double) engine.getPosition() / sampleRate : 0.0;

    const auto beats = (juce::int64) std::floor (seconds * engine.getTempo() / 60.0);
    const auto wholeMinutes = (int) (seconds / 60.0);

    positionLabel.setText (juce::String (beats / roughtrack::beatsPerBar + 1).paddedLeft (' ', 3) + "." + juce::String (beats % roughtrack::beatsPerBar + 1) + "  "
                               + juce::String (wholeMinutes) + ":" + juce::String (seconds - wholeMinutes * 60.0, 2).paddedLeft ('0', 5),
                           juce::dontSendNotification);

    const auto state = engine.getState();
    const auto idleColour = getLookAndFeel().findColour (juce::TextButton::buttonColourId);

    recordButton.setColour (juce::TextButton::buttonColourId, state == roughtrack::TransportState::recording ? console::red : idleColour);
    playButton.setColour (juce::TextButton::buttonColourId, state == roughtrack::TransportState::playing ? console::green.darker (0.3f) : idleColour);

    if (juce::Time::getMillisecondCounter() < showMessageUntil)
    {
        statusLabel.setColour (juce::Label::textColourId, latestMessageIsWarning ? console::amber : console::cream);
        statusLabel.setText (latestMessage, juce::dontSendNotification);
        return;
    }

    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        statusLabel.setColour (juce::Label::textColourId, console::dimText);
        statusLabel.setText ("No audio device", juce::dontSendNotification);
        return;
    }

    const auto stats = engine.getStats();
    const auto latency = roughtrack::bufferLatencyMs (engine.getRecordingLatency(), sampleRate);

    juce::String status;
    status << deviceDescription() << "  |  " << sampleRate << " Hz, " << device->getCurrentBufferSizeSamples() << " samples  |  "
           << "round trip " << juce::String (latency, 2) << " ms (" << (latencyIsCalibrated ? "calibrated" : "reported") << ")  |  "
           << "MIDI in: " << (midiInputNames.isNotEmpty() ? midiInputNames : juce::String ("none")) << "  |  "
           << "dropouts " << stats.dropouts << "  |  disk " << stats.playbackUnderrunSamples + stats.droppedRecordingSamples
           << "  |  CPU " << juce::roundToInt (deviceManager.getCpuUsage() * 100.0) << "%";

    statusLabel.setColour (juce::Label::textColourId, deviceManager.getCpuUsage() > cpuWarningLevel ? console::amber : console::dimText);
    statusLabel.setText (status, juce::dontSendNotification);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.setColour (console::outline);

    for (const auto& divider : toolbarDividers)
        g.fillRect (divider);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (windowMargin);

    auto toolbar = area.removeFromTop (transportHeight).reduced (0, 4);
    toolbarDividers.clear();

    const auto place = [&toolbar] (juce::Component& component, int width)
    {
        component.setBounds (toolbar.removeFromLeft (width));
        toolbar.removeFromLeft (toolbarGap);
    };

    // Groups are set apart by a wider gap with a faint line in the middle.
    const auto endGroup = [this, &toolbar]
    {
        toolbar.removeFromLeft (toolbarGroupGap / 2 - toolbarGap);
        toolbarDividers.push_back (toolbar.removeFromLeft (toolbarGroupGap / 2).withWidth (1).reduced (0, 4));
    };

    for (auto* button : { &sessionButton, &undoButton, &redoButton })
        place (*button, transportButtonWidth);

    endGroup();

    const auto iconsOnly = getWidth() < getMinimumWidth() + (transportLabelledWidth - transportIconWidth) * 4;

    for (auto* button : { &returnButton, &playButton, &stopButton, &recordButton })
    {
        button->setIconOnly (iconsOnly);
        place (*button, iconsOnly ? transportIconWidth : transportLabelledWidth);
    }

    place (positionLabel, positionWidth);
    endGroup();

    place (tempoSlider, tempoWidth);
    place (clickButton, transportButtonWidth);
    endGroup();

    for (auto* button : { &consoleButton, &tunerButton, &effectsButton })
        place (*button, transportButtonWidth);

    if (tuner.isVisible())
        tuner.setBounds (area.removeFromTop (tunerHeight));

    auto statusLine = area.removeFromBottom (statusHeight);
    messageButton.setBounds (statusLine.removeFromLeft (statusHeight).reduced (0, 1));
    statusLine.removeFromLeft (4);
    statusLabel.setBounds (statusLine);

    const auto consoleShowing = consoleButton.getToggleState();

    timeline->setVisible (! consoleShowing);
    masterStrip.setVisible (consoleShowing);

    if (consoleShowing)
    {
        auto scrollBarArea = area.removeFromBottom (trackScrollBarWidth);

        masterStrip.setBounds (area.removeFromRight (masterWidth));
        area.removeFromRight (6);
        consoleScrollBar.setBounds (scrollBarArea.withWidth (area.getWidth()));
        stripHolder.setBounds (area);

        layoutTracks();
        return;
    }

    auto stripArea = area.removeFromLeft (stripWidth);
    area.removeFromLeft (8);

    trackScrollBar.setBounds (area.removeFromRight (trackScrollBarWidth).withTrimmedTop (TimelineView::rulerHeight)
                                  .withTrimmedBottom (TimelineView::scrollBarHeight));
    timeline->setBounds (area);

    stripHolder.setBounds (stripArea.withTrimmedTop (TimelineView::rulerHeight).withTrimmedBottom (TimelineView::scrollBarHeight));

    layoutTracks();
}

int MainComponent::getMinimumWidth()
{
    constexpr auto groupGap = toolbarGroupGap - toolbarGap;
    constexpr auto session = 3 * (transportButtonWidth + toolbarGap) + groupGap;
    constexpr auto transport = 4 * (transportIconWidth + toolbarGap) + positionWidth + toolbarGap + groupGap;
    constexpr auto tempo = tempoWidth + transportButtonWidth + 2 * toolbarGap + groupGap;
    constexpr auto views = 3 * (transportButtonWidth + toolbarGap);

    return session + transport + tempo + views + 2 * windowMargin;
}

// Where a dragged track would land: the gap between rows nearest the pointer.
int MainComponent::dropPositionAt (float y) const
{
    const auto numTracks = (int) engine.getTrackOrder().size();
    return juce::jlimit (0, numTracks, juce::roundToInt ((y + (float) trackScrollOffset) / (float) stripHeight));
}

void MainComponent::showTrackDrag (float y)
{
    const auto position = dropPositionAt (y);
    dropIndicator.setBounds (0, position * stripHeight - trackScrollOffset - 1, stripHolder.getWidth(), 3);
    dropIndicator.setVisible (true);
    dropIndicator.toFront (false);
}

void MainComponent::finishTrackDrag (int track, float y)
{
    dropIndicator.setVisible (false);

    const auto& order = engine.getTrackOrder();
    const auto from = (int) std::distance (order.begin(), std::find (order.begin(), order.end(), track));
    const auto gap = dropPositionAt (y);

    // The track leaves its own place first, so a gap below it is one row higher.
    const auto to = gap > from ? gap - 1 : gap;

    if (to == from)
        return;

    engine.moveTrack (track, to);
    layoutTracks();
    saveSession();
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "Settings" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int, const juce::String&)
{
    juce::PopupMenu menu;
    menu.addItem (audioSettingsMenuId, "Audio Settings...");
    menu.addItem (calibrateMenuId, "Calibrate Latency...");
    return menu;
}

void MainComponent::menuItemSelected (int menuItemId, int)
{
    if (menuItemId == audioSettingsMenuId)
        showAudioSettings();
    else if (menuItemId == calibrateMenuId)
        confirmCalibration();
    else if (menuItemId == aboutMenuId)
        showAboutWindow (aboutWindow);
}

void MainComponent::layoutTracks()
{
    if (consoleButton.getToggleState())
    {
        layoutConsole();
        return;
    }

    consoleScrollBar.setVisible (false);

    const auto& order = engine.getTrackOrder();
    const auto totalHeight = (int) order.size() * stripHeight + stripHeight;

    trackScrollBar.setRangeLimits (0.0, juce::jmax (1, totalHeight));
    trackScrollBar.setCurrentRange (trackScrollBar.getCurrentRangeStart(), juce::jmax (1, stripHolder.getHeight()));
    trackScrollBar.setVisible (totalHeight > stripHolder.getHeight());
    trackScrollOffset = juce::roundToInt (trackScrollBar.getCurrentRangeStart());

    for (size_t lane = 0; lane < order.size(); ++lane)
    {
        auto& strip = *strips[(size_t) order[lane]];

        strip.setLayout (TrackStrip::Layout::row);
        strip.setBounds (0, (int) lane * stripHeight - trackScrollOffset, stripHolder.getWidth(), stripHeight);
    }

    ghostTrack.setBounds (0, (int) order.size() * stripHeight - trackScrollOffset, stripHolder.getWidth(), stripHeight);

    timeline->setVerticalOffset (trackScrollOffset);
    timeline->refresh();
}

void MainComponent::layoutConsole()
{
    trackScrollBar.setVisible (false);

    const auto& order = engine.getTrackOrder();
    const auto totalWidth = ((int) order.size() + 1) * channelWidth;

    consoleScrollBar.setRangeLimits (0.0, juce::jmax (1, totalWidth));
    consoleScrollBar.setCurrentRange (consoleScrollBar.getCurrentRangeStart(), juce::jmax (1, stripHolder.getWidth()));
    consoleScrollBar.setVisible (totalWidth > stripHolder.getWidth());

    const auto offset = juce::roundToInt (consoleScrollBar.getCurrentRangeStart());

    for (size_t channel = 0; channel < order.size(); ++channel)
    {
        auto& strip = *strips[(size_t) order[channel]];

        strip.setLayout (TrackStrip::Layout::channel);
        strip.setBounds ((int) channel * channelWidth - offset, 0, channelWidth, stripHolder.getHeight());
    }

    ghostTrack.setBounds ((int) order.size() * channelWidth - offset, 0, channelWidth, stripHolder.getHeight());
}

void MainComponent::scrollBarMoved (juce::ScrollBar*, double)
{
    layoutTracks();
}

void MainComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (consoleButton.getToggleState())
        consoleScrollBar.setCurrentRangeStart (consoleScrollBar.getCurrentRangeStart() - (double) (wheel.deltaX + wheel.deltaY) * linesPerWheelStep);
    else
        trackScrollBar.setCurrentRangeStart (trackScrollBar.getCurrentRangeStart() - (double) wheel.deltaY * linesPerWheelStep);
}

void MainComponent::createStrip (int track)
{
    auto& strip = strips[(size_t) track];

    if (strip == nullptr)
    {
        strip = std::make_unique<TrackStrip> (engine, track);
        stripHolder.addAndMakeVisible (*strip);

        strip->onDragMove = [this] (float y) { showTrackDrag (y); };
        strip->onDragEnd = [this, track] (float y) { finishTrackDrag (track, y); };

        strip->onInsertClick = [this, track] (int slot, bool wantsMenu, juce::Component& button)
        {
            insertClicked (track, slot, wantsMenu, button);
        };

        strip->onChannelClick = [this, track] { openChannelWindow (track); };
        strip->onDeleteRequest = [this, track] { confirmDeleteTrack (track); };
        strip->onRenamed = [this, track]
        {
            if (channelWindows[(size_t) track] != nullptr)
                channelWindows[(size_t) track]->setName (engine.getTrackName (track) + " Channel");

            saveSession();
        };

        strip->setInputChannels (numInputChannels);
    }

    strip->refreshFromEngine();
    strip->showChannelActive (engine.getChannel (track).hasAmp());
}

juce::String MainComponent::nextTrackName (const juce::String& baseName) const
{
    const auto isTaken = [this] (const juce::String& name)
    {
        for (const auto track : engine.getTrackOrder())
            if (engine.getTrackName (track) == name)
                return true;

        return false;
    };

    // Audio tracks are numbered from the start, because there are usually several.
    if (baseName != "Audio" && ! isTaken (baseName))
        return baseName;

    for (int number = baseName == "Audio" ? 1 : 2;; ++number)
    {
        const auto name = baseName + " " + juce::String (number);

        if (! isTaken (name))
            return name;
    }
}

void MainComponent::showAddTrackMenu (juce::Point<int> screenPosition)
{
    const juce::StringArray names { "Audio", "MIDI", "Drums" };

    juce::PopupMenu menu;

    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i]);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safeThis = juce::Component::SafePointer (this), names] (int result)
                        {
                            if (safeThis != nullptr && result > 0)
                                safeThis->addTrack (names[result - 1]);
                        });
}

void MainComponent::addTrack (const juce::String& baseName)
{
    const auto type = baseName == "Drums" ? roughtrack::TrackType::drums
                                          : (baseName == "MIDI" ? roughtrack::TrackType::midi : roughtrack::TrackType::audio);

    if (type == roughtrack::TrackType::drums)
    {
        const auto drums = engine.loadDrums();

        if (drums.failed())
        {
            showPluginMessage (drums.getErrorMessage());
            return;
        }
    }

    const auto track = engine.addTrack (nextTrackName (baseName), type);

    if (track < 0)
    {
        showPluginMessage (engine.getState() == roughtrack::TransportState::stopped
                               ? "RoughTrack can have up to " + juce::String (roughtrack::maxTracks) + " tracks."
                               : "Stop the transport to add a track.");
        return;
    }

    createStrip (track);
    layoutTracks();
    saveSession();

    if (type == roughtrack::TrackType::midi)
        loadDefaultInstrument (track);
}

void MainComponent::loadDefaultInstrument (int track)
{
    for (const auto& instrument : pluginLibrary->getInstruments())
    {
        if (instrument.name == defaultInstrumentName)
        {
            roughtrack::SavedInsert insert;
            insert.track = track;
            insert.slot = roughtrack::instrumentSlot;
            insert.description = instrument;
            loadInsert (track, roughtrack::instrumentSlot, insert);
            return;
        }
    }
}

void MainComponent::confirmDeleteTrack (int track)
{
    if (engine.getTrackOrder().size() <= 1)
    {
        showPluginMessage ("A session needs at least one track.");
        return;
    }

    if (! canChangeInserts())
        return;

    const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                                  .withTitle ("Delete Track")
                                                  .withMessage ("Delete " + engine.getTrackName (track) + " with its clips, amp and plugins? "
                                                                "This also clears undo history. Its recordings stay in the session folder, "
                                                                "so you can bring them back with Add Take at Playhead.")
                                                  .withButton ("Delete")
                                                  .withButton ("Cancel")
                                                  .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options,
                                  [safeThis = juce::Component::SafePointer (this), track] (int result)
                                  {
                                      if (safeThis != nullptr && result == 1)
                                          safeThis->deleteTrack (track);
                                  });
}

void MainComponent::deleteTrack (int track)
{
    if (! canChangeInserts())
        return;

    for (auto& window : pluginWindows[(size_t) track])
        window.reset();

    channelWindows[(size_t) track].reset();

    if (engine.removeTrack (track).failed())
        return;

    strips[(size_t) track].reset();
    undoManager.clearUndoHistory();

    layoutTracks();
    clipsChanged();
    saveInserts();
}
