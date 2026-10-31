#include "TestHelpers.h"

#include "rifftrack/AudioEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <deque>

using namespace rifftrack;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

/** Drives the engine like an audio device with a loopback cable from output 1
    to input 1, so overdub timing can be checked without any hardware.
*/
class SimulatedDevice
{
public:
    SimulatedDevice (AudioEngine& engineToDrive, int loopbackDelay)
        : engine (engineToDrive), cable ((size_t) loopbackDelay, 0.0f)
    {
        engine.prepare (sampleRate, blockSize);
    }

    void run (int numBlocks)
    {
        for (int block = 0; block < numBlocks; ++block)
            processBlock();
    }

    void runCalibration()
    {
        while (! engine.isCalibrationFinished())
            processBlock();
    }

    void processBlock()
    {
        for (int i = 0; i < blockSize; ++i)
            input[(size_t) i] = cable[(size_t) i];

        const float* inputs[] { input.data() };
        float* outputs[] { outputLeft.data(), outputRight.data() };

        engine.processBlock (inputs, 1, outputs, 2, blockSize);

        cable.erase (cable.begin(), cable.begin() + blockSize);
        cable.insert (cable.end(), outputLeft.begin(), outputLeft.end());
    }

    std::vector<float> input = std::vector<float> (blockSize);
    std::vector<float> outputLeft = std::vector<float> (blockSize);
    std::vector<float> outputRight = std::vector<float> (blockSize);

private:
    AudioEngine& engine;
    std::deque<float> cable;
};

juce::File writePulseTake (const juce::File& directory, int pulsePosition, int length)
{
    juce::AudioBuffer<float> take (1, length);
    take.clear();
    take.setSample (0, pulsePosition, 0.8f);

    const auto file = directory.getChildFile ("pulse.wav");
    test::writeWav (file, take, sampleRate);
    return file;
}

juce::File writeConstantTake (const juce::File& directory, const juce::String& name, float level)
{
    juce::AudioBuffer<float> take (1, 4800);

    for (int i = 0; i < take.getNumSamples(); ++i)
        take.setSample (0, i, level);

    const auto file = directory.getChildFile (name);
    test::writeWav (file, take, sampleRate);
    return file;
}

/** Records track 1 over a pulse on track 0 and returns where it landed. */
juce::int64 overdubPulsePosition (AudioEngine& engine, SimulatedDevice& device, const juce::File& directory, int pulse)
{
    REQUIRE (engine.loadTake (0, writePulseTake (directory, pulse, 48000), 0));

    engine.setTrackArmed (1, true);
    engine.setTrackInput (1, 0, 1);

    REQUIRE (engine.record().wasOk());
    device.run (800);
    engine.stop();

    const auto take = engine.getTake (1);
    const auto recorded = test::readAll (take.file);
    REQUIRE (recorded.getNumSamples() > 0);

    return take.startSample + test::findPeak (recorded);
}

} // namespace

TEST_CASE ("An overdub lands exactly on the part it was played against")
{
    test::TempDirectory temp;
    constexpr int cableDelay = 317;
    constexpr int pulse = 24000;

    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    SimulatedDevice device (engine, cableDelay);

    SECTION ("once latency is calibrated")
    {
        REQUIRE (engine.startCalibration (0).wasOk());
        device.runCalibration();

        const auto calibration = engine.finishCalibration();
        REQUIRE (calibration.succeeded);
        CHECK (calibration.latencySamples == cableDelay);

        CHECK (overdubPulsePosition (engine, device, temp.directory, pulse) == pulse);
    }

    SECTION ("uncalibrated, it lands late by the whole round trip")
    {
        CHECK (overdubPulsePosition (engine, device, temp.directory, pulse) == pulse + cableDelay);
    }
}

TEST_CASE ("Recording refuses to start without what it needs")
{
    test::TempDirectory temp;

    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);

    SECTION ("no device")
    {
        engine.setTrackArmed (0, true);
        CHECK (engine.record().failed());
    }

    SECTION ("nothing armed")
    {
        engine.prepare (sampleRate, blockSize);
        CHECK (engine.record().failed());
        CHECK (engine.getState() == TransportState::stopped);
    }
}

TEST_CASE ("A take just recorded plays back after Return")
{
    test::TempDirectory temp;

    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    engine.prepare (sampleRate, blockSize);
    engine.setRecordingLatency (262);
    engine.setTrackArmed (0, true);
    engine.setTrackInput (0, 0, 1);

    std::vector<float> input ((size_t) blockSize, 0.25f), left ((size_t) blockSize), right ((size_t) blockSize);
    const float* inputs[] { input.data() };
    float* outputs[] { left.data(), right.data() };

    REQUIRE (engine.record().wasOk());

    for (int block = 0; block < 2000; ++block)
        engine.processBlock (inputs, 1, outputs, 2, blockSize);

    engine.stop();
    engine.setPosition (0);

    std::fill (input.begin(), input.end(), 0.0f);

    SECTION ("with the track still armed")
    {
        REQUIRE (engine.play());
    }

    SECTION ("with the track disarmed")
    {
        engine.setTrackArmed (0, false);
        REQUIRE (engine.play());
    }

    engine.processBlock (inputs, 1, outputs, 2, blockSize);

    CHECK (left[0] == Catch::Approx (0.25f).margin (1.0e-3));
    CHECK (right[0] == Catch::Approx (0.25f).margin (1.0e-3));
}

TEST_CASE ("A take stays where it was recorded on the timeline")
{
    test::TempDirectory temp;
    constexpr int blocksBeforeRecording = 1000;

    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    engine.prepare (sampleRate, blockSize);
    engine.setRecordingLatency (0);
    engine.setTrackArmed (0, true);
    engine.setTrackInput (0, 0, 1);

    std::vector<float> input ((size_t) blockSize, 0.0f), left ((size_t) blockSize), right ((size_t) blockSize);
    const float* inputs[] { input.data() };
    float* outputs[] { left.data(), right.data() };

    const auto run = [&] (int numBlocks)
    {
        for (int block = 0; block < numBlocks; ++block)
            engine.processBlock (inputs, 1, outputs, 2, blockSize);
    };

    REQUIRE (engine.play());
    run (blocksBeforeRecording);
    engine.stop();

    std::fill (input.begin(), input.end(), 0.25f);
    REQUIRE (engine.record().wasOk());
    run (500);
    engine.stop();
    std::fill (input.begin(), input.end(), 0.0f);

    CHECK (engine.getTake (0).startSample == blocksBeforeRecording * blockSize);

    engine.setPosition (0);
    REQUIRE (engine.play());

    run (1);
    CHECK (left[0] == 0.0f);

    run (blocksBeforeRecording + 3);
    CHECK (left[0] == Catch::Approx (0.25f).margin (1.0e-3));
}

TEST_CASE ("A new take never overwrites an earlier one")
{
    test::TempDirectory temp;

    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    SimulatedDevice device (engine, 64);

    engine.setTrackArmed (2, true);

    REQUIRE (engine.record().wasOk());
    device.run (10);
    engine.stop();
    const auto first = engine.getTake (2).file;

    REQUIRE (engine.record().wasOk());
    device.run (10);
    engine.stop();
    const auto second = engine.getTake (2).file;

    CHECK (first != second);
    CHECK (first.existsAsFile());
    CHECK (second.existsAsFile());
}

TEST_CASE ("A take with a dropout is flagged, and the next clean take isn't")
{
    test::TempDirectory temp;

    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    engine.prepare (sampleRate, blockSize);
    engine.setTrackArmed (0, true);

    std::vector<float> input ((size_t) blockSize), left ((size_t) blockSize), right ((size_t) blockSize);
    const float* inputs[] { input.data() };
    float* outputs[] { left.data(), right.data() };

    std::uint64_t hostTimeNs = 1'000'000'000;

    const auto callback = [&] (std::uint64_t gapNs)
    {
        hostTimeNs += gapNs;
        juce::AudioIODeviceCallbackContext context;
        context.hostTimeNs = &hostTimeNs;
        engine.audioDeviceIOCallbackWithContext (inputs, 1, outputs, 2, blockSize, context);
    };

    constexpr std::uint64_t blockNs = 1'333'333;

    REQUIRE (engine.record().wasOk());
    callback (blockNs);
    callback (blockNs);
    callback (20 * blockNs);
    callback (blockNs);
    engine.stop();

    CHECK (engine.getLastTakeHealth().dropouts == 1);
    CHECK_FALSE (engine.getLastTakeHealth().isClean());

    REQUIRE (engine.record().wasOk());
    CHECK (engine.getLastTakeHealth().isClean());

    callback (blockNs);
    callback (blockNs);
    engine.stop();

    CHECK (engine.getLastTakeHealth().isClean());
}

TEST_CASE ("Seven tracks mix with gain, mute and solo")
{
    test::TempDirectory temp;

    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    SimulatedDevice device (engine, 64);

    for (int track = 0; track < numTracks; ++track)
    {
        const auto name = "track" + juce::String (track) + ".wav";
        REQUIRE (engine.loadTake (track, writeConstantTake (temp.directory, name, 0.1f), 0));
    }

    const auto playAndMeasure = [&]
    {
        engine.setPosition (0);
        REQUIRE (engine.play());
        device.run (3);
        engine.stop();
        return device.outputLeft[10];
    };

    SECTION ("all tracks sum at unity")
    {
        CHECK (playAndMeasure() == Catch::Approx (0.7f).margin (1.0e-4));
    }

    SECTION ("gain scales one track")
    {
        engine.setTrackGain (0, 0.5f);
        CHECK (playAndMeasure() == Catch::Approx (0.65f).margin (1.0e-4));
    }

    SECTION ("mute removes one track")
    {
        engine.setTrackMuted (3, true);
        CHECK (playAndMeasure() == Catch::Approx (0.6f).margin (1.0e-4));
    }

    SECTION ("solo leaves only the soloed tracks")
    {
        engine.setTrackSoloed (1, true);
        engine.setTrackSoloed (2, true);
        CHECK (playAndMeasure() == Catch::Approx (0.2f).margin (1.0e-4));
    }

    SECTION ("a hard-panned track leaves the opposite side")
    {
        for (int track = 1; track < numTracks; ++track)
            engine.setTrackMuted (track, true);

        engine.setTrackPan (0, -1.0f);
        playAndMeasure();

        CHECK (device.outputRight[10] == Catch::Approx (0.0f).margin (1.0e-4));
        CHECK (device.outputLeft[10] > 0.1f);
    }
}

TEST_CASE ("Input monitoring needs both arm and monitor on")
{
    test::TempDirectory temp;

    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    engine.prepare (sampleRate, blockSize);

    std::vector<float> input ((size_t) blockSize, 0.25f), left ((size_t) blockSize), right ((size_t) blockSize);
    const float* inputs[] { input.data() };
    float* outputs[] { left.data(), right.data() };

    SECTION ("armed only")
    {
        engine.setTrackArmed (0, true);
        engine.processBlock (inputs, 1, outputs, 2, blockSize);
        CHECK (left[0] == 0.0f);
    }

    SECTION ("armed and monitoring")
    {
        engine.setTrackArmed (0, true);
        engine.setTrackMonitoring (0, true);
        engine.processBlock (inputs, 1, outputs, 2, blockSize);
        CHECK (left[0] == Catch::Approx (0.25f));
        CHECK (right[0] == Catch::Approx (0.25f));
    }
}

TEST_CASE ("The transport only moves while running")
{
    AudioEngine engine;
    SimulatedDevice device (engine, 64);

    device.run (10);
    CHECK (engine.getPosition() == 0);

    REQUIRE (engine.play());
    device.run (10);
    CHECK (engine.getPosition() == 640);

    engine.stop();
    device.run (10);
    CHECK (engine.getPosition() == 640);
}
