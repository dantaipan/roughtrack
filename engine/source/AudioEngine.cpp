#include "rifftrack/AudioEngine.h"

#include "rifftrack/Metronome.h"

#include <cmath>
#include <numbers>

namespace rifftrack
{

namespace
{
constexpr int minimumBufferCapacity = 8192;
constexpr float clickGain = 0.5f;

using ScopedStateLock = juce::SpinLock::ScopedLockType;
using ScopedStateTryLock = juce::SpinLock::ScopedTryLockType;

void raisePeak (std::atomic<float>& peak, float level) noexcept
{
    if (level > peak.load())
        peak = level;
}

void clearOutputs (float* const* outputs, int numOutputs, int numSamples) noexcept
{
    for (int channel = 0; channel < numOutputs; ++channel)
        if (outputs[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputs[channel], numSamples);
}

} // namespace

AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats();

    for (auto& track : tracks)
        track.player = std::make_unique<TrackPlayer> (readThread, formatManager);

    readThread.startThread (juce::Thread::Priority::high);
    writeThread.startThread (juce::Thread::Priority::high);
}

AudioEngine::~AudioEngine()
{
    cancelPendingUpdate();
    stop();

    for (auto& track : tracks)
        track.player.reset();

    readThread.stopThread (2000);
    writeThread.stopThread (2000);
}

void AudioEngine::setSessionDirectory (const juce::File& directory)
{
    sessionDirectory = directory;
}

void AudioEngine::prepare (double sampleRate, int maximumBlockSize)
{
    const auto capacity = juce::jmax (minimumBufferCapacity, maximumBlockSize);

    const ScopedStateLock lock (stateLock);
    currentSampleRate = sampleRate;
    bufferCapacity = capacity;
    trackBuffer.setSize (2, capacity);
    mixBuffer.setSize (2, capacity);
    silence.setSize (1, capacity);
    silence.clear();
}

void AudioEngine::processBlock (const float* const* inputs,
                                int numInputs,
                                float* const* outputs,
                                int numOutputs,
                                int numSamples) noexcept
{
    const ScopedStateTryLock lock (stateLock);

    if (! lock.isLocked() || numSamples > bufferCapacity)
    {
        clearOutputs (outputs, numOutputs, numSamples);
        return;
    }

    if (calibrator != nullptr)
    {
        clearOutputs (outputs, numOutputs, numSamples);

        if (numOutputs > 0)
            calibrator->process (inputChannel (inputs, numInputs, calibrationInput), outputs[0], numSamples);

        if (numOutputs > 1)
            juce::FloatVectorOperations::copy (outputs[1], outputs[0], numSamples);

        return;
    }

    mixBuffer.clear (0, numSamples);

    auto anySoloed = false;

    for (const auto& track : tracks)
        anySoloed = anySoloed || track.soloed.load();

    for (auto& track : tracks)
        processTrack (track, inputs, numInputs, numSamples, anySoloed);

    const auto running = state.load() != TransportState::stopped;
    const auto timeline = position.load();

    if (running && clickEnabled.load())
        addClick (mixBuffer.getWritePointer (0),
                  mixBuffer.getWritePointer (1),
                  numSamples,
                  timeline,
                  currentSampleRate,
                  tempo.load(),
                  clickGain);

    mixBuffer.applyGain (0, numSamples, masterGain.load());
    raisePeak (masterPeak, mixBuffer.getMagnitude (0, numSamples));

    clearOutputs (outputs, numOutputs, numSamples);

    for (int channel = 0; channel < juce::jmin (2, numOutputs); ++channel)
        if (outputs[channel] != nullptr)
            juce::FloatVectorOperations::copy (outputs[channel], mixBuffer.getReadPointer (channel), numSamples);

    if (running)
        position = timeline + numSamples;
}

void AudioEngine::processTrack (Track& track,
                                const float* const* inputs,
                                int numInputs,
                                int numSamples,
                                bool anySoloed) noexcept
{
    auto* left = trackBuffer.getWritePointer (0);
    auto* right = trackBuffer.getWritePointer (1);

    const auto currentState = state.load();
    const auto isRecordingTrack = currentState == TransportState::recording && track.writer != nullptr;

    if (currentState != TransportState::stopped && ! isRecordingTrack)
    {
        track.player->pull (left, right, numSamples);
    }
    else
    {
        juce::FloatVectorOperations::clear (left, numSamples);
        juce::FloatVectorOperations::clear (right, numSamples);
    }

    if (isRecordingTrack)
    {
        const float* channels[] { inputChannel (inputs, numInputs, track.recordingFirstInput),
                                  inputChannel (inputs, numInputs, track.recordingFirstInput + 1) };

        if (! track.writer->write (channels, numSamples))
            droppedSamples.fetch_add (numSamples);
    }

    if (track.armed.load() && track.monitoring.load())
    {
        const auto first = track.firstInput.load();
        const auto* inputLeft = inputChannel (inputs, numInputs, first);
        const auto* inputRight = track.inputChannels.load() > 1 ? inputChannel (inputs, numInputs, first + 1) : inputLeft;

        juce::FloatVectorOperations::add (left, inputLeft, numSamples);
        juce::FloatVectorOperations::add (right, inputRight, numSamples);
    }

    if (isOutputSilenced (track, anySoloed))
        return;

    const auto angle = (track.pan.load() + 1.0f) * std::numbers::pi_v<float> / 4.0f;
    const auto gain = track.gain.load() * std::numbers::sqrt2_v<float>;
    const auto leftGain = gain * std::cos (angle);
    const auto rightGain = gain * std::sin (angle);

    juce::FloatVectorOperations::multiply (left, leftGain, numSamples);
    juce::FloatVectorOperations::multiply (right, rightGain, numSamples);

    mixBuffer.addFrom (0, 0, left, numSamples);
    mixBuffer.addFrom (1, 0, right, numSamples);

    raisePeak (track.peak, trackBuffer.getMagnitude (0, numSamples));
}

const float* AudioEngine::inputChannel (const float* const* inputs, int numInputs, int channel) const noexcept
{
    if (channel < numInputs && inputs[channel] != nullptr)
        return inputs[channel];

    return silence.getReadPointer (0);
}

bool AudioEngine::isOutputSilenced (const Track& track, bool anySoloed) const noexcept
{
    return track.muted.load() || (anySoloed && ! track.soloed.load());
}

void AudioEngine::setState (TransportState newState)
{
    const ScopedStateLock lock (stateLock);
    state = newState;
}

void AudioEngine::prepareTracksToPlay()
{
    const auto timeline = position.load();

    for (auto& track : tracks)
        track.player->prepareToPlay (timeline);
}

bool AudioEngine::play()
{
    if (state.load() != TransportState::stopped || currentSampleRate <= 0.0 || calibrator != nullptr)
        return false;

    prepareTracksToPlay();
    setState (TransportState::playing);
    return true;
}

juce::Result AudioEngine::record()
{
    if (state.load() != TransportState::stopped)
        return juce::Result::fail ("Stop the transport first.");

    if (currentSampleRate <= 0.0)
        return juce::Result::fail ("No audio device is running.");

    if (calibrator != nullptr)
        return juce::Result::fail ("Latency calibration is running.");

    auto anyArmed = false;

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        auto& track = tracks[i];

        if (! track.armed.load())
            continue;

        const auto name = juce::String (trackNames[i]).replace (" ", "-");
        const auto file = sessionDirectory.getNonexistentChildFile (name + "-take", ".wav", false);

        track.recordingFirstInput = track.firstInput.load();
        track.recordingChannels = track.inputChannels.load();
        track.writer = TakeWriter::create (file, currentSampleRate, track.recordingChannels, writeThread);

        if (track.writer == nullptr)
        {
            for (auto& other : tracks)
                other.writer.reset();

            return juce::Result::fail ("Couldn't create " + file.getFullPathName());
        }

        anyArmed = true;
    }

    if (! anyArmed)
        return juce::Result::fail ("Arm a track first.");

    prepareTracksToPlay();
    recordingStart = position.load();
    dropoutsAtRecordingStart = dropoutDetector.getDropouts();
    droppedSamplesAtRecordingStart = droppedSamples.load();
    lastTakeDropouts = 0;
    lastTakeDroppedSamples = 0;
    setState (TransportState::recording);
    return juce::Result::ok();
}

void AudioEngine::stop()
{
    setState (TransportState::stopped);
    finishTakes();
}

void AudioEngine::finishTakes()
{
    for (auto& track : tracks)
    {
        if (track.writer == nullptr)
            continue;

        lastTakeDropouts = dropoutDetector.getDropouts() - dropoutsAtRecordingStart;
        lastTakeDroppedSamples = droppedSamples.load() - droppedSamplesAtRecordingStart;

        const auto file = track.writer->getFile();
        track.writer.reset();

        const auto start = recordingStart - recordingLatency.load();

        if (track.player->load (file, start))
            track.take = { file, start };
    }
}

void AudioEngine::setPosition (juce::int64 timelinePosition)
{
    if (state.load() == TransportState::stopped)
        position = juce::jmax<juce::int64> (0, timelinePosition);
}

void AudioEngine::setTrackArmed (int track, bool shouldBeArmed)    { tracks[(size_t) track].armed = shouldBeArmed; }
void AudioEngine::setTrackMonitoring (int track, bool shouldMonitor) { tracks[(size_t) track].monitoring = shouldMonitor; }
void AudioEngine::setTrackGain (int track, float linearGain)       { tracks[(size_t) track].gain = linearGain; }
void AudioEngine::setTrackPan (int track, float panMinusOneToOne)  { tracks[(size_t) track].pan = panMinusOneToOne; }
void AudioEngine::setTrackMuted (int track, bool shouldBeMuted)    { tracks[(size_t) track].muted = shouldBeMuted; }
void AudioEngine::setTrackSoloed (int track, bool shouldBeSoloed)  { tracks[(size_t) track].soloed = shouldBeSoloed; }

void AudioEngine::setTrackInput (int track, int firstInputChannel, int numChannels)
{
    tracks[(size_t) track].firstInput = firstInputChannel;
    tracks[(size_t) track].inputChannels = numChannels;
}

bool AudioEngine::loadTake (int track, const juce::File& file, juce::int64 startSample)
{
    auto& target = tracks[(size_t) track];

    if (state.load() != TransportState::stopped || ! target.player->load (file, startSample))
        return false;

    target.take = { file, startSample };
    return true;
}

Take AudioEngine::getTake (int track) const
{
    return tracks[(size_t) track].take;
}

float AudioEngine::takeTrackPeak (int track) noexcept
{
    return tracks[(size_t) track].peak.exchange (0.0f);
}

float AudioEngine::takeMasterPeak() noexcept
{
    return masterPeak.exchange (0.0f);
}

void AudioEngine::setTempo (double beatsPerMinute)   { tempo = juce::jlimit (30.0, 300.0, beatsPerMinute); }
void AudioEngine::setClickEnabled (bool shouldClick) { clickEnabled = shouldClick; }
void AudioEngine::setMasterGain (float linearGain)   { masterGain = linearGain; }
void AudioEngine::setRecordingLatency (int samples)  { recordingLatency = samples; }

juce::Result AudioEngine::startCalibration (int inputChannelIndex)
{
    if (state.load() != TransportState::stopped)
        return juce::Result::fail ("Stop the transport first.");

    if (currentSampleRate <= 0.0)
        return juce::Result::fail ("No audio device is running.");

    auto newCalibrator = std::make_unique<LatencyCalibrator>();
    newCalibrator->prepare (currentSampleRate);

    const ScopedStateLock lock (stateLock);
    calibrationInput = inputChannelIndex;
    calibrator = std::move (newCalibrator);
    return juce::Result::ok();
}

bool AudioEngine::isCalibrationFinished() const noexcept
{
    const ScopedStateLock lock (stateLock);
    return calibrator != nullptr && calibrator->isFinished();
}

LatencyCalibrator::Result AudioEngine::finishCalibration()
{
    std::unique_ptr<LatencyCalibrator> finished;

    {
        const ScopedStateLock lock (stateLock);
        finished = std::move (calibrator);
    }

    if (finished == nullptr || ! finished->isFinished())
        return { false, 0, "Calibration didn't finish." };

    auto result = finished->analyse();

    if (result.succeeded)
        recordingLatency = result.latencySamples;

    return result;
}

EngineStats AudioEngine::getStats() const
{
    EngineStats stats;
    stats.dropouts = dropoutDetector.getDropouts();
    stats.timedBlocks = dropoutDetector.getTimedBlocks();

    for (const auto& track : tracks)
        stats.playbackUnderrunSamples += track.player->getUnderrunSamples();

    stats.droppedRecordingSamples = droppedSamples.load();
    return stats;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs,
                                                    int numInputs,
                                                    float* const* outputs,
                                                    int numOutputs,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext& context)
{
    if (context.hostTimeNs != nullptr)
        dropoutDetector.blockStarted (*context.hostTimeNs, numSamples, currentSampleRate);

    processBlock (inputs, numInputs, outputs, numOutputs, numSamples);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* newDevice)
{
    prepare (newDevice->getCurrentSampleRate(), newDevice->getCurrentBufferSizeSamples());
    recordingLatency = newDevice->getInputLatencyInSamples() + newDevice->getOutputLatencyInSamples();
    dropoutDetector.reset();
}

void AudioEngine::audioDeviceStopped()
{
    // A device can vanish mid-take, such as when its USB cable is pulled. This
    // can run on any thread, so the take is closed on the message thread.
    setState (TransportState::stopped);
    triggerAsyncUpdate();
}

void AudioEngine::handleAsyncUpdate()
{
    finishTakes();
}

} // namespace rifftrack
