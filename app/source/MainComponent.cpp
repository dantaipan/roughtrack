#include "MainComponent.h"

#include "rifftrack/AudioDevices.h"
#include "rifftrack/MicrophoneAccess.h"

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
constexpr int stripWidth = 700;
constexpr double cpuWarningLevel = 0.8;
constexpr int effectsWidth = 420;
constexpr int firstSessionMenuId = 1;
constexpr int newSessionMenuId = 100000;
constexpr auto defaultSessionName = "Session";
constexpr auto sessionFileName = "session.xml";

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
    timeline->setSessionDirectory (currentSession);

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
        for (int track = 0; track < rifftrack::numTracks; ++track)
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
                                                                     &takeWarningLabel, &pluginLabel, &sessionButton, &effectsButton, &undoButton, &redoButton })
        addAndMakeVisible (component);

    for (int track = 0; track < rifftrack::numTracks; ++track)
    {
        strips.push_back (std::make_unique<TrackStrip> (engine, track));
        addAndMakeVisible (*strips.back());

        strips.back()->onInsertClick = [this, track] (int slot, bool wantsMenu, juce::Component& button)
        {
            insertClicked (track, slot, wantsMenu, button);
        };

        strips.back()->onChannelClick = [this, track] { openChannelWindow (track); };
    }

    setSize (1400, transportHeight + TimelineView::rulerHeight + rifftrack::numTracks * stripHeight + TimelineView::scrollBarHeight + 3 * statusHeight + 32);

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
    saveSession();
    saveInserts();
    closeChannelWindows();

    for (auto& track : pluginWindows)
        for (auto& window : track)
            window.reset();

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

    loadCurrentSession();

    if (! pluginLibrary->hasBeenScanned())
        startPluginScan();
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

        window = std::make_unique<PanelWindow> (juce::String (rifftrack::trackNames[(size_t) track]) + " Channel", panel);
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
    for (int track = 0; track < rifftrack::numTracks; ++track)
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

    for (auto& strip : strips)
        strip->refreshFromEngine();

    tempoSlider.setValue (engine.getTempo(), juce::dontSendNotification);
    clickButton.setToggleState (engine.isClickEnabled(), juce::dontSendNotification);
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

    for (int track = 0; track < rifftrack::numTracks; ++track)
    {
        for (int slot = 0; slot < rifftrack::numInsertSlots; ++slot)
        {
            pluginWindows[(size_t) track][(size_t) slot].reset();
            engine.removeInsert (track, slot);
            refreshInsert (track, slot);
        }

        engine.setClips (track, {});
        engine.setTrackSettings (track, {});
        engine.setTrackArmed (track, false);
        strips[(size_t) track]->clearArm();
    }

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

void MainComponent::recordingFinished()
{
    recordingInProgress = false;
    undoManager.beginNewTransaction ("Record");

    for (int track = 0; track < rifftrack::numTracks; ++track)
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
    const auto& plugins = pluginLibrary->getPlugins();
    const auto loaded = engine.getInsert (track, slot) != nullptr;

    juce::PopupMenu menu;

    if (loaded)
    {
        menu.addItem (openMenuId, "Open");
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
                                insert.description = safeThis->pluginLibrary->getPlugins()[(size_t) (result - firstPluginMenuId)];

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

    for (const auto& insert : inserts)
        loadInsert (insert.track, insert.slot, insert);
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
    if (recordingInProgress && engine.getState() == rifftrack::TransportState::stopped)
        recordingFinished();

    timeline->updatePlayhead();

    if (--autosaveCountdown <= 0)
    {
        autosaveCountdown = autosaveTicks;
        saveSession();
    }

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
           << "dropouts " << stats.dropouts << "  |  disk " << stats.playbackUnderrunSamples + stats.droppedRecordingSamples
           << "  |  CPU " << juce::roundToInt (deviceManager.getCpuUsage() * 100.0) << "%";

    statusLabel.setColour (juce::Label::textColourId, deviceManager.getCpuUsage() > cpuWarningLevel ? juce::Colours::orange : juce::Colours::white);
    statusLabel.setText (status, juce::dontSendNotification);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto transport = area.removeFromTop (transportHeight).reduced (0, 4);

    for (auto* button : { &returnButton, &playButton, &recordButton, &stopButton, &clickButton, &sessionButton, &effectsButton, &undoButton, &redoButton })
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
    pluginLabel.setBounds (area.removeFromBottom (statusHeight));
    takeWarningLabel.setBounds (area.removeFromBottom (statusHeight));

    auto stripArea = area.removeFromLeft (stripWidth);
    area.removeFromLeft (8);
    timeline->setBounds (area);

    stripArea.removeFromTop (TimelineView::rulerHeight);

    for (auto& strip : strips)
        strip->setBounds (stripArea.removeFromTop (stripHeight));
}
