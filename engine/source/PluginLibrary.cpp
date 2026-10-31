#include "rifftrack/PluginLibrary.h"

#include <algorithm>

namespace rifftrack
{

namespace
{

// Long enough for someone to answer a macOS privacy prompt, which a plugin
// can trigger the first time it reads its presets.
constexpr int scanTimeoutMs = 120000;
constexpr int stopTimeoutMs = 5000;
} // namespace

PluginLibrary::PluginLibrary (juce::File listFile)
    : juce::Thread ("RiffTrack plugin scan"), file (std::move (listFile))
{
    addPluginFormats (formats);
    setPlugins (readPluginList (file));
}

PluginLibrary::~PluginLibrary()
{
    stopThread (stopTimeoutMs);
}

bool PluginLibrary::isInsertEffect (const juce::PluginDescription& plugin)
{
    if (plugin.isInstrument)
        return false;

    for (const auto* category : { "Mixer", "Panner", "Generator", "Synth" })
        if (plugin.category.containsIgnoreCase (category))
            return false;

    return true;
}

void PluginLibrary::setPlugins (const std::vector<juce::PluginDescription>& newPlugins)
{
    plugins.clear();

    std::copy_if (newPlugins.begin(), newPlugins.end(), std::back_inserter (plugins), isInsertEffect);

    std::sort (plugins.begin(), plugins.end(), [] (const auto& a, const auto& b)
    {
        const auto manufacturer = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
        const auto name = a.name.compareIgnoreCase (b.name);

        if (manufacturer != 0)
            return manufacturer < 0;

        return name != 0 ? name < 0 : a.pluginFormatName < b.pluginFormatName;
    });
}

void PluginLibrary::startScan (PluginScanCommand command, juce::File workDirectory, ScanProgress progress, ScanFinished finished)
{
    if (isThreadRunning())
        return;

    scanCommand = std::move (command);
    scanDirectory = std::move (workDirectory);
    scanTargets = findPluginScanTargets (formats);
    scanProgress = std::move (progress);
    scanFinished = std::move (finished);

    startThread();
}

void PluginLibrary::run()
{
    auto scanned = scanPlugins (scanTargets,
                                scanCommand,
                                scanDirectory,
                                scanTimeoutMs,
                                [this] (const PluginScanTarget&, int numFinished, int numTargets)
                                {
                                    const auto message = "Scanning plugins: " + juce::String (numFinished) + " of " + juce::String (numTargets);

                                    juce::MessageManager::callAsync ([library = juce::WeakReference<PluginLibrary> (this), message]
                                                                     {
                                                                         if (library != nullptr)
                                                                             library->scanProgress (message);
                                                                     });
                                },
                                [this] { return threadShouldExit(); });

    if (threadShouldExit())
        return;

    juce::MessageManager::callAsync ([library = juce::WeakReference<PluginLibrary> (this), result = std::move (scanned)]
                                     {
                                         if (library == nullptr)
                                             return;

                                         writePluginList (result.plugins, library->file);
                                         library->setPlugins (result.plugins);
                                         library->scanFinished (result);
                                     });
}

void PluginLibrary::createInstance (const juce::PluginDescription& description, double sampleRate, int blockSize, InstanceReady ready)
{
    formats.createPluginInstanceAsync (description, sampleRate, blockSize, std::move (ready));
}

} // namespace rifftrack
