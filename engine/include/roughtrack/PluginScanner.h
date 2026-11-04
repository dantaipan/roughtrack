#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>

namespace roughtrack
{

/** One plugin file or Audio Unit waiting to be scanned. */
struct PluginScanTarget
{
    juce::String format;
    juce::String identifier;
};

struct PluginScanFailure
{
    PluginScanTarget target;
    juce::String reason;
};

struct PluginScanResult
{
    std::vector<juce::PluginDescription> plugins;
    std::vector<PluginScanFailure> failures;
};

/** Builds the command that scans one target and writes its result to a file. */
using PluginScanCommand = std::function<juce::StringArray (const PluginScanTarget&, const juce::File& resultFile)>;

/** Called before each target with how many have been finished. */
using PluginScanProgress = std::function<void (const PluginScanTarget&, int numFinished, int numTargets)>;

void addPluginFormats (juce::AudioPluginFormatManager&);

/** Lists every VST3 and Audio Unit installed, without loading any of them. */
std::vector<PluginScanTarget> findPluginScanTargets (juce::AudioPluginFormatManager&);

/** Scans each target in its own process, so a plugin that crashes or hangs while
    loading is reported as a failure and can't take the app down with it.
*/
PluginScanResult scanPlugins (const std::vector<PluginScanTarget>& targets,
                              const PluginScanCommand& command,
                              const juce::File& workDirectory,
                              int timeoutMs,
                              const PluginScanProgress& progress = {},
                              const std::function<bool()>& shouldStop = {});

bool writePluginList (const std::vector<juce::PluginDescription>&, const juce::File&);
std::vector<juce::PluginDescription> readPluginList (const juce::File&);

/** The child's half of a scan. Loads one target in this process and writes what
    it found to resultFile, which is only created if the load worked.
*/
bool writePluginScanResult (juce::AudioPluginFormatManager&, const PluginScanTarget&, const juce::File& resultFile);

} // namespace roughtrack
