// Headless checks that need a real process or real hardware.
//
//   rifftrack_bench crash-writer <file>
//       Records a sine wave in real time until killed. The crash recovery test
//       kills this process mid-take and inspects what it left behind.
//
//   rifftrack_bench soak <seconds> [--device <name>] [--buffer <samples>] [--record] [--stall-ms <ms>] [--amp <file>] [--amps <count>] [--cabinet <file>] [--full-channel]
//       Plays 7 tracks through a real device, muted, and reports dropouts.
//       --record also records the first two inputs at the same time.
//       --stall-ms blocks the audio thread once, halfway through, to prove the
//       dropout detector catches a real failure.
//       --amp loads a NAM capture on the first --amps tracks (all by default),
//       and --cabinet loads an IR on the same tracks. --full-channel turns on
//       every track's EQ, compressor and sends.
//
//   rifftrack_bench overdub-check [--device <name>] [--buffer <samples>] [--input <number>]
//       Needs a cable from output 1 to input 1, and plays test sweeps. Measures
//       round-trip latency, then records an overdub through the cable and
//       reports how far it landed from the part it was recorded against.
//
//   rifftrack_bench scan-plugins [--timeout <seconds>]
//       Scans every installed VST3 and Audio Unit, each in its own process.
//
//   rifftrack_bench scan-plugin <format> <identifier> <result file>
//       The child process of scan-plugins. Loads one plugin.
//
//   rifftrack_bench plugin-window <format> <name> [--seconds <seconds>] [--first <format> <name>]
//       Opens a plugin's editor the way the app does and prints its native
//       views. --first opens another plugin in the same process before it.
//
//   rifftrack_bench amp-check <file> [--buffer <samples>] [--instances <count>] [--lite]
//       Loads a NAM capture and times it against the buffer deadline, running
//       the given number of copies in one block as a session would.
//
//   rifftrack_bench plugin-check [--buffer <samples>] [--name <text>]
//       Runs every third-party effect through the engine with no audio device.
//       Compares reported latency with a measured pulse, times each block
//       against the buffer deadline, and reloads the plugin from saved state.
//
//   rifftrack_bench tuner-check <take.wav>
//       Runs the tuner over a recording as the app would, and reports each note
//       it holds, how steady the reading is and what the analysis costs.

#include "BuildDrumKit.h"
#include "NativeViews.h"
#include "PluginWindow.h"

#include "rifftrack/AmpModel.h"
#include "rifftrack/AudioDevices.h"
#include "rifftrack/AudioEngine.h"
#include "rifftrack/PluginScanner.h"
#include "rifftrack/Tuner.h"

#include <CoreFoundation/CoreFoundation.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <thread>

namespace
{

constexpr double benchSampleRate = 48000.0;
constexpr int benchTracks = 7;
constexpr int writerBlockSize = 1024;

float testTone (juce::int64 sample)
{
    return 0.5f * (float) std::sin (2.0 * std::numbers::pi * 440.0 * (double) sample / benchSampleRate);
}

int runCrashWriter (const juce::File& file)
{
    juce::TimeSliceThread thread ("bench writer");
    thread.startThread();

    auto writer = rifftrack::TakeWriter::create (file, benchSampleRate, 1, thread);

    if (writer == nullptr)
        return 1;

    const auto blockTime = std::chrono::duration<double> (writerBlockSize / benchSampleRate);
    auto nextBlock = std::chrono::steady_clock::now();

    std::vector<float> block (writerBlockSize);

    for (juce::int64 written = 0;;)
    {
        for (auto& sample : block)
            sample = testTone (written++);

        const float* channels[] { block.data() };
        writer->write (channels, writerBlockSize);

        nextBlock += std::chrono::duration_cast<std::chrono::steady_clock::duration> (blockTime);
        std::this_thread::sleep_until (nextBlock);
    }
}

juce::File makeWorkDirectory()
{
    const auto directory = juce::File::createTempFile ("").withFileExtension ("").getSiblingFile ("rifftrack-bench");
    directory.deleteRecursively();
    directory.createDirectory();
    return directory;
}

bool openDevice (juce::AudioDeviceManager& manager, const juce::StringArray& args, bool withInputs, int bufferSize)
{
    const auto error = manager.initialise (withInputs ? 2 : 0, 2, nullptr, true);

    if (error.isNotEmpty())
    {
        std::cout << "Device error: " << error << std::endl;
        return false;
    }

    auto setup = manager.getAudioDeviceSetup();
    setup.sampleRate = benchSampleRate;
    setup.bufferSize = bufferSize;

    if (const auto deviceIndex = args.indexOf ("--device"); deviceIndex >= 0)
    {
        setup.outputDeviceName = args[deviceIndex + 1];
        setup.inputDeviceName = withInputs ? args[deviceIndex + 1] : juce::String();
    }

    const auto setupError = manager.setAudioDeviceSetup (setup, true);

    if (setupError.isNotEmpty())
    {
        std::cout << "Device error: " << setupError << std::endl;
        return false;
    }

    return true;
}

/** Forwards to the engine, but can hold up one callback on purpose. */
class StallingCallback final : public juce::AudioIODeviceCallback
{
public:
    explicit StallingCallback (rifftrack::AudioEngine& engineToWrap) : engine (engineToWrap) {}

    void requestStall (int milliseconds) { stallMs = milliseconds; }

    void audioDeviceIOCallbackWithContext (const float* const* inputs,
                                           int numInputs,
                                           float* const* outputs,
                                           int numOutputs,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override
    {
        engine.audioDeviceIOCallbackWithContext (inputs, numInputs, outputs, numOutputs, numSamples, context);

        if (const auto milliseconds = stallMs.exchange (0); milliseconds > 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (milliseconds));
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override { engine.audioDeviceAboutToStart (device); }
    void audioDeviceStopped() override { engine.audioDeviceStopped(); }

private:
    rifftrack::AudioEngine& engine;
    std::atomic<int> stallMs { 0 };
};

juce::File writeSourceTake (const juce::File& directory, int seconds)
{
    const auto file = directory.getChildFile ("source.wav");

    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (benchSampleRate)
                                                         .withNumChannels (1)
                                                         .withBitsPerSample (24);
    auto writer = juce::WavAudioFormat().createWriterFor (stream, options);

    juce::AudioBuffer<float> block (1, 48000);

    for (int second = 0; second < seconds; ++second)
    {
        for (int i = 0; i < block.getNumSamples(); ++i)
            block.setSample (0, i, testTone (second * 48000 + i));

        writer->writeFromAudioSampleBuffer (block, 0, block.getNumSamples());
    }

    return file;
}

int runSoak (const juce::StringArray& args)
{
    const auto seconds = args[2].getIntValue();
    const auto bufferIndex = args.indexOf ("--buffer");
    const auto recording = args.contains ("--record");
    const auto bufferSize = bufferIndex >= 0 ? args[bufferIndex + 1].getIntValue() : 64;
    const auto stallIndex = args.indexOf ("--stall-ms");
    const auto stallMs = stallIndex >= 0 ? args[stallIndex + 1].getIntValue() : 0;

    if (seconds <= 0)
        return 1;

    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const auto directory = makeWorkDirectory();

    std::cout << "Preparing " << seconds << "s of audio for 7 tracks" << std::endl;

    const auto source = writeSourceTake (directory, seconds + 5);

    juce::AudioDeviceManager manager;

    if (! openDevice (manager, args, recording, bufferSize))
        return 1;

    rifftrack::AudioEngine engine;
    engine.setSessionDirectory (directory);
    engine.setMasterGain (0.0f);

    for (int track = 0; track < benchTracks; ++track)
    {
        engine.activateTrack (track, "Track " + juce::String (track + 1));

        const auto copy = directory.getChildFile ("track" + juce::String (track) + ".wav");
        source.copyFileTo (copy);
        engine.loadTake (track, copy, 0);
    }

    const auto ampIndex = args.indexOf ("--amp");
    const auto ampsIndex = args.indexOf ("--amps");
    const auto cabinetIndex = args.indexOf ("--cabinet");
    const auto numAmps = ampsIndex >= 0 ? args[ampsIndex + 1].getIntValue() : benchTracks;

    for (int track = 0; track < numAmps; ++track)
    {
        if (ampIndex >= 0)
        {
            const auto result = engine.loadAmp (track, juce::File (args[ampIndex + 1]));

            if (result.failed())
            {
                std::cout << "Amp failed: " << result.getErrorMessage() << std::endl;
                return 1;
            }
        }

        if (cabinetIndex >= 0)
            engine.getChannel (track).loadCabinet (juce::File (args[cabinetIndex + 1]));
    }

    if (args.contains ("--full-channel"))
    {
        for (int track = 0; track < benchTracks; ++track)
        {
            auto& channel = engine.getChannel (track);

            channel.setParameter (rifftrack::ChannelParameter::eqEnabled, 1.0f);
            channel.setParameter (rifftrack::ChannelParameter::highPass, 80.0f);
            channel.setParameter (rifftrack::ChannelParameter::midGain, -3.0f);
            channel.setParameter (rifftrack::ChannelParameter::compressorEnabled, 1.0f);
            channel.setParameter (rifftrack::ChannelParameter::sendReverb, 0.3f);
            channel.setParameter (rifftrack::ChannelParameter::sendDelay, 0.2f);
        }
    }

    const auto drumsIndex = args.indexOf ("--drums");

    if (drumsIndex >= 0)
    {
        engine.setDrumFolder (juce::File (args[drumsIndex + 1]));

        if (const auto drums = engine.loadDrums(); drums.failed())
        {
            std::cout << drums.getErrorMessage() << std::endl;
            return 1;
        }

        const auto& groove = engine.getGrooves().getGrooves().front();
        engine.setDrumParts (engine.addTrack ("Drums", rifftrack::TrackType::drums), { { groove.id, 0, 1000, 0 } });
    }

    StallingCallback callback (engine);
    manager.addAudioCallback (&callback);

    auto* device = manager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        std::cout << "No device opened" << std::endl;
        return 1;
    }

    std::cout << device->getName() << ": " << device->getCurrentSampleRate() << " Hz, "
              << device->getCurrentBufferSizeSamples() << " samples" << std::endl;

    if (recording)
    {
        engine.setTrackArmed (5, true);
        engine.setTrackArmed (6, true);
        engine.setTrackInput (5, 0, 1);
        engine.setTrackInput (6, 1, 1);

        const auto result = engine.record();

        if (result.failed())
        {
            std::cout << result.getErrorMessage() << std::endl;
            return 1;
        }
    }
    else
    {
        engine.play();
    }

    const auto startStats = engine.getStats();

    std::this_thread::sleep_for (std::chrono::milliseconds (seconds * 500));
    callback.requestStall (stallMs);
    std::this_thread::sleep_for (std::chrono::milliseconds (seconds * 500));

    const auto stats = engine.getStats();
    const auto position = engine.getPosition();
    const auto dropoutLog = engine.getDropoutLog();

    engine.stop();
    manager.removeAudioCallback (&callback);
    directory.deleteRecursively();

    std::cout << "Processed " << (double) position / benchSampleRate << "s\n"
              << "Blocks timed: " << stats.timedBlocks - startStats.timedBlocks << "\n"
              << "Dropouts: " << stats.dropouts - startStats.dropouts << "\n"
              << "Playback underrun samples: " << stats.playbackUnderrunSamples << "\n"
              << "Dropped recording samples: " << stats.droppedRecordingSamples << std::endl;

    for (const auto& dropout : dropoutLog)
        std::cout << "  dropout at " << dropout.atSeconds << "s: " << dropout.gapMilliseconds << " ms gap" << std::endl;

    return stats.timedBlocks > startStats.timedBlocks && stats.dropouts == startStats.dropouts
                   && stats.playbackUnderrunSamples == 0 && stats.droppedRecordingSamples == 0
               ? 0
               : 2;
}

int runOverdubCheck (const juce::StringArray& args)
{
    constexpr int pulsePosition = 48000;
    constexpr int takeLength = 96000;
    constexpr double recordSeconds = 3.0;

    const auto bufferIndex = args.indexOf ("--buffer");
    const auto bufferSize = bufferIndex >= 0 ? args[bufferIndex + 1].getIntValue() : 64;

    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::AudioDeviceManager manager;

    if (! openDevice (manager, args, true, bufferSize))
        return 1;

    const auto directory = makeWorkDirectory();

    rifftrack::AudioEngine engine;
    engine.setSessionDirectory (directory);
    engine.activateTrack (0, "Reference");
    engine.activateTrack (1, "Overdub");
    manager.addAudioCallback (&engine);

    auto* device = manager.getCurrentAudioDevice();

    const auto inputIndex = args.indexOf ("--input");
    const auto input = inputIndex >= 0 ? args[inputIndex + 1].getIntValue() - 1 : 0;

    if (device == nullptr || engine.startCalibration (input).failed())
    {
        std::cout << "No device opened" << std::endl;
        return 1;
    }

    std::cout << device->getName() << ": " << device->getCurrentSampleRate() << " Hz, "
              << device->getCurrentBufferSizeSamples() << " samples\n"
              << "Driver reports " << engine.getRecordingLatency() << " samples" << std::endl;

    while (! engine.isCalibrationFinished())
        std::this_thread::sleep_for (std::chrono::milliseconds (20));

    const auto calibration = engine.finishCalibration();

    if (! calibration.succeeded)
    {
        std::cout << calibration.message << " Correlation " << calibration.correlation << "." << std::endl;
        manager.removeAudioCallback (&engine);
        directory.deleteRecursively();
        return 2;
    }

    std::cout << "Measured " << calibration.latencySamples << " samples ("
              << rifftrack::bufferLatencyMs (calibration.latencySamples, device->getCurrentSampleRate()) << " ms)" << std::endl;

    juce::AudioBuffer<float> pulse (1, takeLength);
    pulse.clear();
    pulse.setSample (0, pulsePosition, 0.5f);

    const auto pulseFile = directory.getChildFile ("pulse.wav");

    {
        std::unique_ptr<juce::OutputStream> stream = pulseFile.createOutputStream();
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (benchSampleRate)
                                                             .withNumChannels (1)
                                                             .withBitsPerSample (24);
        juce::WavAudioFormat().createWriterFor (stream, options)->writeFromAudioSampleBuffer (pulse, 0, takeLength);
    }

    engine.loadTake (0, pulseFile, 0);
    engine.setTrackArmed (1, true);
    engine.setTrackInput (1, input, 1);

    const auto result = engine.record();

    if (result.failed())
    {
        std::cout << result.getErrorMessage() << std::endl;
        return 1;
    }

    std::this_thread::sleep_for (std::chrono::duration<double> (recordSeconds));
    engine.stop();

    const auto take = engine.getTake (1);
    manager.removeAudioCallback (&engine);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (take.file));

    juce::AudioBuffer<float> recorded (1, (int) reader->lengthInSamples);
    reader->read (&recorded, 0, recorded.getNumSamples(), 0, true, false);

    auto peakIndex = 0;
    auto peakLevel = 0.0f;

    for (int i = 0; i < recorded.getNumSamples(); ++i)
    {
        if (const auto level = std::abs (recorded.getSample (0, i)); level > peakLevel)
        {
            peakLevel = level;
            peakIndex = i;
        }
    }

    const auto landedAt = take.startSample + peakIndex;

    std::cout << "Overdub landed " << landedAt - pulsePosition << " samples from the original (peak " << peakLevel << ")"
              << std::endl;

    directory.deleteRecursively();
    return 0;
}

int runScanPlugin (const juce::StringArray& args)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::AudioPluginFormatManager formats;
    rifftrack::addPluginFormats (formats);

    return rifftrack::writePluginScanResult (formats, { args[2], args[3] }, juce::File (args[4])) ? 0 : 1;
}

struct PluginCheck
{
    int peakAt = -1;
    float peak = 0.0f;
    bool invalidOutput = false;
    double averageBlockMs = 0.0, worstBlockMs = 0.0;
    int blocksOverDeadline = 0;
};

PluginCheck measurePlugin (rifftrack::AudioEngine& engine, int bufferSize)
{
    constexpr int pulseAt = 24000;
    constexpr int numPulseSamples = 96000;
    constexpr int numToneSamples = 144000;

    const auto deadlineMs = 1000.0 * bufferSize / benchSampleRate;

    std::vector<float> input ((size_t) bufferSize), left ((size_t) bufferSize), right ((size_t) bufferSize);
    const float* inputs[] { input.data() };
    float* outputs[] { left.data(), right.data() };

    PluginCheck check;

    for (int sample = 0; sample < numPulseSamples; sample += bufferSize)
    {
        std::fill (input.begin(), input.end(), 0.0f);

        if (pulseAt >= sample && pulseAt < sample + bufferSize)
            input[(size_t) (pulseAt - sample)] = 0.5f;

        engine.processBlock (inputs, 1, outputs, 2, bufferSize);

        for (int i = 0; i < bufferSize; ++i)
        {
            const auto level = std::abs (left[(size_t) i]);

            if (sample + i >= pulseAt && level > check.peak)
            {
                check.peak = level;
                check.peakAt = sample + i - pulseAt;
            }
        }
    }

    auto totalMs = 0.0;
    auto numBlocks = 0;

    for (int sample = 0; sample < numToneSamples; sample += bufferSize)
    {
        for (int i = 0; i < bufferSize; ++i)
            input[(size_t) i] = testTone (sample + i);

        const auto start = juce::Time::getMillisecondCounterHiRes();
        engine.processBlock (inputs, 1, outputs, 2, bufferSize);
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;

        totalMs += elapsed;
        ++numBlocks;
        check.worstBlockMs = juce::jmax (check.worstBlockMs, elapsed);

        if (elapsed > deadlineMs)
            ++check.blocksOverDeadline;

        for (auto level : left)
            if (! std::isfinite (level))
                check.invalidOutput = true;
    }

    check.averageBlockMs = totalMs / numBlocks;
    return check;
}

int runPluginCheck (const juce::StringArray& args)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const auto bufferIndex = args.indexOf ("--buffer");
    const auto bufferSize = bufferIndex > 0 ? args[bufferIndex + 1].getIntValue() : 64;
    const auto nameIndex = args.indexOf ("--name");
    const auto nameFilter = nameIndex > 0 ? args[nameIndex + 1] : juce::String();

    juce::AudioPluginFormatManager formats;
    rifftrack::addPluginFormats (formats);

    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();

    const auto scan = rifftrack::scanPlugins (rifftrack::findPluginScanTargets (formats),
                                              [&] (const rifftrack::PluginScanTarget& target, const juce::File& resultFile)
                                              {
                                                  return juce::StringArray { executable, "scan-plugin", target.format, target.identifier, resultFile.getFullPathName() };
                                              },
                                              makeWorkDirectory(),
                                              60000);

    auto failures = 0;

    for (const auto& plugin : scan.plugins)
    {
        if (plugin.manufacturerName == "Apple" || plugin.isInstrument
            || (nameFilter.isNotEmpty() && ! plugin.name.containsIgnoreCase (nameFilter)))
            continue;

        std::cout << "\n" << plugin.pluginFormatName << " " << plugin.name << std::endl;

        juce::String error;
        auto instance = formats.createPluginInstance (plugin, benchSampleRate, bufferSize, error);

        if (instance == nullptr)
        {
            std::cout << "  FAILED to load: " << error << std::endl;
            ++failures;
            continue;
        }

        auto* raw = instance.get();

        rifftrack::AudioEngine engine;
        engine.prepare (benchSampleRate, bufferSize);
        engine.setRecordingLatency (0);
        engine.activateTrack (0, "Bench");
        engine.setTrackArmed (0, true);
        engine.setTrackMonitoring (0, true);

        if (engine.setInsert (0, 0, std::move (instance)).failed())
        {
            std::cout << "  FAILED to insert" << std::endl;
            ++failures;
            continue;
        }

        std::cout << "  channels " << raw->getTotalNumInputChannels() << " in, " << raw->getTotalNumOutputChannels() << " out, "
                  << "editor " << (raw->hasEditor() ? "yes" : "no") << ", reported latency " << raw->getLatencySamples() << std::endl;

        const auto check = measurePlugin (engine, bufferSize);

        std::cout << "  pulse peak " << check.peak << " at +" << check.peakAt << " samples\n"
                  << "  block average " << check.averageBlockMs << " ms, worst " << check.worstBlockMs
                  << " ms, over " << 1000.0 * bufferSize / benchSampleRate << " ms deadline " << check.blocksOverDeadline << " times"
                  << (check.invalidOutput ? ", INVALID OUTPUT" : "") << std::endl;

        if (check.invalidOutput)
            ++failures;

        juce::MemoryBlock state;
        raw->getStateInformation (state);

        auto reloaded = formats.createPluginInstance (plugin, benchSampleRate, bufferSize, error);

        if (reloaded == nullptr || engine.setInsert (0, 1, std::move (reloaded), state).failed())
        {
            std::cout << "  FAILED to reload" << std::endl;
            ++failures;
            continue;
        }

        juce::MemoryBlock stateAfterReload;
        engine.getInsert (0, 1)->getStateInformation (stateAfterReload);

        std::cout << "  state " << state.getSize() << " bytes, after reload " << stateAfterReload.getSize()
                  << (state == stateAfterReload ? " (identical)" : " (differs)") << std::endl;
    }

    std::cout << "\n" << failures << " failures" << std::endl;
    return failures == 0 ? 0 : 1;
}

std::unique_ptr<juce::AudioPluginInstance> loadNamedPlugin (juce::AudioPluginFormatManager& formats,
                                                            const juce::String& formatName,
                                                            const juce::String& name)
{
    for (auto* format : formats.getFormats())
    {
        if (format->getName() != formatName)
            continue;

        for (const auto& identifier : format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false))
        {
            if (! identifier.containsIgnoreCase (name))
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile (found, identifier);

            juce::String error;

            if (! found.isEmpty())
                return formats.createPluginInstance (*found[0], benchSampleRate, 64, error);
        }
    }

    return nullptr;
}

int runPluginWindow (const juce::StringArray& args)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const auto secondsIndex = args.indexOf ("--seconds");
    const auto seconds = secondsIndex > 0 ? args[secondsIndex + 1].getIntValue() : 3;
    const auto firstIndex = args.indexOf ("--first");

    juce::AudioPluginFormatManager formats;
    rifftrack::addPluginFormats (formats);

    rifftrack::AudioEngine engine;
    engine.prepare (benchSampleRate, 64);

    std::vector<std::unique_ptr<PluginWindow>> windows;

    const auto open = [&] (const juce::String& format, const juce::String& name, int track)
    {
        auto instance = loadNamedPlugin (formats, format, name);

        if (instance == nullptr)
        {
            std::cout << "Couldn't load " << format << " " << name << std::endl;
            return false;
        }

        auto* raw = instance.get();
        engine.setInsert (track, 0, std::move (instance));
        windows.push_back (std::make_unique<PluginWindow> (*raw, std::function<void()>()));
        windows.back()->setAlwaysOnTop (true);
        return true;
    };

    if (firstIndex > 0 && ! open (args[firstIndex + 1], args[firstIndex + 2], 0))
        return 1;

    if (! open (args[2], args[3], 1))
        return 1;

    juce::Timer::callAfterDelay (seconds * 1000, [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();

    for (auto& window : windows)
    {
        std::cout << "window " << window->getBounds().toString() << std::endl;
        printNativeViewTree (window->getPeer()->getNativeHandle());
    }

    windows.clear();
    return 0;
}

int runAmpCheck (const juce::StringArray& args)
{
    const auto option = [&args] (const char* name, int fallback)
    {
        const auto index = args.indexOf (name);
        return index > 0 ? args[index + 1].getIntValue() : fallback;
    };

    const auto bufferSize = option ("--buffer", 64);
    const auto numInstances = option ("--instances", 1);
    const auto lite = args.contains ("--lite");

    std::vector<std::unique_ptr<rifftrack::AmpModel>> models;

    const auto loadStart = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < numInstances; ++i)
    {
        auto loaded = rifftrack::AmpModel::load (juce::File (args[2]), benchSampleRate, bufferSize, lite);

        if (loaded.model == nullptr)
        {
            std::cout << "FAILED: " << loaded.error << std::endl;
            return 1;
        }

        models.push_back (std::move (loaded.model));
    }

    std::cout << "Loaded " << numInstances << " in " << juce::Time::getMillisecondCounterHiRes() - loadStart
              << " ms, has lite version: " << (models[0]->hasLiteVersion() ? "yes" : "no") << std::endl;

    const auto deadlineMs = 1000.0 * bufferSize / benchSampleRate;
    constexpr int numSeconds = 5;

    std::vector<float> block ((size_t) bufferSize);
    auto totalMs = 0.0, worstMs = 0.0, peak = 0.0;
    auto numBlocks = 0, overDeadline = 0;
    auto invalid = false;

    for (juce::int64 sample = 0; sample < numSeconds * (juce::int64) benchSampleRate; sample += bufferSize)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (auto& model : models)
        {
            for (int i = 0; i < bufferSize; ++i)
                block[(size_t) i] = 0.2f * testTone (sample + i);

            model->process (block.data(), bufferSize);
        }

        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;

        totalMs += elapsed;
        worstMs = juce::jmax (worstMs, elapsed);
        ++numBlocks;

        if (elapsed > deadlineMs)
            ++overDeadline;

        for (auto level : block)
        {
            peak = juce::jmax (peak, (double) std::abs (level));
            invalid = invalid || ! std::isfinite (level);
        }
    }

    const auto average = totalMs / numBlocks;

    std::cout << "Average " << average << " ms (" << 100.0 * average / deadlineMs << "% of " << deadlineMs << " ms deadline), worst "
              << worstMs << " ms, over deadline " << overDeadline << " of " << numBlocks << " blocks, output peak " << peak
              << (invalid ? ", INVALID OUTPUT" : "") << std::endl;

    return invalid ? 1 : 0;
}

int runScanPlugins (const juce::StringArray& args)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const auto timeoutIndex = args.indexOf ("--timeout");
    const auto timeoutMs = 1000 * (timeoutIndex > 0 ? args[timeoutIndex + 1].getIntValue() : 60);

    juce::AudioPluginFormatManager formats;
    rifftrack::addPluginFormats (formats);

    const auto targets = rifftrack::findPluginScanTargets (formats);
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();

    const auto command = [&] (const rifftrack::PluginScanTarget& target, const juce::File& resultFile)
    {
        return juce::StringArray { executable, "scan-plugin", target.format, target.identifier, resultFile.getFullPathName() };
    };

    const auto startMs = juce::Time::getMillisecondCounter();
    const auto result = rifftrack::scanPlugins (targets, command, makeWorkDirectory(), timeoutMs);

    for (const auto& plugin : result.plugins)
        std::cout << plugin.pluginFormatName << "  " << plugin.manufacturerName << " / " << plugin.name
                  << "  [" << plugin.category << "]  in " << plugin.numInputChannels
                  << " out " << plugin.numOutputChannels << (plugin.isInstrument ? "  instrument" : "") << "\n";

    for (const auto& failure : result.failures)
        std::cout << "FAILED " << failure.target.format << " " << failure.target.identifier << ": " << failure.reason << "\n";

    std::cout << targets.size() << " targets, " << result.plugins.size() << " plugins, " << result.failures.size()
              << " failures in " << (juce::Time::getMillisecondCounter() - startMs) / 1000.0 << " s" << std::endl;

    return 0;
}

/** Plays notes through real CoreMIDI into macOS's own synth on a real device, with
    the output muted: a virtual keyboard stands in for a USB one from the OS onwards.
*/
int runMidiCheck (const juce::StringArray& args)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    constexpr int numNotes = 16;
    constexpr int noteSpacingMs = 125;
    constexpr int noteLengthMs = 60;
    constexpr double tempo = 120.0;
    constexpr auto deviceName = "RiffTrack Check";

    const auto bufferIndex = args.indexOf ("--buffer");
    const auto bufferSize = bufferIndex >= 0 ? args[bufferIndex + 1].getIntValue() : 64;

    juce::AudioDeviceManager manager;

    if (! openDevice (manager, args, false, bufferSize))
        return 1;

    juce::AudioPluginFormatManager formats;
    rifftrack::addPluginFormats (formats);

    std::unique_ptr<juce::AudioPluginInstance> synth;
    juce::String error;

    for (auto* format : formats.getFormats())
    {
        for (const auto& identifier : format->searchPathsForPlugins ({}, true, false))
        {
            juce::OwnedArray<juce::PluginDescription> found;

            if (identifier.containsIgnoreCase ("dls"))
                format->findAllTypesForFile (found, identifier);

            if (! found.isEmpty() && synth == nullptr)
                synth = formats.createPluginInstance (*found[0], benchSampleRate, bufferSize, error);
        }
    }

    if (synth == nullptr)
    {
        std::cout << "Couldn't load DLSMusicDevice: " << error << std::endl;
        return 1;
    }

    const auto directory = makeWorkDirectory();

    rifftrack::AudioEngine engine;
    engine.setSessionDirectory (directory);
    engine.setTempo (tempo);
    engine.setMasterGain (0.0f);

    const auto track = engine.addTrack ("Keys", rifftrack::TrackType::midi);

    if (const auto result = engine.setInsert (track, rifftrack::instrumentSlot, std::move (synth)); result.failed())
    {
        std::cout << result.getErrorMessage() << std::endl;
        return 1;
    }

    const auto keyboard = juce::MidiOutput::createNewDevice (deviceName);

    if (keyboard == nullptr)
    {
        std::cout << "Couldn't create a virtual keyboard" << std::endl;
        return 1;
    }

    auto connected = false;

    // CoreMIDI only publishes a new device while this thread's run loop is turning.
    for (int attempt = 0; attempt < 20 && ! connected; ++attempt)
    {
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.1, false);

        for (const auto& device : juce::MidiInput::getAvailableDevices())
        {
            if (device.name == deviceName)
            {
                manager.setMidiInputDeviceEnabled (device.identifier, true);
                connected = true;
            }
        }
    }

    if (! connected)
    {
        std::cout << "The virtual keyboard never appeared" << std::endl;
        return 1;
    }

    manager.addMidiInputDeviceCallback ({}, &engine);
    manager.addAudioCallback (&engine);

    auto* device = manager.getCurrentAudioDevice();
    std::cout << device->getName() << ": " << device->getCurrentSampleRate() << " Hz, " << device->getCurrentBufferSizeSamples() << " samples" << std::endl;

    engine.setRecordingLatency (0);
    engine.setTrackArmed (track, true);

    const auto recorded = engine.record();

    if (recorded.failed())
    {
        std::cout << recorded.getErrorMessage() << std::endl;
        return 1;
    }

    std::vector<double> sentMs;

    juce::Thread::sleep (200);
    engine.takeTrackPeak (track);

    for (int note = 0; note < numNotes; ++note)
    {
        sentMs.push_back (juce::Time::getMillisecondCounterHiRes());
        keyboard->sendMessageNow (juce::MidiMessage::noteOn (1, 60 + note, (juce::uint8) 100));
        juce::Thread::sleep (noteLengthMs);
        keyboard->sendMessageNow (juce::MidiMessage::noteOff (1, 60 + note));
        juce::Thread::sleep (noteSpacingMs - noteLengthMs);
    }

    juce::Thread::sleep (200);

    const auto recordingPeak = engine.takeTrackPeak (track);
    std::cout << "Keyboard while recording peaked at " << juce::Decibels::gainToDecibels (recordingPeak) << " dB" << std::endl;

    engine.stop();
    engine.setTrackArmed (track, false);

    juce::Thread::sleep (100);
    engine.setTrackArmed (track, true);
    juce::Thread::sleep (100);
    engine.takeTrackPeak (track);

    keyboard->sendMessageNow (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    juce::Thread::sleep (300);
    keyboard->sendMessageNow (juce::MidiMessage::noteOff (1, 60));
    juce::Thread::sleep (200);

    const auto stoppedPeak = engine.takeTrackPeak (track);
    std::cout << "Keyboard while stopped peaked at " << juce::Decibels::gainToDecibels (stoppedPeak) << " dB" << std::endl;

    engine.setTrackArmed (track, false);

    const auto clips = engine.getClips (track);

    if (clips.empty())
    {
        std::cout << "Nothing was recorded" << std::endl;
        return 1;
    }

    const auto take = rifftrack::readMidiTake (clips[0].file);

    if (take == nullptr)
    {
        std::cout << "The take couldn't be read back" << std::endl;
        return 1;
    }

    const auto msPerTick = 60000.0 / tempo / rifftrack::ticksPerBeat;
    auto worstMs = 0.0;

    for (size_t note = 0; note < take->notes.size() && note < sentMs.size(); ++note)
    {
        const auto takenGap = (double) (take->notes[note].tick - take->notes[0].tick) * msPerTick;
        worstMs = juce::jmax (worstMs, std::abs (takenGap - (sentMs[note] - sentMs[0])));
    }

    std::cout << "Sent " << numNotes << " notes, took " << take->notes.size() << " in " << clips[0].file.getFileName()
              << ", worst timing error " << worstMs << " ms" << std::endl;

    engine.takeTrackPeak (track);
    engine.setPosition (0);
    engine.play();

    auto peak = 0.0f;

    for (int i = 0; i < 40; ++i)
    {
        juce::Thread::sleep (50);
        peak = juce::jmax (peak, engine.takeTrackPeak (track));
    }

    engine.stop();
    juce::Thread::sleep (100);

    std::cout << "Playback through the synth peaked at " << juce::Decibels::gainToDecibels (peak) << " dB, CPU "
              << manager.getCpuUsage() * 100.0 << "%, dropouts " << engine.getStats().dropouts << std::endl;

    manager.removeMidiInputDeviceCallback ({}, &engine);
    manager.removeAudioCallback (&engine);

    return (int) take->notes.size() == numNotes && peak > 0.0f && recordingPeak > 0.0f && stoppedPeak > 0.0f ? 0 : 1;
}

/** Plays every groove through the engine, for levels and cost, and can save one. */
int runDrumCheck (const juce::StringArray& args)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    constexpr int blockSize = 64;
    constexpr double sampleRate = 48000.0;

    rifftrack::AudioEngine engine;
    engine.setDrumFolder (juce::File (args[2]));
    engine.prepare (sampleRate, blockSize);

    const auto loadStartMs = juce::Time::getMillisecondCounterHiRes();
    const auto loaded = engine.loadDrums();

    if (loaded.failed())
    {
        std::cout << loaded.getErrorMessage() << std::endl;
        return 1;
    }

    const auto loadMs = juce::Time::getMillisecondCounterHiRes() - loadStartMs;
    const auto track = engine.addTrack ("Drums", rifftrack::TrackType::drums);

    const auto wavIndex = args.indexOf ("--wav");
    const auto grooveIndex = args.indexOf ("--groove");

    std::vector<float> left ((size_t) blockSize), right ((size_t) blockSize), input ((size_t) blockSize);
    const float* inputs[] { input.data() };
    float* outputs[] { left.data(), right.data() };

    auto highestPeak = 0.0f;
    auto renderedSamples = (juce::int64) 0;
    auto renderMs = 0.0;
    std::vector<float> rmsLevels;

    for (const auto& groove : engine.getGrooves().getGrooves())
    {
        if (grooveIndex > 0 && groove.id != args[grooveIndex + 1])
            continue;

        engine.setTempo (groove.recordedBpm);
        engine.setDrumParts (track, { { groove.id, 0, groove.bars, 0 } });
        engine.setPosition (0);
        engine.play();

        const auto numSamples = (int) (groove.bars * rifftrack::beatsPerBar * 60.0 / groove.recordedBpm * sampleRate);
        juce::AudioBuffer<float> audio (2, numSamples);

        const auto startMs = juce::Time::getMillisecondCounterHiRes();

        for (int done = 0; done + blockSize <= numSamples; done += blockSize)
        {
            engine.processBlock (inputs, 1, outputs, 2, blockSize);
            audio.copyFrom (0, done, left.data(), blockSize);
            audio.copyFrom (1, done, right.data(), blockSize);
        }

        renderMs += juce::Time::getMillisecondCounterHiRes() - startMs;
        renderedSamples += numSamples;
        engine.stop();

        highestPeak = juce::jmax (highestPeak, audio.getMagnitude (0, numSamples));
        rmsLevels.push_back (audio.getRMSLevel (0, 0, numSamples));

        if (wavIndex > 0)
        {
            const juce::File file (args[wavIndex + 1]);
            file.deleteFile();

            std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
            auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                                                          .withNumChannels (2)
                                                                                                          .withBitsPerSample (24));
            writer->writeFromAudioSampleBuffer (audio, 0, numSamples);
        }
    }

    if (rmsLevels.empty())
        return 1;

    std::sort (rmsLevels.begin(), rmsLevels.end());

    std::cout << rmsLevels.size() << " grooves, kit loaded in " << loadMs << " ms\n"
              << "highest peak " << juce::Decibels::gainToDecibels (highestPeak) << " dB, "
              << "RMS " << juce::Decibels::gainToDecibels (rmsLevels.front()) << " to "
              << juce::Decibels::gainToDecibels (rmsLevels.back()) << " dB, median "
              << juce::Decibels::gainToDecibels (rmsLevels[rmsLevels.size() / 2]) << " dB\n"
              << "cost " << 100.0 * (renderMs / 1000.0) / ((double) renderedSamples / sampleRate) << "% of real time" << std::endl;

    return 0;
}

int runTunerCheck (const juce::File& file)
{
    constexpr int analysisSpacing = 2048;
    constexpr size_t shortestRun = 4;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return 1;

    juce::AudioBuffer<float> audio (1, (int) reader->lengthInSamples);
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, false);

    rifftrack::PitchDetector detector (reader->sampleRate);

    const auto reportRun = [] (int note, std::vector<double>& cents, double seconds)
    {
        if (cents.size() >= shortestRun)
        {
            std::sort (cents.begin(), cents.end());

            std::cout << juce::String (seconds, 2) << " s  " << rifftrack::getTunerNoteName (note) << "  median "
                      << juce::String (cents[cents.size() / 2], 1) << " cents, middle half spans "
                      << juce::String (cents[cents.size() * 3 / 4] - cents[cents.size() / 4], 1) << " cents, "
                      << cents.size() << " readings\n";
        }

        cents.clear();
    };

    auto currentNote = -1, numAnalyses = 0, numWithPitch = 0;
    std::vector<double> cents;
    auto analysisMs = 0.0;

    for (int start = 0; start + detector.getWindowSize() <= audio.getNumSamples(); start += analysisSpacing)
    {
        const auto began = juce::Time::getMillisecondCounterHiRes();
        const auto reading = detector.analyse (audio.getReadPointer (0, start));

        analysisMs += juce::Time::getMillisecondCounterHiRes() - began;
        ++numAnalyses;

        const auto seconds = start / reader->sampleRate;

        if (! reading.hasPitch || reading.note != currentNote)
        {
            reportRun (currentNote, cents, seconds);
            currentNote = reading.hasPitch ? reading.note : -1;
        }

        if (reading.hasPitch)
        {
            cents.push_back (reading.cents);
            ++numWithPitch;
        }
    }

    reportRun (currentNote, cents, (double) audio.getNumSamples() / reader->sampleRate);

    const auto msPerAnalysis = analysisMs / juce::jmax (1, numAnalyses);

    std::cout << numWithPitch << " of " << numAnalyses << " analyses found a note\n"
              << juce::String (msPerAnalysis, 2) << " ms each, "
              << juce::String (100.0 * msPerAnalysis / 1000.0 / (analysisSpacing / reader->sampleRate), 2) << "% of one core" << std::endl;

    return 0;
}

} // namespace

int main (int argc, char** argv)
{
    juce::StringArray args (argv, argc);

    if (args[1] == "crash-writer" && args.size() == 3)
        return runCrashWriter (juce::File (args[2]));

    if (args[1] == "soak" && args.size() >= 3)
        return runSoak (args);

    if (args[1] == "overdub-check")
        return runOverdubCheck (args);

    if (args[1] == "scan-plugin" && args.size() == 5)
        return runScanPlugin (args);

    if (args[1] == "scan-plugins")
        return runScanPlugins (args);

    if (args[1] == "plugin-check")
        return runPluginCheck (args);

    if (args[1] == "amp-check" && args.size() >= 3)
        return runAmpCheck (args);

    if (args[1] == "plugin-window" && args.size() >= 4)
        return runPluginWindow (args);

    if (args[1] == "midi-check")
        return runMidiCheck (args);

    if (args[1] == "drum-check" && args.size() >= 3)
        return runDrumCheck (args);

    if (args[1] == "tuner-check" && args.size() == 3)
        return runTunerCheck (juce::File (args[2]));

    if (args[1] == "build-kit" && args.size() == 4)
        return runBuildDrumKit (juce::File (args[2]), juce::File (args[3]));

    std::cout << "usage: rifftrack_bench crash-writer <file>\n"
                 "       rifftrack_bench soak <seconds> [--device <name>] [--buffer <samples>] [--record] [--stall-ms <ms>] [--amp <file>] [--amps <count>] [--cabinet <file>] [--full-channel] [--drums <folder>]\n"
                 "       rifftrack_bench overdub-check [--device <name>] [--buffer <samples>] [--input <number>]\n"
                 "       rifftrack_bench scan-plugins [--timeout <seconds>]\n"
                 "       rifftrack_bench scan-plugin <format> <identifier> <result file>\n"
                 "       rifftrack_bench amp-check <file> [--buffer <samples>] [--instances <count>] [--lite]\n"
                 "       rifftrack_bench plugin-check [--buffer <samples>] [--name <text>]\n"
                 "       rifftrack_bench plugin-window <format> <name> [--seconds <seconds>] [--first <format> <name>]\n"
                 "       rifftrack_bench midi-check [--device <name>] [--buffer <samples>]\n"
                 "       rifftrack_bench drum-check <drums folder> [--groove <id>] [--wav <file>]\n"
                 "       rifftrack_bench build-kit <DrumGizmo kit folder> <output folder>\n";
    return 1;
}
