#include "rifftrack/AudioEngine.h"

#include "rifftrack/Metronome.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rifftrack
{

namespace
{
constexpr int minimumBufferCapacity = 8192;
constexpr float clickGain = 0.5f;
constexpr double fallbackSampleRate = 48000.0;
constexpr int fallbackBlockSize = 512;
constexpr int midiBufferCapacity = 8192;
constexpr int recordedMidiCapacity = 1 << 16;

// A VU needle takes 300 ms to reach 99% of a steady level, which is 4.6 of these.
constexpr double levelTimeConstantSeconds = 0.065;

using ScopedStateLock = juce::SpinLock::ScopedLockType;
using ScopedStateTryLock = juce::SpinLock::ScopedTryLockType;

void raisePeak (std::atomic<float>& peak, float level) noexcept
{
    if (level > peak.load())
        peak = level;
}

float meanSquare (const float* left, const float* right, int numSamples) noexcept
{
    auto total = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        total += left[i] * left[i] + right[i] * right[i];

    return total / (float) (2 * numSamples);
}

float absolutePeak (const float* samples, int numSamples) noexcept
{
    const auto range = juce::FloatVectorOperations::findMinAndMax (samples, numSamples);
    return juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
}

void moveLevel (std::atomic<float>& levelSquared, float target, float step) noexcept
{
    const auto current = levelSquared.load();
    levelSquared = current + (target - current) * step;
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

    {
        const ScopedStateLock lock (stateLock);
        currentSampleRate = sampleRate;
        bufferCapacity = capacity;
        preparedBlockSize = maximumBlockSize;
        trackMidi.ensureSize (midiBufferCapacity);
        trackBuffer.setSize (2, capacity);
        mixBuffer.setSize (2, capacity);
        silence.setSize (1, capacity);
        silence.clear();
    }

    for (auto& track : tracks)
    {
        track.inserts.prepare (sampleRate, maximumBlockSize);
        track.instrument.prepare (sampleRate, maximumBlockSize);
        track.channel.prepare (sampleRate, capacity);
    }

    aux.prepare (sampleRate, capacity);

    if (drumKit != nullptr && ! juce::approximatelyEqual (drumKit->getSampleRate(), sampleRate))
        loadDrumKit();
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

    levelStep = 1.0f - (float) std::exp (-numSamples / (currentSampleRate * levelTimeConstantSeconds));

    if (const auto channel = tunerInput.load(); channel >= 0 && tunerFifo.getFreeSpace() >= numSamples)
    {
        const auto* input = inputChannel (inputs, numInputs, channel);
        const auto scope = tunerFifo.write (numSamples);

        juce::FloatVectorOperations::copy (tunerSamples.data() + scope.startIndex1, input, scope.blockSize1);
        juce::FloatVectorOperations::copy (tunerSamples.data() + scope.startIndex2, input + scope.blockSize1, scope.blockSize2);
    }

    mixBuffer.clear (0, numSamples);
    aux.clearSends (numSamples);
    collectLiveMidi();

    auto anySoloed = false;

    for (const auto& track : tracks)
        anySoloed = anySoloed || track.soloed.load();

    for (auto& track : tracks)
        if (track.active.load())
            processTrack (track, inputs, numInputs, numSamples, anySoloed);

    aux.addReturns (mixBuffer.getWritePointer (0), mixBuffer.getWritePointer (1), numSamples, tempo.load());

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
    moveLevel (masterLevelSquared, meanSquare (mixBuffer.getReadPointer (0), mixBuffer.getReadPointer (1), numSamples), levelStep);

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

    if (track.type.load() == TrackType::midi)
    {
        renderMidiTrack (track, left, right, numSamples, currentState != TransportState::stopped);
    }
    else if (currentState != TransportState::stopped && track.type.load() == TrackType::drums)
    {
        track.drums.render (left, right, numSamples, position.load() + track.playbackLatency, 60.0 * currentSampleRate / tempo.load());
    }
    else if (currentState != TransportState::stopped && ! isRecordingTrack)
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

    if (track.armed.load() && track.type.load() == TrackType::audio)
    {
        const auto first = track.firstInput.load();
        const auto* inputLeft = inputChannel (inputs, numInputs, first);
        const auto* inputRight = track.inputChannels.load() > 1 ? inputChannel (inputs, numInputs, first + 1) : inputLeft;

        raisePeak (track.inputPeak, juce::jmax (absolutePeak (inputLeft, numSamples), absolutePeak (inputRight, numSamples)));
        moveLevel (track.inputLevelSquared, meanSquare (inputLeft, inputRight, numSamples), levelStep);

        if (track.monitoring.load())
        {
            juce::FloatVectorOperations::add (left, inputLeft, numSamples);
            juce::FloatVectorOperations::add (right, inputRight, numSamples);
        }
    }
    else
    {
        moveLevel (track.inputLevelSquared, 0.0f, levelStep);
    }

    if (track.inverted.load())
    {
        juce::FloatVectorOperations::negate (left, left, numSamples);
        juce::FloatVectorOperations::negate (right, right, numSamples);
    }

    // Everything keeps running on a muted track so tails and timing carry on.
    track.channel.processAmp (left, right, numSamples);
    track.inserts.process (left, right, numSamples);
    track.channel.processChannel (left, right, numSamples);

    if (isOutputSilenced (track, anySoloed))
    {
        moveLevel (track.levelSquared, 0.0f, levelStep);
        return;
    }

    const auto angle = (track.pan.load() + 1.0f) * std::numbers::pi_v<float> / 4.0f;
    const auto gain = track.gain.load() * std::numbers::sqrt2_v<float>;
    const auto leftGain = gain * std::cos (angle);
    const auto rightGain = gain * std::sin (angle);

    juce::FloatVectorOperations::multiply (left, leftGain, numSamples);
    juce::FloatVectorOperations::multiply (right, rightGain, numSamples);

    mixBuffer.addFrom (0, 0, left, numSamples);
    mixBuffer.addFrom (1, 0, right, numSamples);

    aux.addToSends (left,
                    right,
                    track.channel.getParameter (ChannelParameter::sendReverb),
                    track.channel.getParameter (ChannelParameter::sendDelay),
                    numSamples);

    raisePeak (track.peak, trackBuffer.getMagnitude (0, numSamples));
    moveLevel (track.levelSquared, meanSquare (left, right, numSamples), levelStep);
}

void AudioEngine::collectLiveMidi() noexcept
{
    numLiveMidi = 0;

    const auto ready = liveMidiFifo.read (liveMidiFifo.getNumReady());

    ready.forEach ([this] (int index) { liveMidi[(size_t) numLiveMidi++] = liveMidiQueue[(size_t) index]; });
}

void AudioEngine::renderMidiTrack (Track& track, float* left, float* right, int numSamples, bool isRunning) noexcept
{
    juce::FloatVectorOperations::clear (left, numSamples);
    juce::FloatVectorOperations::clear (right, numSamples);

    const auto samplesPerTick = 60.0 * currentSampleRate / tempo.load() / ticksPerBeat;
    const auto timeline = position.load();

    // Stopping takes the lock, so once this reads false the take is the message thread's.
    const auto isCapturing = track.recordingMidi && state.load() == TransportState::recording;

    trackMidi.clear();

    if (track.notesNeedEnding.exchange (false))
        track.sequencer.endSoundingNotes (trackMidi, 0);

    if (isRunning && ! isCapturing)
        track.sequencer.render (trackMidi, numSamples, timeline + track.playbackLatency, samplesPerTick);

    if (track.armed.load())
    {
        // The player reacts to what they hear, which left the outputs a moment ago.
        const auto heardPosition = timeline - recordingLatency.load() / 2;

        for (int i = 0; i < numLiveMidi; ++i)
        {
            const auto& message = liveMidi[(size_t) i];

            trackMidi.addEvent (message.data, message.size, 0);
            track.sequencer.noteSent (message.data);

            if (! isCapturing)
                continue;

            if (track.recordedMidi.size() < (size_t) recordedMidiCapacity)
            {
                MidiEvent event;
                event.tick = juce::jmax<juce::int64> (0, sampleToTick (heardPosition, samplesPerTick) - midiRecordingStart);
                std::copy_n (message.data, 3, event.data);
                track.recordedMidi.push_back (event);
            }
            else
            {
                droppedSamples.fetch_add (1);
            }
        }
    }

    track.instrument.process (left, right, numSamples, trackMidi);
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    const auto size = message.getRawDataSize();

    // Clock and active sensing would only fill the queue.
    if (size > 3 || size < 1 || message.getRawData()[0] >= 0xf0)
        return;

    const auto write = liveMidiFifo.write (1);

    write.forEach ([&] (int index)
    {
        auto& entry = liveMidiQueue[(size_t) index];
        std::copy_n (message.getRawData(), size, entry.data);
        entry.size = size;
    });
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

    // Reading a track ahead by its plugins' latency brings their output back in
    // time without delaying everything else, including what's being monitored.
    for (auto& track : tracks)
    {
        track.player->prepareToPlay (timeline + track.inserts.getLatencySamples());
        track.playbackLatency = track.inserts.getLatencySamples() + track.instrument.getLatencySamples();
        track.drums.reset();
        track.sequencer.rewind();
    }
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
    const auto currentState = state.load();

    if (currentState == TransportState::recording)
        return juce::Result::fail ("Already recording.");

    if (currentSampleRate <= 0.0)
        return juce::Result::fail ("No audio device is running.");

    if (calibrator != nullptr)
        return juce::Result::fail ("Latency calibration is running.");

    // Takes are created before the switch, because the audio thread waits while it happens.
    std::vector<std::pair<Track*, std::unique_ptr<TakeWriter>>> audioTakes;
    std::vector<Track*> midiTracks;

    for (auto& track : tracks)
    {
        if (! track.armed.load() || ! track.active.load() || track.type.load() == TrackType::drums)
            continue;

        if (track.type.load() == TrackType::midi)
        {
            track.recordedMidi.clear();
            track.recordedMidi.reserve ((size_t) recordedMidiCapacity);
            midiTracks.push_back (&track);
            continue;
        }

        const auto name = juce::File::createLegalFileName (track.name).replace (" ", "-");
        const auto file = sessionDirectory.getNonexistentChildFile (name + "-take", ".wav", false);
        auto writer = TakeWriter::create (file, currentSampleRate, track.inputChannels.load(), writeThread);

        if (writer == nullptr)
            return juce::Result::fail ("Couldn't create " + file.getFullPathName());

        audioTakes.emplace_back (&track, std::move (writer));
    }

    if (audioTakes.empty() && midiTracks.empty())
        return juce::Result::fail ("Arm a track first.");

    // Punching in leaves playback running; only the armed tracks switch over.
    if (currentState == TransportState::stopped)
        prepareTracksToPlay();

    dropoutsAtRecordingStart = dropoutDetector.getDropouts();
    droppedSamplesAtRecordingStart = droppedSamples.load();
    lastTakeDropouts = 0;
    lastTakeDroppedSamples = 0;

    const ScopedStateLock lock (stateLock);

    for (auto& [track, writer] : audioTakes)
    {
        track->recordingFirstInput = track->firstInput.load();
        track->recordingChannels = track->inputChannels.load();
        track->writer = std::move (writer);
    }

    // A part the track was playing would otherwise leave its notes hanging.
    for (auto* track : midiTracks)
    {
        track->recordingMidi = true;
        track->notesNeedEnding = true;
    }

    recordingStart = position.load();
    midiRecordingStart = sampleToTick (recordingStart, 60.0 * currentSampleRate / tempo.load() / ticksPerBeat);
    state = TransportState::recording;
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
        track.notesNeedEnding = true;

        if (track.recordingMidi)
            endMidiTake (track);

        if (track.writer == nullptr)
            continue;

        lastTakeDropouts = dropoutDetector.getDropouts() - dropoutsAtRecordingStart;
        lastTakeDroppedSamples = droppedSamples.load() - droppedSamplesAtRecordingStart;

        const auto file = track.writer->getFile();
        track.writer.reset();

        const std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            continue;

        Clip clip;
        clip.file = file;
        clip.timelineStart = recordingStart - recordingLatency.load();
        clip.length = reader->lengthInSamples;
        clip.fileLength = reader->lengthInSamples;

        track.clips.push_back (clip);
        track.player->setClips (track.clips);
    }
}

void AudioEngine::endMidiTake (Track& track)
{
    track.recordingMidi = false;

    if (track.recordedMidi.empty())
        return;

    const auto samplesPerTick = 60.0 * currentSampleRate / tempo.load() / ticksPerBeat;
    const auto length = juce::jmax<juce::int64> (1, sampleToTick (position.load(), samplesPerTick) - midiRecordingStart);
    const auto take = makeMidiTake (std::move (track.recordedMidi), length);

    track.recordedMidi = {};

    const auto name = juce::File::createLegalFileName (track.name).replace (" ", "-");
    const auto file = sessionDirectory.getNonexistentChildFile (name + "-take", ".mid", false);

    if (! writeMidiTake (*take, file))
        return;

    midiTakes[file] = take;

    Clip clip;
    clip.file = file;
    clip.timelineStart = midiRecordingStart;
    clip.length = take->length;
    clip.fileLength = take->length;

    auto clips = track.clips;
    clips.push_back (clip);
    setClips ((int) std::distance (tracks.data(), &track), clips);
}

std::shared_ptr<const MidiTake> AudioEngine::loadMidiTake (const juce::File& file)
{
    const auto found = midiTakes.find (file);

    if (found != midiTakes.end())
        return found->second;

    auto take = readMidiTake (file);

    if (take != nullptr)
        midiTakes[file] = take;

    return take;
}

const MidiTake* AudioEngine::findMidiTake (const juce::File& file) const
{
    const auto found = midiTakes.find (file);
    return found != midiTakes.end() ? found->second.get() : nullptr;
}

ClipList AudioEngine::quantizeClip (int track, int clip, int gridTicks)
{
    auto clips = getClips (track);
    auto& target = clips[(size_t) clip];
    const auto source = loadMidiTake (target.file);

    if (source == nullptr)
        return clips;

    const auto take = quantizeTake (*source, target.fileOffset, target.fileOffset + target.length,
                                    target.timelineStart - target.fileOffset, gridTicks);

    const auto name = target.file.getFileNameWithoutExtension().upToFirstOccurrenceOf ("-quantized", false, false);
    const auto file = sessionDirectory.getNonexistentChildFile (name + "-quantized", ".mid", false);

    if (! writeMidiTake (*take, file))
        return clips;

    midiTakes[file] = take;
    target.file = file;
    target.fileLength = take->length;
    return clips;
}

void AudioEngine::setPosition (juce::int64 timelinePosition)
{
    if (state.load() == TransportState::stopped)
        position = juce::jmax<juce::int64> (0, timelinePosition);
}

int AudioEngine::addTrack (const juce::String& name, TrackType type)
{
    if (state.load() != TransportState::stopped)
        return -1;

    for (int track = 0; track < maxTracks; ++track)
    {
        if (! isTrackActive (track))
        {
            activateTrack (track, name, type);
            return track;
        }
    }

    return -1;
}

void AudioEngine::activateTrack (int track, const juce::String& name, TrackType type)
{
    auto& target = tracks[(size_t) track];

    target.name = name;
    target.type = type;

    if (! target.active.exchange (true))
        trackOrder.push_back (track);
}

juce::Result AudioEngine::removeTrack (int track)
{
    if (state.load() != TransportState::stopped)
        return juce::Result::fail ("Stop the transport first.");

    auto& target = tracks[(size_t) track];

    target.active = false;
    target.armed = false;
    trackOrder.erase (std::remove (trackOrder.begin(), trackOrder.end(), track), trackOrder.end());

    target.clips.clear();
    target.player->setClips ({});
    target.drumParts.clear();
    target.drums.setParts ({}, grooves);
    target.type = TrackType::audio;

    for (int slot = 0; slot <= instrumentSlot; ++slot)
        removeInsert (track, slot);

    target.sequencer.setClips ({}, {});
    removeAmp (track);
    target.channel.clearCabinet();
    target.channel.resetParameters();
    setTrackSettings (track, {});
    target.name.clear();

    return juce::Result::ok();
}

void AudioEngine::removeAllTracks()
{
    const auto order = trackOrder;

    for (const auto track : order)
        removeTrack (track);
}

void AudioEngine::setTrackArmed (int track, bool shouldBeArmed)
{
    tracks[(size_t) track].armed = shouldBeArmed;
    tracks[(size_t) track].notesNeedEnding = true;
}

void AudioEngine::setTrackMonitoring (int track, bool shouldMonitor) { tracks[(size_t) track].monitoring = shouldMonitor; }
void AudioEngine::setTrackGain (int track, float linearGain)       { tracks[(size_t) track].gain = linearGain; }
void AudioEngine::setTrackPan (int track, float panMinusOneToOne)  { tracks[(size_t) track].pan = panMinusOneToOne; }
void AudioEngine::setTrackMuted (int track, bool shouldBeMuted)    { tracks[(size_t) track].muted = shouldBeMuted; }
void AudioEngine::setTrackSoloed (int track, bool shouldBeSoloed)  { tracks[(size_t) track].soloed = shouldBeSoloed; }
void AudioEngine::setTrackInverted (int track, bool shouldBeInverted) { tracks[(size_t) track].inverted = shouldBeInverted; }

void AudioEngine::setTrackInput (int track, int firstInputChannel, int numChannels)
{
    tracks[(size_t) track].firstInput = firstInputChannel;
    tracks[(size_t) track].inputChannels = numChannels;
}

TrackSettings AudioEngine::getTrackSettings (int track) const
{
    const auto& source = tracks[(size_t) track];

    TrackSettings settings;
    settings.gain = source.gain.load();
    settings.pan = source.pan.load();
    settings.muted = source.muted.load();
    settings.soloed = source.soloed.load();
    settings.monitoring = source.monitoring.load();
    settings.inverted = source.inverted.load();
    settings.firstInput = source.firstInput.load();
    settings.inputChannels = source.inputChannels.load();
    return settings;
}

void AudioEngine::setTrackSettings (int track, const TrackSettings& settings)
{
    setTrackGain (track, settings.gain);
    setTrackPan (track, settings.pan);
    setTrackMuted (track, settings.muted);
    setTrackSoloed (track, settings.soloed);
    setTrackMonitoring (track, settings.monitoring);
    setTrackInverted (track, settings.inverted);
    setTrackInput (track, settings.firstInput, settings.inputChannels);
}

bool AudioEngine::loadTake (int track, const juce::File& file, juce::int64 startSample)
{
    auto& target = tracks[(size_t) track];

    if (state.load() != TransportState::stopped)
        return false;

    const std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
        return false;

    Clip clip;
    clip.file = file;
    clip.timelineStart = startSample;
    clip.length = reader->lengthInSamples;
    clip.fileLength = reader->lengthInSamples;

    target.clips = { clip };
    return target.player->setClips (target.clips);
}

Take AudioEngine::getTake (int track) const
{
    const auto& clips = tracks[(size_t) track].clips;

    if (clips.empty())
        return {};

    return { clips.back().file, clips.back().timelineStart };
}

const ClipList& AudioEngine::getClips (int track) const
{
    return tracks[(size_t) track].clips;
}

juce::Result AudioEngine::setClips (int track, const ClipList& clips)
{
    if (state.load() != TransportState::stopped)
        return juce::Result::fail ("Stop the transport first.");

    auto& target = tracks[(size_t) track];
    target.clips = clips;

    if (target.type.load() == TrackType::midi)
    {
        std::vector<std::shared_ptr<const MidiTake>> takes;

        for (const auto& clip : clips)
            takes.push_back (loadMidiTake (clip.file));

        target.sequencer.setClips (clips, std::move (takes));
    }
    else
    {
        target.player->setClips (clips);
    }

    return juce::Result::ok();
}

void AudioEngine::setDrumFolder (const juce::File& folder)
{
    drumFolder = folder;
}

juce::Result AudioEngine::loadDrums()
{
    if (grooves.getGrooves().empty() && ! grooves.load (drumFolder.getChildFile ("grooves.xml")))
        return juce::Result::fail ("The drum grooves in " + drumFolder.getFullPathName() + " couldn't be read.");

    return drumKit != nullptr ? juce::Result::ok() : loadDrumKit();
}

juce::Result AudioEngine::loadDrumKit()
{
    auto loaded = DrumKit::load (drumFolder, currentSampleRate > 0.0 ? currentSampleRate : fallbackSampleRate);

    if (loaded.kit == nullptr)
        return juce::Result::fail (loaded.error);

    std::shared_ptr<const DrumKit> kit = std::move (loaded.kit);

    {
        const ScopedStateLock lock (stateLock);
        std::swap (drumKit, kit);

        for (auto& track : tracks)
            track.drums.setKit (drumKit.get());
    }

    return juce::Result::ok();
}

juce::Result AudioEngine::setDrumParts (int track, const DrumPartList& parts)
{
    if (state.load() != TransportState::stopped)
        return juce::Result::fail ("Stop the transport first.");

    auto& target = tracks[(size_t) track];
    target.drumParts = parts;
    target.drums.setParts (parts, grooves);
    return juce::Result::ok();
}

juce::Result AudioEngine::setInsert (int track,
                                     int slot,
                                     std::unique_ptr<juce::AudioPluginInstance> instance,
                                     const juce::MemoryBlock& pluginState)
{
    if (state.load() != TransportState::stopped)
        return juce::Result::fail ("Stop the transport first.");

    auto& target = tracks[(size_t) track];
    auto& chain = target.inserts;

    if (instance != nullptr)
        InsertChain::configure (*instance,
                                currentSampleRate > 0.0 ? currentSampleRate : fallbackSampleRate,
                                preparedBlockSize > 0 ? preparedBlockSize : fallbackBlockSize,
                                &playHead,
                                pluginState);

    std::unique_ptr<juce::AudioPluginInstance> previous;

    {
        const ScopedStateLock lock (stateLock);
        previous = slot == instrumentSlot ? target.instrument.exchange (std::move (instance))
                                          : chain.exchange (slot, std::move (instance));
    }

    return juce::Result::ok();
}

juce::Result AudioEngine::removeInsert (int track, int slot)
{
    return setInsert (track, slot, nullptr);
}

juce::Result AudioEngine::loadAmp (int track, const juce::File& file)
{
    auto loaded = AmpModel::load (file,
                                  currentSampleRate > 0.0 ? currentSampleRate : fallbackSampleRate,
                                  bufferCapacity > 0 ? bufferCapacity : fallbackBlockSize);

    if (loaded.model == nullptr)
        return juce::Result::fail (loaded.error);

    auto& channel = tracks[(size_t) track].channel;

    std::unique_ptr<AmpModel> previous;

    {
        const ScopedStateLock lock (stateLock);
        previous = channel.exchangeAmp (std::move (loaded.model));
    }

    channel.setAmpFile (file);
    return juce::Result::ok();
}

void AudioEngine::removeAmp (int track)
{
    auto& channel = tracks[(size_t) track].channel;

    std::unique_ptr<AmpModel> previous;

    {
        const ScopedStateLock lock (stateLock);
        previous = channel.exchangeAmp (nullptr);
    }

    channel.setAmpFile ({});
}

void AudioEngine::resetChannels()
{
    for (int track = 0; track < maxTracks; ++track)
    {
        removeAmp (track);
        tracks[(size_t) track].channel.clearCabinet();
        tracks[(size_t) track].channel.resetParameters();
    }

    aux.resetParameters();
}

juce::AudioPluginInstance* AudioEngine::getInsert (int track, int slot) const
{
    return slot == instrumentSlot ? tracks[(size_t) track].instrument.get() : tracks[(size_t) track].inserts.get (slot);
}

void AudioEngine::setInsertBypassed (int track, int slot, bool shouldBypass)
{
    if (slot != instrumentSlot)
        tracks[(size_t) track].inserts.setBypassed (slot, shouldBypass);
}

bool AudioEngine::isInsertBypassed (int track, int slot) const
{
    return slot != instrumentSlot && tracks[(size_t) track].inserts.isBypassed (slot);
}

int AudioEngine::getInsertLatency (int track) const
{
    return tracks[(size_t) track].inserts.getLatencySamples();
}

std::unique_ptr<juce::XmlElement> AudioEngine::createInsertsXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("INSERTS");

    for (int track = 0; track < maxTracks; ++track)
    {
        for (int slot = 0; slot <= instrumentSlot; ++slot)
        {
            auto* instance = getInsert (track, slot);

            if (instance == nullptr)
                continue;

            juce::MemoryBlock pluginState;
            instance->getStateInformation (pluginState);

            auto* element = xml->createNewChildElement ("INSERT");
            element->setAttribute ("track", track);
            element->setAttribute ("slot", slot);
            element->setAttribute ("bypassed", isInsertBypassed (track, slot));
            element->setAttribute ("state", pluginState.toBase64Encoding());
            element->addChildElement (instance->getPluginDescription().createXml().release());
        }
    }

    return xml;
}

std::vector<SavedInsert> parseSavedInserts (const juce::XmlElement& xml)
{
    std::vector<SavedInsert> inserts;

    for (auto* element : xml.getChildWithTagNameIterator ("INSERT"))
    {
        SavedInsert insert;
        insert.track = element->getIntAttribute ("track", -1);
        insert.slot = element->getIntAttribute ("slot", -1);
        insert.bypassed = element->getBoolAttribute ("bypassed");
        insert.state.fromBase64Encoding (element->getStringAttribute ("state"));

        const auto* description = element->getFirstChildElement();

        if (juce::isPositiveAndBelow (insert.track, maxTracks) && juce::isPositiveAndNotGreaterThan (insert.slot, instrumentSlot)
            && description != nullptr && insert.description.loadFromXml (*description))
            inserts.push_back (std::move (insert));
    }

    return inserts;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> AudioEngine::TransportPlayHead::getPosition() const
{
    const auto samples = engine.position.load();
    const auto currentState = engine.state.load();

    PositionInfo info;
    info.setBpm (engine.tempo.load());
    info.setTimeInSamples (samples);
    info.setIsPlaying (currentState != TransportState::stopped);
    info.setIsRecording (currentState == TransportState::recording);

    if (engine.currentSampleRate > 0.0)
    {
        const auto seconds = (double) samples / engine.currentSampleRate;
        info.setTimeInSeconds (seconds);
        info.setPpqPosition (seconds * engine.tempo.load() / 60.0);
    }

    return info;
}

float AudioEngine::takeTrackPeak (int track) noexcept
{
    return tracks[(size_t) track].peak.exchange (0.0f);
}

float AudioEngine::takeMasterPeak() noexcept
{
    return masterPeak.exchange (0.0f);
}

float AudioEngine::takeTrackInputPeak (int track) noexcept
{
    return tracks[(size_t) track].inputPeak.exchange (0.0f);
}

float AudioEngine::getTrackLevel (int track) const noexcept
{
    return std::sqrt (tracks[(size_t) track].levelSquared.load());
}

float AudioEngine::getTrackInputLevel (int track) const noexcept
{
    return std::sqrt (tracks[(size_t) track].inputLevelSquared.load());
}

float AudioEngine::getMasterLevel() const noexcept
{
    return std::sqrt (masterLevelSquared.load());
}

void AudioEngine::setTunerInput (int channel) noexcept
{
    tunerInput = channel;
}

int AudioEngine::readTuner (float* destination, int maximumSamples) noexcept
{
    const auto scope = tunerFifo.read (juce::jmin (maximumSamples, tunerFifo.getNumReady()));

    juce::FloatVectorOperations::copy (destination, tunerSamples.data() + scope.startIndex1, scope.blockSize1);
    juce::FloatVectorOperations::copy (destination + scope.blockSize1, tunerSamples.data() + scope.startIndex2, scope.blockSize2);
    return scope.blockSize1 + scope.blockSize2;
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
