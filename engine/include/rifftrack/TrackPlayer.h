#pragma once

#include "rifftrack/Clip.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <map>
#include <memory>

namespace rifftrack
{

/** Streams a track's clips from disk so the audio thread never touches the files.

    setClips(), load(), clear() and prepareToPlay() must only be called while the audio
    thread isn't calling pull().
*/
class TrackPlayer final : private juce::TimeSliceClient
{
public:
    TrackPlayer (juce::TimeSliceThread& diskThread, juce::AudioFormatManager& formats);
    ~TrackPlayer() override;

    /** startSample is where the first sample of the file sits on the timeline,
        and may be negative when latency compensation moves a take earlier.
    */
    bool load (const juce::File& file, juce::int64 startSample);
    void clear();

    /** Returns false if any clip's file can't be read. That clip plays as silence. */
    bool setClips (const ClipList&);

    /** Fills the read-ahead buffer so playback can start immediately. */
    void prepareToPlay (juce::int64 timelinePosition);

    /** Copies the next samples into both channels, or silence if there are no clips. */
    void pull (float* left, float* right, int numSamples) noexcept;

    juce::int64 getUnderrunSamples() const noexcept { return underrunSamples.load(); }

    /** Whether the next numSamples are read from disk, for rendering faster than real time. */
    bool isReadyFor (int numSamples) const noexcept { return ! hasClips.load() || fifo.getNumReady() >= numSamples; }

private:
    int useTimeSlice() override;
    void fillBuffer();
    void render (int ringStart, int numSamples);

    juce::TimeSliceThread& thread;
    juce::AudioFormatManager& formatManager;

    ClipList clips;
    std::map<juce::File, std::shared_ptr<juce::AudioFormatReader>> readers;
    juce::int64 readPosition = 0;

    juce::AudioBuffer<float> ring;
    juce::AbstractFifo fifo;

    std::atomic<bool> hasClips { false };
    std::atomic<juce::int64> samplesToSkip { 0 };
    std::atomic<juce::int64> underrunSamples { 0 };
};

} // namespace rifftrack
