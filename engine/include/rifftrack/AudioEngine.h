#pragma once

#include "rifftrack/DropoutDetector.h"
#include "rifftrack/LatencyCalibrator.h"
#include "rifftrack/TakeWriter.h"
#include "rifftrack/Tracks.h"
#include "rifftrack/TrackPlayer.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <memory>

namespace rifftrack
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

/** The fixed 7-track console: playback, overdubbing, monitoring and the click.

    Everything except processBlock() and the device callbacks belongs to the
    message thread. Transport changes take a spin lock that the audio thread
    only ever try-locks, so it can't be blocked by them.
*/
class AudioEngine final : public juce::AudioIODeviceCallback,
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

    void setPosition (juce::int64 timelinePosition);
    juce::int64 getPosition() const noexcept { return position.load(); }
    TransportState getState() const noexcept { return state.load(); }
    double getSampleRate() const noexcept { return currentSampleRate; }

    void setTrackArmed (int track, bool shouldBeArmed);
    void setTrackMonitoring (int track, bool shouldMonitor);
    void setTrackInput (int track, int firstInputChannel, int numChannels);
    void setTrackGain (int track, float linearGain);
    void setTrackPan (int track, float panMinusOneToOne);
    void setTrackMuted (int track, bool shouldBeMuted);
    void setTrackSoloed (int track, bool shouldBeSoloed);

    bool loadTake (int track, const juce::File& file, juce::int64 startSample);
    Take getTake (int track) const;

    TakeHealth getLastTakeHealth() const noexcept { return { lastTakeDropouts.load(), lastTakeDroppedSamples.load() }; }

    /** Highest post-fader level since the last call. */
    float takeTrackPeak (int track) noexcept;
    float takeMasterPeak() noexcept;

    void setTempo (double beatsPerMinute);
    double getTempo() const noexcept { return tempo.load(); }
    void setClickEnabled (bool shouldClick);
    void setMasterGain (float linearGain);

    /** How far a new take is moved earlier to line up with what the player heard. */
    void setRecordingLatency (int samples);
    int getRecordingLatency() const noexcept { return recordingLatency.load(); }

    juce::Result startCalibration (int inputChannel);
    bool isCalibrationFinished() const noexcept;

    /** Ends the measurement. On success the result becomes the recording latency. */
    LatencyCalibrator::Result finishCalibration();

    EngineStats getStats() const;
    std::vector<DropoutDetector::Dropout> getDropoutLog() const { return dropoutDetector.getLog(); }

    void audioDeviceIOCallbackWithContext (const float* const* inputs,
                                           int numInputs,
                                           float* const* outputs,
                                           int numOutputs,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    struct Track
    {
        std::atomic<bool> armed { false }, monitoring { false }, muted { false }, soloed { false };
        std::atomic<int> firstInput { 0 }, inputChannels { 1 };
        std::atomic<float> gain { 1.0f }, pan { 0.0f }, peak { 0.0f };

        std::unique_ptr<TrackPlayer> player;
        std::unique_ptr<TakeWriter> writer;
        int recordingFirstInput = 0;
        int recordingChannels = 1;
        Take take;
    };

    void handleAsyncUpdate() override;
    void setState (TransportState newState);
    void finishTakes();
    void prepareTracksToPlay();

    void processTrack (Track&, const float* const* inputs, int numInputs, int numSamples, bool anySoloed) noexcept;
    const float* inputChannel (const float* const* inputs, int numInputs, int channel) const noexcept;
    bool isOutputSilenced (const Track&, bool anySoloed) const noexcept;

    juce::SpinLock stateLock;
    std::atomic<TransportState> state { TransportState::stopped };
    std::atomic<juce::int64> position { 0 };
    juce::int64 recordingStart = 0;
    int dropoutsAtRecordingStart = 0;
    juce::int64 droppedSamplesAtRecordingStart = 0;
    std::atomic<int> lastTakeDropouts { 0 };
    std::atomic<juce::int64> lastTakeDroppedSamples { 0 };

    std::array<Track, numTracks> tracks;

    juce::AudioBuffer<float> trackBuffer, mixBuffer, silence;
    double currentSampleRate = 0.0;
    int bufferCapacity = 0;

    std::atomic<double> tempo { 120.0 };
    std::atomic<bool> clickEnabled { false };
    std::atomic<float> masterGain { 1.0f }, masterPeak { 0.0f };
    std::atomic<int> recordingLatency { 0 };
    std::atomic<juce::int64> droppedSamples { 0 };

    std::unique_ptr<LatencyCalibrator> calibrator;
    int calibrationInput = 0;

    DropoutDetector dropoutDetector;

    juce::File sessionDirectory;
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread readThread { "RiffTrack disk read" }, writeThread { "RiffTrack disk write" };

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};

} // namespace rifftrack
