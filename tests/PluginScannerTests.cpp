#include "TestHelpers.h"

#include "roughtrack/PluginLibrary.h"
#include "roughtrack/PluginScanner.h"

#include <catch2/catch_test_macros.hpp>

using namespace roughtrack;

namespace
{

constexpr int quickTimeoutMs = 500;

const auto validResult = juce::String (
    "<PLUGINS><PLUGIN name=\"Fine Amp\" format=\"VST3\" file=\"/fine.vst3\" uniqueId=\"1\" deprecatedUid=\"1\"/></PLUGINS>");

/** Stands in for the child with a shell script, keyed by target identifier. */
PluginScanCommand shellCommand()
{
    return [] (const PluginScanTarget& target, const juce::File& resultFile)
    {
        auto script = target.identifier;

        if (target.identifier == "good")
        {
            resultFile.getSiblingFile ("valid.xml").replaceWithText (validResult);
            script = "cp '" + resultFile.getSiblingFile ("valid.xml").getFullPathName() + "' '" + resultFile.getFullPathName() + "'";
        }

        return juce::StringArray { "/bin/sh", "-c", script };
    };
}

} // namespace

TEST_CASE ("A plugin that loads is added to the results")
{
    test::TempDirectory temp;

    const auto result = scanPlugins ({ { "VST3", "good" } }, shellCommand(), temp.directory, 5000);

    REQUIRE (result.plugins.size() == 1);
    CHECK (result.plugins[0].name == "Fine Amp");
    CHECK (result.failures.empty());
}

TEST_CASE ("A plugin that crashes the scanner is reported and the scan carries on")
{
    test::TempDirectory temp;

    const auto result = scanPlugins ({ { "VST3", "kill -SEGV $$" }, { "VST3", "good" } }, shellCommand(), temp.directory, 5000);

    REQUIRE (result.failures.size() == 1);
    CHECK (result.failures[0].target.identifier == "kill -SEGV $$");
    CHECK (result.plugins.size() == 1);
}

TEST_CASE ("A plugin that exits cleanly without a result counts as failed")
{
    test::TempDirectory temp;

    const auto result = scanPlugins ({ { "VST3", "exit 0" } }, shellCommand(), temp.directory, 5000);

    CHECK (result.plugins.empty());
    CHECK (result.failures.size() == 1);
}

TEST_CASE ("A plugin that hangs is killed at the timeout")
{
    test::TempDirectory temp;
    const auto startMs = juce::Time::getMillisecondCounter();

    const auto result = scanPlugins ({ { "VST3", "sleep 30" }, { "VST3", "good" } }, shellCommand(), temp.directory, quickTimeoutMs);

    CHECK (juce::Time::getMillisecondCounter() - startMs < 10000);
    REQUIRE (result.failures.size() == 1);
    CHECK (result.failures[0].reason.contains ("didn't finish"));
    CHECK (result.plugins.size() == 1);
}

TEST_CASE ("Stopping a scan kills the plugin it's waiting on")
{
    test::TempDirectory temp;
    const auto startMs = juce::Time::getMillisecondCounter();

    const auto result = scanPlugins ({ { "VST3", "sleep 30" }, { "VST3", "good" } },
                                     shellCommand(),
                                     temp.directory,
                                     60000,
                                     {},
                                     [startMs] { return juce::Time::getMillisecondCounter() - startMs > 200; });

    CHECK (juce::Time::getMillisecondCounter() - startMs < 10000);
    CHECK (result.plugins.empty());
    CHECK (result.failures.empty());
}

TEST_CASE ("A plugin list survives being saved")
{
    test::TempDirectory temp;
    const auto file = temp.directory.getChildFile ("plugins.xml");

    juce::PluginDescription plugin;
    plugin.name = "Fine Amp";
    plugin.pluginFormatName = "VST3";

    REQUIRE (writePluginList ({ plugin }, file));

    const auto loaded = readPluginList (file);

    REQUIRE (loaded.size() == 1);
    CHECK (loaded[0].name == "Fine Amp");
}

TEST_CASE ("A scanner that can't start is reported")
{
    test::TempDirectory temp;

    const PluginScanCommand missing = [] (const PluginScanTarget&, const juce::File&)
    {
        return juce::StringArray { "/nonexistent/roughtrack-scanner" };
    };

    const auto result = scanPlugins ({ { "VST3", "any" } }, missing, temp.directory, quickTimeoutMs);

    CHECK (result.plugins.empty());
    CHECK (result.failures.size() == 1);
}

TEST_CASE ("Only effects are offered as inserts")
{
    const auto plugin = [] (const juce::String& category, bool instrument)
    {
        juce::PluginDescription description;
        description.category = category;
        description.isInstrument = instrument;
        return description;
    };

    CHECK (PluginLibrary::isInsertEffect (plugin ("Effect", false)));
    CHECK (PluginLibrary::isInsertEffect (plugin ("Fx|Distortion", false)));
    CHECK_FALSE (PluginLibrary::isInsertEffect (plugin ("Synth", true)));
    CHECK_FALSE (PluginLibrary::isInsertEffect (plugin ("Mixer", false)));
    CHECK_FALSE (PluginLibrary::isInsertEffect (plugin ("Panner", false)));
    CHECK_FALSE (PluginLibrary::isInsertEffect (plugin ("Generator", false)));
}
