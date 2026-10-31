// Headless checks that need a real process or real hardware.
//
//   rifftrack_bench crash-writer <file>
//       Records a sine wave in real time until killed. The crash recovery test
//       kills this process mid-take and inspects what it left behind.
//
//   rifftrack_bench soak <seconds> [--device <name>] [--buffer <samples>] [--record] [--stall-ms <ms>]
//       Plays 7 tracks through a real device, muted, and reports dropouts.
//       --record also records the first two inputs at the same time.
//       --stall-ms blocks the audio thread once, halfway through, to prove the
//       dropout detector catches a real failure.
//
//   rifftrack_bench overdub-check [--device <name>] [--buffer <samples>] [--input <number>]
//       Needs a cable from output 1 to input 1, and plays test sweeps. Measures
//       round-trip latency, then records an overdub through the cable and
//       reports how far it landed from the part it was recorded against.

#include "rifftrack/AudioDevices.h"
#include "rifftrack/AudioEngine.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <thread>

namespace
{

constexpr double benchSampleRate = 48000.0;
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

    for (int track = 0; track < rifftrack::numTracks; ++track)
    {
        const auto copy = directory.getChildFile ("track" + juce::String (track) + ".wav");
        source.copyFileTo (copy);
        engine.loadTake (track, copy, 0);
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

    std::cout << "usage: rifftrack_bench crash-writer <file>\n"
                 "       rifftrack_bench soak <seconds> [--device <name>] [--buffer <samples>] [--record] [--stall-ms <ms>]\n"
                 "       rifftrack_bench overdub-check [--device <name>] [--buffer <samples>] [--input <number>]\n";
    return 1;
}
