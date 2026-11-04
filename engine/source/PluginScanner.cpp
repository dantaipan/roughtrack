#include "roughtrack/PluginScanner.h"

namespace roughtrack
{

namespace
{

constexpr auto listTag = "PLUGINS";

constexpr int pollMs = 20;

} // namespace

void addPluginFormats (juce::AudioPluginFormatManager& formats)
{
    juce::addDefaultFormatsToManager (formats);
}

std::vector<PluginScanTarget> findPluginScanTargets (juce::AudioPluginFormatManager& formats)
{
    std::vector<PluginScanTarget> targets;

    for (auto* format : formats.getFormats())
        for (const auto& identifier : format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false))
            targets.push_back ({ format->getName(), identifier });

    return targets;
}

PluginScanResult scanPlugins (const std::vector<PluginScanTarget>& targets,
                              const PluginScanCommand& command,
                              const juce::File& workDirectory,
                              int timeoutMs,
                              const PluginScanProgress& progress,
                              const std::function<bool()>& shouldStop)
{
    PluginScanResult result;
    workDirectory.createDirectory();

    const auto wantsToStop = [&shouldStop] { return shouldStop != nullptr && shouldStop(); };

    for (size_t i = 0; i < targets.size() && ! wantsToStop(); ++i)
    {
        const auto& target = targets[i];

        if (progress != nullptr)
            progress (target, (int) i, (int) targets.size());

        const auto resultFile = workDirectory.getNonexistentChildFile ("scan-result", ".xml", false);

        // Streams are left unread so a chatty plugin can't fill a pipe and stall.
        juce::ChildProcess child;

        if (! child.start (command (target, resultFile), 0))
        {
            result.failures.push_back ({ target, "The scanner couldn't be started." });
            continue;
        }

        auto waitedMs = 0;

        while (waitedMs < timeoutMs && ! wantsToStop() && ! child.waitForProcessToFinish (pollMs))
            waitedMs += pollMs;

        if (child.isRunning() && wantsToStop())
        {
            child.kill();
            resultFile.deleteFile();
            break;
        }

        if (child.isRunning())
        {
            child.kill();
            result.failures.push_back ({ target, "It didn't finish loading within "
                                                     + juce::String (timeoutMs / 1000)
                                                     + " seconds. If macOS asked for permission, allow it and rescan." });
            resultFile.deleteFile();
            continue;
        }

        // ChildProcess reports exit code 0 for a process killed by a signal, so
        // only the result file proves the plugin loaded.
        const auto plugins = readPluginList (resultFile);
        resultFile.deleteFile();

        if (plugins.empty())
            result.failures.push_back ({ target, "It crashed or failed while loading." });
        else
            result.plugins.insert (result.plugins.end(), plugins.begin(), plugins.end());
    }

    return result;
}

bool writePluginList (const std::vector<juce::PluginDescription>& plugins, const juce::File& file)
{
    juce::XmlElement xml (listTag);

    for (const auto& plugin : plugins)
        xml.addChildElement (plugin.createXml().release());

    return file.replaceWithText (xml.toString());
}

std::vector<juce::PluginDescription> readPluginList (const juce::File& file)
{
    std::vector<juce::PluginDescription> plugins;

    if (const auto xml = juce::parseXML (file))
    {
        for (auto* element : xml->getChildIterator())
        {
            juce::PluginDescription description;

            if (description.loadFromXml (*element))
                plugins.push_back (description);
        }
    }

    return plugins;
}

bool writePluginScanResult (juce::AudioPluginFormatManager& formats, const PluginScanTarget& target, const juce::File& resultFile)
{
    for (auto* format : formats.getFormats())
    {
        if (format->getName() != target.format)
            continue;

        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile (found, target.identifier);

        if (found.isEmpty())
            return false;

        std::vector<juce::PluginDescription> plugins;

        for (auto* description : found)
            plugins.push_back (*description);

        return writePluginList (plugins, resultFile);
    }

    return false;
}

} // namespace roughtrack
