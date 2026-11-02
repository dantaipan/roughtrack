#include "MainComponent.h"

#include "rifftrack/AudioDevices.h"
#include "rifftrack/MicrophoneAccess.h"

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
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("RiffTrack");
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

MainComponent::MainComponent (const juce::File& sessionToOpen)
    : settings (std::make_unique<juce::PropertiesFile> (settingsOptions())),
      pluginLibrary (std::make_unique<rifftrack::PluginLibrary> (settings->getFile().getSiblingFile ("RiffTrack.plugins.xml"))),
      timeline (std::make_unique<TimelineView> (engine, stripHeight)),
      sessionWasGiven (sessionToOpen != juce::File())
{
    currentSession = sessionWasGiven ? sessionToOpen
                                     : sessionsFolder().getChildFile (settings->getValue ("lastSession", defaultSessionName));
    engine.setSessionDirectory (currentSession);
    engine.setDrumFolder (drumsFolder());
    timeline->setSessionDirectory (currentSession);

    timeline->onDrumEdit = [this] (int track, const rifftrack::DrumPartList& parts, const juce::String& name) { applyDrumEdit (track, parts, name); };
    timeline->onEdit = [this] (int track, const rifftrack::ClipList& clips, const juce::String& name) { applyClipEdit (track, clips, name); };
    timeline->onMessage = [this] (const juce::String& message) { showPluginMessage (message); };
    timeline->onSeek = [this] (juce::int64 position)
    {
        if (engine.getState() == rifftrack::TransportState::stopped)
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
        if (engine.getState() == rifftrack::TransportState::recording)
            return;

        for (int track = 0; track < rifftrack::maxTracks; ++track)
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

    audioSettingsButton.setTooltip ("Audio Settings");
    audioSettingsButton.onClick = [this] { showAudioSettings(); };
    calibrateButton.setTooltip ("Calibrate Latency");
    calibrateButton.onClick = [this] { confirmCalibration(); };

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
                                                                     &audioSettingsButton, &calibrateButton, &statusLabel,
                                                                     &takeWarningLabel, &pluginLabel, &sessionButton, &effectsButton, &undoButton, &redoButton,
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

    addTrackButton.onClick = [this] { showAddTrackMenu(); };
    addAndMakeVisible (addTrackButton);

    timeline->onScrollLanes = [this] (float wheelDelta)
    {
        trackScrollBar.setCurrentRangeStart (trackScrollBar.getCurrentRangeStart() - (double) wheelDelta * linesPerWheelStep);
    };

    // Before setSize, so the first layout is already the saved view.
    consoleButton.setToggleState (settings->getBoolValue ("consoleView"), juce::dontSendNotification);

    setSize (1400, transportHeight + TimelineView::rulerHeight + rowsShownAtStart * stripHeight + TimelineView::scrollBarHeight + 3 * statusHeight + 32);

    takeWarningLabel.setColour (juce::Label::textColourId, console::amber);
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
            (int) rifftrack::AuxParameter::count,
            [] (int i) -> const rifftrack::ParameterInfo& { return rifftrack::getParameterInfo ((rifftrack::AuxParameter) i); },
            [this] (int i) { return engine.getAux().getParameter ((rifftrack::AuxParameter) i); },
            [this] (int i, float value) { engine.getAux().setParameter ((rifftrack::AuxParameter) i, value); });

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
        const auto result = rifftrack::applySessionXml (engine, *xml, currentSession);

        if (result.failed())
            showPluginMessage (result.getErrorMessage());
    }

    if (engine.getTrackOrder().empty())
        engine.addTrack (nextTrackName ("Guitar"));

    for (const auto track : engine.getTrackOrder())
        createStrip (track);

    layoutTracks();

    tempoSlider.setValue (engine.getTempo(), juce::dontSendNotification);
    clickButton.setToggleState (engine.isClickEnabled(), juce::dontSendNotification);
    masterStrip.refreshFromEngine();
    refreshChannels();

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName ("RiffTrack - " + currentSession.getFileName());

    timeline->setSessionDirectory (currentSession);
    clipsChanged();
    restoreInserts();
}

// Nothing is saved until a session has loaded, or quitting early, such as with
// microphone access denied, would write an empty session over the real one.
void MainComponent::saveSession()
{
    if (! sessionLoaded || engine.getState() != rifftrack::TransportState::stopped)
        return;

    const auto text = rifftrack::createSessionXml (engine, currentSession)->toString();

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
                            else if (folders[result - firstSessionMenuId] != safeThis->currentSession)
                                safeThis->openSession (folders[result - firstSessionMenuId]);
                        });
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

void MainComponent::applyClipEdit (int track, const rifftrack::ClipList& clips, const juce::String& editName)
{
    if (engine.getState() != rifftrack::TransportState::stopped)
    {
        showPluginMessage ("Stop the transport to edit.");
        return;
    }

    undoManager.beginNewTransaction (editName);
    undoManager.perform (new rifftrack::ClipEdit (engine, track, engine.getClips (track), clips));
    clipsChanged();
}

void MainComponent::applyDrumEdit (int track, const rifftrack::DrumPartList& parts, const juce::String& editName)
{
    if (engine.getState() != rifftrack::TransportState::stopped)
    {
        showPluginMessage ("Stop the transport to edit.");
        return;
    }

    undoManager.beginNewTransaction (editName);
    undoManager.perform (new rifftrack::DrumEdit (engine, track, engine.getDrumParts (track), parts));
    clipsChanged();
}

void MainComponent::recordingFinished()
{
    recordingInProgress = false;
    undoManager.beginNewTransaction ("Record");

    for (int track = 0; track < rifftrack::maxTracks; ++track)
        if (engine.getClips (track) != clipsBeforeRecording[(size_t) track])
            undoManager.perform (new rifftrack::ClipEdit (engine, track, clipsBeforeRecording[(size_t) track], engine.getClips (track)));

    clipsChanged();
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
    if (engine.getState() != rifftrack::TransportState::stopped)
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

    if (key == juce::KeyPress ('x'))
    {
        showConsole (! consoleButton.getToggleState());
        return true;
    }

    if (key == juce::KeyPress (juce::KeyPress::spaceKey))
    {
        if (engine.getState() == rifftrack::TransportState::stopped)
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

    const auto command = [executable] (const rifftrack::PluginScanTarget& target, const juce::File& resultFile)
    {
        return juce::StringArray { executable, scanChildArgument, target.format, target.identifier, resultFile.getFullPathName() };
    };

    const auto workDirectory = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("RiffTrackScan");

    const auto safeThis = juce::Component::SafePointer (this);

    pluginLibrary->startScan (command,
                              workDirectory,
                              [safeThis] (const juce::String& message)
                              {
                                  if (safeThis != nullptr)
                                      safeThis->showPluginMessage (message);
                              },
                              [safeThis] (const rifftrack::PluginScanResult& result)
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

void MainComponent::showPluginMessage (const juce::String& message)
{
    pluginLabel.setText (message, juce::dontSendNotification);
}

bool MainComponent::canChangeInserts()
{
    if (engine.getState() == rifftrack::TransportState::stopped)
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
    const auto isInstrument = slot == rifftrack::instrumentSlot;
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
                                rifftrack::SavedInsert insert;
                                insert.track = track;
                                insert.slot = slot;
                                insert.description = (slot == rifftrack::instrumentSlot ? safeThis->pluginLibrary->getInstruments()
                                                                                         : safeThis->pluginLibrary->getPlugins())[(size_t) (result - firstPluginMenuId)];

                                if (safeThis->canChangeInserts())
                                    safeThis->loadInsert (track, slot, insert);
                            }
                        });
}

void MainComponent::loadInsert (int track, int slot, const rifftrack::SavedInsert& insert)
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

    const auto inserts = rifftrack::parseSavedInserts (*xml);
    numInsertsRestoring = (int) inserts.size();

    // A track holding only a plugin wasn't saved as in use by older sessions.
    for (const auto& insert : inserts)
    {
        if (! engine.isTrackActive (insert.track))
        {
            const auto isLegacyTrack = insert.track < (int) rifftrack::legacyTrackNames.size();
            engine.activateTrack (insert.track, isLegacyTrack ? rifftrack::legacyTrackNames[(size_t) insert.track] : "Audio");
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
        || engine.getState() != rifftrack::TransportState::stopped)
        return;

    const rifftrack::AudioDeviceChoice chosen { chosenXml->getStringAttribute ("audioOutputDeviceName").toStdString(),
                                                chosenXml->getDoubleAttribute ("audioDeviceRate"),
                                                chosenXml->getIntAttribute ("audioDeviceBufferSize") };

    const auto setup = deviceManager.getAudioDeviceSetup();
    const rifftrack::AudioDeviceChoice current { setup.outputDeviceName.toStdString(),
                                                 device != nullptr ? device->getCurrentSampleRate() : 0.0,
                                                 device != nullptr ? device->getCurrentBufferSizeSamples() : 0 };

    std::vector<std::string> outputs;

    for (const auto& name : type->getDeviceNames (false))
        outputs.push_back (name.toStdString());

    if (! rifftrack::shouldRestoreAudioDevice (chosen, current, outputs))
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
    if (recordingInProgress && engine.getState() == rifftrack::TransportState::stopped)
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
        if (engine.getTrackType (candidate) != rifftrack::TrackType::audio)
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

void MainComponent::updateStatus()
{
    const auto sampleRate = engine.getSampleRate();
    const auto seconds = sampleRate > 0.0 ? (double) engine.getPosition() / sampleRate : 0.0;

    const auto beats = (juce::int64) std::floor (seconds * engine.getTempo() / 60.0);
    const auto wholeMinutes = (int) (seconds / 60.0);

    positionLabel.setText (juce::String (beats / rifftrack::beatsPerBar + 1).paddedLeft (' ', 3) + "." + juce::String (beats % rifftrack::beatsPerBar + 1) + "  "
                               + juce::String (wholeMinutes) + ":" + juce::String (seconds - wholeMinutes * 60.0, 2).paddedLeft ('0', 5),
                           juce::dontSendNotification);

    const auto state = engine.getState();
    const auto idleColour = getLookAndFeel().findColour (juce::TextButton::buttonColourId);

    recordButton.setColour (juce::TextButton::buttonColourId, state == rifftrack::TransportState::recording ? console::red : idleColour);
    playButton.setColour (juce::TextButton::buttonColourId, state == rifftrack::TransportState::playing ? console::green.darker (0.3f) : idleColour);

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
    auto area = getLocalBounds().reduced (12);

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

    for (auto* button : { &returnButton, &playButton, &stopButton, &recordButton })
        place (*button, transportButtonWidth);

    place (positionLabel, positionWidth);
    endGroup();

    place (tempoSlider, tempoWidth);
    place (clickButton, transportButtonWidth);
    endGroup();

    for (auto* button : { &addTrackButton, &consoleButton, &tunerButton, &effectsButton })
        place (*button, transportButtonWidth);

    calibrateButton.setBounds (toolbar.removeFromRight (transportButtonWidth));
    toolbar.removeFromRight (toolbarGap);
    audioSettingsButton.setBounds (toolbar.removeFromRight (transportButtonWidth));
    toolbar.removeFromRight (toolbarGroupGap / 2);
    toolbarDividers.push_back (toolbar.removeFromRight (1).reduced (0, 4));

    if (tuner.isVisible())
        tuner.setBounds (area.removeFromTop (tunerHeight));

    statusLabel.setBounds (area.removeFromBottom (statusHeight));
    pluginLabel.setBounds (area.removeFromBottom (statusHeight));
    takeWarningLabel.setBounds (area.removeFromBottom (statusHeight));

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

void MainComponent::layoutTracks()
{
    if (consoleButton.getToggleState())
    {
        layoutConsole();
        return;
    }

    consoleScrollBar.setVisible (false);

    const auto& order = engine.getTrackOrder();
    const auto totalHeight = (int) order.size() * stripHeight;

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

    timeline->setVerticalOffset (trackScrollOffset);
    timeline->refresh();
}

void MainComponent::layoutConsole()
{
    trackScrollBar.setVisible (false);

    const auto& order = engine.getTrackOrder();
    const auto totalWidth = (int) order.size() * channelWidth;

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

    // Guitars are numbered from the start, because there are usually several.
    if (baseName != "Guitar" && ! isTaken (baseName))
        return baseName;

    for (int number = baseName == "Guitar" ? 1 : 2;; ++number)
    {
        const auto name = baseName + " " + juce::String (number);

        if (! isTaken (name))
            return name;
    }
}

void MainComponent::showAddTrackMenu()
{
    const juce::StringArray names { "Guitar", "Bass", "Drums", "MIDI", "Vocal", "Audio" };

    juce::PopupMenu menu;

    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i]);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addTrackButton),
                        [safeThis = juce::Component::SafePointer (this), names] (int result)
                        {
                            if (safeThis != nullptr && result > 0)
                                safeThis->addTrack (names[result - 1]);
                        });
}

void MainComponent::addTrack (const juce::String& baseName)
{
    const auto type = baseName == "Drums" ? rifftrack::TrackType::drums
                                          : (baseName == "MIDI" ? rifftrack::TrackType::midi : rifftrack::TrackType::audio);

    if (type == rifftrack::TrackType::drums)
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
        showPluginMessage (engine.getState() == rifftrack::TransportState::stopped
                               ? "RiffTrack can have up to " + juce::String (rifftrack::maxTracks) + " tracks."
                               : "Stop the transport to add a track.");
        return;
    }

    createStrip (track);
    layoutTracks();
    saveSession();

    if (type == rifftrack::TrackType::midi)
        loadDefaultInstrument (track);
}

void MainComponent::loadDefaultInstrument (int track)
{
    for (const auto& instrument : pluginLibrary->getInstruments())
    {
        if (instrument.name == defaultInstrumentName)
        {
            rifftrack::SavedInsert insert;
            insert.track = track;
            insert.slot = rifftrack::instrumentSlot;
            insert.description = instrument;
            loadInsert (track, rifftrack::instrumentSlot, insert);
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
