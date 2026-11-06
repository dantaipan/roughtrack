#pragma once

#include "roughtrack/Clip.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <map>
#include <memory>

namespace roughtrack
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

    /** Fills the read-ahead buffer so playback can start immediately. With a cycle, reading
        jumps back to its start on reaching its end, if it reaches the end from before it,
        so the buffer always holds what plays next. maxPrefill limits how much is read
        now, leaving the rest to the disk thread.
    */
    void prepareToPlay (juce::int64 timelinePosition, juce::Range<juce::int64> cycle = {}, int maxPrefill = -1);

    /** Copies the next samples into both channels, or silence if there are no clips. */
    void pull (float* left, float* right, int numSamples) noexcept;

    juce::int64 getUnderrunSamples() const noexcept { return underrunSamples.load(); }

    /** Whether the next numSamples are read from disk, for rendering faster than real time. */
    bool isReadyFor (int numSamples) const noexcept { return ! hasClips.load() || fifo.getNumReady() >= numSamples; }

private:
    int useTimeSlice() override;
    void fillBuffer (int maxSamples = -1);
    void render (int ringStart, int numSamples);
    void advance (juce::int64 numSamples);

    juce::TimeSliceThread& thread;
    juce::AudioFormatManager& formatManager;

    ClipList clips;
    std::map<juce::File, std::shared_ptr<juce::AudioFormatReader>> readers;
    juce::int64 readPosition = 0;
    juce::Range<juce::int64> cycle;
    juce::int64 samplesSinceCycleStart = 0;

    juce::AudioBuffer<float> ring;
    juce::AbstractFifo fifo;

    std::atomic<bool> hasClips { false };
    std::atomic<juce::int64> samplesToSkip { 0 };
    std::atomic<juce::int64> underrunSamples { 0 };
};

} // namespace roughtrack
