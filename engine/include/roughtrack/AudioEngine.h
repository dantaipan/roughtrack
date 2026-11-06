#pragma once

#include "roughtrack/ChannelStrip.h"
#include "roughtrack/Clip.h"
#include "roughtrack/DropoutDetector.h"
#include "roughtrack/DrumSampler.h"
#include "roughtrack/InsertChain.h"
#include "roughtrack/LatencyCalibrator.h"
#include "roughtrack/MidiSequencer.h"
#include "roughtrack/TakeWriter.h"
#include "roughtrack/TrackInstrument.h"
#include "roughtrack/Tracks.h"
#include "roughtrack/TrackPlayer.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>

namespace roughtrack
{

enum class TransportState
{
    stopped,
    playing,
    recording
};

struct Take
{
    juce::File file;
    juce::int64 startSample = 0;
};

/** A track's mixer settings, for saving and restoring a session. */
struct TrackSettings
{
    float gain = 1.0f, pan = 0.0f;
    bool muted = false, soloed = false, monitoring = false, inverted = false;
    int firstInput = 0, inputChannels = 1;
};

/** A plugin as saved with the session. */
struct SavedInsert
{
    int track = 0;
    int slot = 0;
    bool bypassed = false;
    juce::PluginDescription description;
    juce::MemoryBlock state;
};

std::vector<SavedInsert> parseSavedInserts (const juce::XmlElement&);

struct EngineStats
{
    int dropouts = 0;
    juce::int64 timedBlocks = 0;
    juce::int64 playbackUnderrunSamples = 0;
    juce::int64 droppedRecordingSamples = 0;
};

/** What went wrong while the most recent take was being recorded. */
struct TakeHealth
{
    int dropouts = 0;
    juce::int64 droppedSamples = 0;

    bool isClean() const noexcept { return dropouts == 0 && droppedSamples == 0; }
};

/** The console: playback, overdubbing, monitoring and the click.

    Everything except processBlock() and the device callbacks belongs to the
    message thread. Transport changes take a spin lock that the audio thread
    only ever try-locks, so it can't be blocked by them.
*/
class AudioEngine final : public juce::AudioIODeviceCallback,
                          public juce::MidiInputCallback,
                          private juce::AsyncUpdater
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /** Takes are written here, and never overwrite earlier ones. */
    void setSessionDirectory (const juce::File& directory);

    void prepare (double sampleRate, int maximumBlockSize);
    void processBlock (const float* const* inputs,
                       int numInputs,
                       float* const* outputs,
                       int numOutputs,
                       int numSamples) noexcept;

    bool play();
    juce::Result record();
    void stop();

    /** The stretch playback goes round, in samples, when the cycle is on. Playback that
        reaches its end from before it jumps back to its start, and Play from past its end
        starts at its start. Recording plays straight through. Changes take effect straight
        away, even while playing.
    */
    void setCycleRange (juce::Range<juce::int64>);
    juce::Range<juce::int64> getCycleRange() const noexcept;
    void setCycleEnabled (bool);
    bool isCycleEnabled() const noexcept { return cycleEnabled.load(); }

    /** Cycles shorter than this are ignored, so a stray click can't trap the playhead. */
    static constexpr juce::int64 minimumCycleSamples = 4800;

    /** Where the last clip or drum part ends, in samples, counting loops. */
    juce::int64 getSessionLength() const;

    /** Where a clip or drum part stops, counting its loop if it's set to loop: in bars
        on a drum track, ticks on a MIDI track and samples on an audio track.
    */
    juce::int64 getLoopEnd (int track, int index) const;

    /** Where a last loop on this track runs to: the end of everything else in the song,
        with loops at their own length, in the track's units as for getLoopEnd().
    */
    juce::int64 getSongEnd (int track) const;

    struct BounceResult
    {
        juce::Result result = juce::Result::ok();
        juce::int64 numSamples = 0;
        float peak = 0.0f;
    };

    /** Mixes the session from startSample to endSample into a 24-bit stereo WAV,
        then carries on until any reverb or delay tail has died away. It runs on the
        calling thread, faster than real time, while the device plays silence. Live
        inputs, the keyboard and the click are left out. shouldContinue is asked
        between blocks with progress from 0 to 1, and returning false cancels.
    */
    BounceResult bounce (const juce::File& file,
                         juce::int64 startSample,
                         juce::int64 endSample,
                         const std::function<bool (double)>& shouldContinue = {});

    /** Like bounce(), but writes one WAV per track that can be heard, numbered in
        track order, plus one for the reverb and delay returns. They all start and
        end together and include the master fader, so they add up to the mixdown.
        The peak returned is the loudest of them.
    */
    BounceResult bounceStems (const juce::File& folder,
                              juce::int64 startSample,
                              juce::int64 endSample,
                              const std::function<bool (double)>& shouldContinue = {});

    void setPosition (juce::int64 timelinePosition);
    juce::int64 getPosition() const noexcept { return position.load(); }
    TransportState getState() const noexcept { return state.load(); }
    double getSampleRate() const noexcept { return currentSampleRate; }

    /** The rate the session's audio takes were recorded at, or 0 while it has none, when
        it simply follows the device. The first audio take sets it. Takes aren't converted,
        so while the device runs at another rate they play at the wrong speed and pitch.
    */
    void setSessionSampleRate (double rate) noexcept { sessionSampleRate = rate; }
    double getSessionSampleRate() const noexcept { return sessionSampleRate; }

    /** Empty while the device runs at the session's rate. Otherwise it says what's wrong
        and what to do. Recording audio and mixing down are refused until it's empty.
    */
    juce::String getSampleRateWarning() const;

    /** Takes the next free track, or returns -1 if the transport is running or
        every track is in use. A track keeps its number for as long as it exists.
    */
    int addTrack (const juce::String& name, TrackType = TrackType::audio);

    /** Brings a particular track into use, for restoring a session. */
    void activateTrack (int track, const juce::String& name, TrackType = TrackType::audio);

    /** Removes a track and everything on it, apart from takes already on disk. */
    juce::Result removeTrack (int track);

    /** Moves a track to another place in the running order. Its number, and so
        everything filed under it, stays the same.
    */
    void moveTrack (int track, int newPosition);
    void removeAllTracks();

    bool isTrackActive (int track) const noexcept { return tracks[(size_t) track].active.load(); }
    TrackType getTrackType (int track) const noexcept { return tracks[(size_t) track].type.load(); }
    juce::String getTrackName (int track) const { return tracks[(size_t) track].name; }
    void setTrackName (int track, const juce::String& name) { tracks[(size_t) track].name = name; }

    /** The tracks in use, in the order they're shown. */
    const std::vector<int>& getTrackOrder() const noexcept { return trackOrder; }

    void setTrackArmed (int track, bool shouldBeArmed);
    void setTrackMonitoring (int track, bool shouldMonitor);
    void setTrackInput (int track, int firstInputChannel, int numChannels);
    void setTrackGain (int track, float linearGain);
    void setTrackPan (int track, float panMinusOneToOne);
    void setTrackMuted (int track, bool shouldBeMuted);
    void setTrackSoloed (int track, bool shouldBeSoloed);

    /** Flips a track's polarity on the way through the mixer. Takes stay as recorded. */
    void setTrackInverted (int track, bool shouldBeInverted);
    bool isTrackArmed (int track) const noexcept { return tracks[(size_t) track].armed.load(); }

    TrackSettings getTrackSettings (int track) const;
    void setTrackSettings (int track, const TrackSettings&);

    /** Replaces a track's clips with this one take. */
    bool loadTake (int track, const juce::File& file, juce::int64 startSample);

    /** The newest take on a track. */
    Take getTake (int track) const;

    /** On a MIDI track these are in ticks rather than samples. */
    const ClipList& getClips (int track) const;

    /** Reads a take unless it's already in memory. Returns nullptr if it can't. */
    std::shared_ptr<const MidiTake> loadMidiTake (const juce::File&);

    /** A take that's in memory, for drawing, or nullptr. */
    const MidiTake* findMidiTake (const juce::File&) const;

    /** Writes a quantized copy of a MIDI clip's take and returns the track's clips with
        that clip using it. The original take is kept, so undoing brings it straight back.
        If the copy can't be made the clips come back as they were.
    */
    ClipList quantizeClip (int track, int clip, int gridTicks);

    /** Clips only change while stopped, because the disk thread is reading them. */
    juce::Result setClips (int track, const ClipList&);

    /** Where the drum kit and grooves are kept. */
    void setDrumFolder (const juce::File&);

    /** Loads the kit and grooves if they aren't in memory already. Safe to call
        again, and needed before a drum track can make a sound.
    */
    juce::Result loadDrums();
    const GrooveLibrary& getGrooves() const noexcept { return grooves; }

    /** Parts only change while stopped, like clips. */
    juce::Result setDrumParts (int track, const DrumPartList&);
    const DrumPartList& getDrumParts (int track) const { return tracks[(size_t) track].drumParts; }

    /** Plays a groove on a drum track so it can be heard before it's placed. It loops at
        the session's tempo through the track's channel, and the track's mute and solo are
        set aside meanwhile. While the transport runs it replaces the track's parts, lined
        up from the bar the playhead is in; while stopped it plays on its own.
    */
    void startGroovePreview (int track, const juce::String& grooveId);
    void stopGroovePreview();
    bool isPreviewingGroove() const noexcept { return previewTrack.load() >= 0; }

    /** Plugins only change while stopped, because each one moves its track's
        delay compensation, and that's set when the transport starts.
    */
    juce::Result setInsert (int track,
                            int slot,
                            std::unique_ptr<juce::AudioPluginInstance>,
                            const juce::MemoryBlock& pluginState = {});
    juce::Result removeInsert (int track, int slot);
    juce::AudioPluginInstance* getInsert (int track, int slot) const;
    void setInsertBypassed (int track, int slot, bool shouldBypass);
    bool isInsertBypassed (int track, int slot) const;
    int getInsertLatency (int track) const;
    std::unique_ptr<juce::XmlElement> createInsertsXml() const;

    ChannelStrip& getChannel (int track) { return tracks[(size_t) track].channel; }
    const ChannelStrip& getChannel (int track) const { return tracks[(size_t) track].channel; }
    AuxBuses& getAux() noexcept { return aux; }
    const AuxBuses& getAux() const noexcept { return aux; }

    /** A capture can change while playing, because it adds no latency. */
    juce::Result loadAmp (int track, const juce::File&);
    void removeAmp (int track);

    /** Loads a session's capture. One that can't load now, because the device runs at
        another rate or the file has gone, is still remembered, so saving keeps it and
        retryAmps() can load it later. Any capture the track had is removed either way.
    */
    juce::Result restoreAmp (int track, const juce::File&);

    /** Tries again to load every capture that's remembered but not running, such as after
        the device changes rate. Returns how many loaded.
    */
    int retryAmps();

    /** Puts every channel and the aux effects back to their defaults. */
    void resetChannels();

    TakeHealth getLastTakeHealth() const noexcept { return { lastTakeDropouts.load(), lastTakeDroppedSamples.load() }; }

    /** Highest post-fader level since the last call. */
    float takeTrackPeak (int track) noexcept;
    float takeMasterPeak() noexcept;

    /** Highest level at an armed audio track's input since the last call. Unlike the
        post-fader level, this works with monitoring off.
    */
    float takeTrackInputPeak (int track) noexcept;

    /** Average levels that rise and fall like a VU meter's needle. */
    float getTrackLevel (int track) const noexcept;
    float getTrackInputLevel (int track) const noexcept;
    float getMasterLevel() const noexcept;

    /** Copies an input channel for the tuner, or stops copying if it's -1. */
    void setTunerInput (int channel) noexcept;

    /** Takes what the tuner has been sent. Returns how many samples that was. */
    int readTuner (float* destination, int maximumSamples) noexcept;

    void setTempo (double beatsPerMinute);
    double getTempo() const noexcept { return tempo.load(); }
    void setClickEnabled (bool shouldClick);
    bool isClickEnabled() const noexcept { return clickEnabled.load(); }
    void setMasterGain (float linearGain);
    float getMasterGain() const noexcept { return masterGain.load(); }

    /** How far a new take is moved earlier to line up with what the player heard. */
    void setRecordingLatency (int samples);
    int getRecordingLatency() const noexcept { return recordingLatency.load(); }

    juce::Result startCalibration (int inputChannel);
    bool isCalibrationFinished() const noexcept;

    /** Ends the measurement. On success the result becomes the recording latency. */
    LatencyCalibrator::Result finishCalibration();

    EngineStats getStats() const;
    std::vector<DropoutDetector::Dropout> getDropoutLog() const { return dropoutDetector.getLog(); }

    /** Keyboards reach whichever MIDI tracks are armed. Any thread can call this. */
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    void audioDeviceIOCallbackWithContext (const float* const* inputs,
                                           int numInputs,
                                           float* const* outputs,
                                           int numOutputs,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    /** Lets plugins follow the tempo, for things like synced delays. */
    class TransportPlayHead final : public juce::AudioPlayHead
    {
    public:
        explicit TransportPlayHead (const AudioEngine& engineToFollow) : engine (engineToFollow) {}
        juce::Optional<PositionInfo> getPosition() const override;

    private:
        const AudioEngine& engine;
    };

    struct Track
    {
        std::atomic<bool> active { false };
        std::atomic<TrackType> type { TrackType::audio };
        juce::String name;
        std::atomic<bool> armed { false }, monitoring { false }, muted { false }, soloed { false }, inverted { false };
        std::atomic<int> firstInput { 0 }, inputChannels { 1 };
        std::atomic<float> gain { 1.0f }, pan { 0.0f }, peak { 0.0f }, inputPeak { 0.0f };
        std::atomic<float> levelSquared { 0.0f }, inputLevelSquared { 0.0f };

        std::unique_ptr<TrackPlayer> player;
        std::unique_ptr<TakeWriter> writer;
        InsertChain inserts;
        ChannelStrip channel;
        int recordingFirstInput = 0;
        int recordingChannels = 1;
        ClipList clips;
        DrumPartList drumParts;

        // What actually plays: the above with every loop spelled out.
        ClipList playedClips;
        DrumPartList playedParts;
        DrumSampler drums;
        TrackInstrument instrument;
        MidiSequencer sequencer;
        std::vector<MidiEvent> recordedMidi;
        bool recordingMidi = false;
        std::atomic<bool> notesNeedEnding { false };
        int playbackLatency = 0;
    };

    /** One message on its way from a keyboard to the audio thread. */
    struct LiveMidi
    {
        juce::uint8 data[3] {};
        int size = 0;
    };

    void handleAsyncUpdate() override;
    void setState (TransportState newState);
    void finishTakes();
    void prepareTracksToPlay (juce::Range<juce::int64> cycle = {});
    juce::Range<juce::int64> cycleToPlay() const noexcept;
    void restartCycle();
    void renderAcrossCycle (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept;
    juce::Result loadDrumKit();

    void processTrack (Track&, const float* const* inputs, int numInputs, int numSamples, bool anySoloed) noexcept;
    void renderMidiTrack (Track&, float* left, float* right, int numSamples, bool isRunning) noexcept;
    void renderBlock (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept;
    void waitForDisk (int numSamples) const;

    /** Where a stem bounce collects each track's output and the effect returns. */
    struct StemCapture
    {
        std::vector<juce::AudioBuffer<float>> tracks;
        juce::AudioBuffer<float> effects;
    };

    juce::Result checkCanBounce (juce::int64 startSample, juce::int64 endSample) const;
    BounceResult renderOffline (juce::int64 startSample,
                                juce::int64 endSample,
                                const std::function<bool (double)>& shouldContinue,
                                StemCapture* capture,
                                const std::function<bool (const juce::AudioBuffer<float>&, int)>& writeBlock);
    void setPluginsNonRealtime (bool isNonRealtime);
    void collectLiveMidi() noexcept;
    void endMidiTake (Track&);
    const float* inputChannel (const float* const* inputs, int numInputs, int channel) const noexcept;
    bool isOutputSilenced (const Track&, bool anySoloed) const noexcept;

    /** Where the song ends without its loops, in samples. */
    double getSongEndSamples() const;
    void refreshLoops();
    bool isPreviewing (const Track&) const noexcept;

    juce::SpinLock stateLock;
    std::atomic<TransportState> state { TransportState::stopped };
    std::atomic<bool> bouncing { false };
    StemCapture* stemCapture = nullptr;
    std::atomic<juce::int64> position { 0 };

    // What the player asked for, and what the tracks' read-ahead was prepared with. The
    // second only changes under the state lock, so the audio thread and the disk always agree.
    std::atomic<juce::int64> cycleStart { 0 }, cycleEnd { 0 };
    std::atomic<bool> cycleEnabled { false };
    std::atomic<juce::int64> activeCycleStart { 0 }, activeCycleEnd { 0 };
    juce::int64 recordingStart = 0;
    int dropoutsAtRecordingStart = 0;
    juce::int64 droppedSamplesAtRecordingStart = 0;
    std::atomic<int> lastTakeDropouts { 0 };
    std::atomic<juce::int64> lastTakeDroppedSamples { 0 };

    std::array<Track, maxTracks> tracks;
    std::vector<int> trackOrder;

    juce::AudioBuffer<float> trackBuffer, mixBuffer, silence;
    double currentSampleRate = 0.0;
    double sessionSampleRate = 0.0;
    int bufferCapacity = 0;

    std::atomic<double> tempo { 120.0 };
    std::atomic<bool> clickEnabled { false };
    std::atomic<float> masterGain { 1.0f }, masterPeak { 0.0f }, masterLevelSquared { 0.0f };
    float levelStep = 1.0f;
    std::atomic<int> tunerInput { -1 };
    static constexpr int tunerCapacity = 1 << 15;
    std::vector<float> tunerSamples = std::vector<float> ((size_t) tunerCapacity);
    juce::AbstractFifo tunerFifo { tunerCapacity };
    std::atomic<int> recordingLatency { 0 };
    std::atomic<juce::int64> droppedSamples { 0 };

    std::unique_ptr<LatencyCalibrator> calibrator;
    int calibrationInput = 0;

    DropoutDetector dropoutDetector;
    AuxBuses aux;
    TransportPlayHead playHead { *this };
    int preparedBlockSize = 0;

    static constexpr int liveMidiCapacity = 1024;
    std::array<LiveMidi, liveMidiCapacity> liveMidiQueue;
    juce::AbstractFifo liveMidiFifo { liveMidiCapacity };
    std::array<LiveMidi, liveMidiCapacity> liveMidi;
    int numLiveMidi = 0;
    juce::MidiBuffer trackMidi;
    juce::int64 midiRecordingStart = 0;
    std::map<juce::File, std::shared_ptr<const MidiTake>> midiTakes;

    juce::File sessionDirectory, drumFolder;
    GrooveLibrary grooves;
    std::shared_ptr<const DrumKit> drumKit;

    DrumSampler previewSampler;
    std::atomic<int> previewTrack { -1 };
    juce::int64 previewPosition = 0; // Only the audio thread moves it, while stopped.
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread readThread { "RoughTrack disk read" }, writeThread { "RoughTrack disk write" };

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};

} // namespace roughtrack
