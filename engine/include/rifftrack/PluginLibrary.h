#pragma once

#include "rifftrack/PluginScanner.h"

#include <juce_events/juce_events.h>

namespace rifftrack
{

/** The effects and instruments available to load, kept in a file so startup needn't rescan. */
class PluginLibrary final : private juce::Thread
{
public:
    using ScanProgress = std::function<void (const juce::String& message)>;
    using ScanFinished = std::function<void (const PluginScanResult&)>;
    using InstanceReady = std::function<void (std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error)>;

    explicit PluginLibrary (juce::File listFile);
    ~PluginLibrary() override;

    /** Mixers, panners, generators and instruments can't sit on a guitar track. */
    static bool isInsertEffect (const juce::PluginDescription&);

    const std::vector<juce::PluginDescription>& getPlugins() const noexcept { return plugins; }
    const std::vector<juce::PluginDescription>& getInstruments() const noexcept { return instruments; }
    bool hasBeenScanned() const { return file.existsAsFile(); }
    bool isScanning() const { return isThreadRunning(); }

    /** Both callbacks arrive on the message thread. */
    void startScan (PluginScanCommand, juce::File workDirectory, ScanProgress, ScanFinished);

    /** Loads without blocking the message thread, since amp sims can take seconds. */
    void createInstance (const juce::PluginDescription&, double sampleRate, int blockSize, InstanceReady);

private:
    void run() override;
    void setPlugins (const std::vector<juce::PluginDescription>&);

    juce::File file;
    juce::AudioPluginFormatManager formats;
    std::vector<juce::PluginDescription> plugins, instruments;

    PluginScanCommand scanCommand;
    juce::File scanDirectory;
    std::vector<PluginScanTarget> scanTargets;
    ScanProgress scanProgress;
    ScanFinished scanFinished;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PluginLibrary)
};

} // namespace rifftrack
