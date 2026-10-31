#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

namespace rifftrack
{

/** Streams one take from disk so the audio thread never touches the file.

    load(), clear() and prepareToPlay() must only be called while the audio
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

    /** Fills the read-ahead buffer so playback can start immediately. */
    void prepareToPlay (juce::int64 timelinePosition);

    /** Copies the next samples into both channels, or silence if there's no take. */
    void pull (float* left, float* right, int numSamples) noexcept;

    juce::int64 getUnderrunSamples() const noexcept { return underrunSamples.load(); }

private:
    int useTimeSlice() override;
    void fillBuffer();
    void render (int ringStart, int numSamples);

    juce::TimeSliceThread& thread;
    juce::AudioFormatManager& formatManager;

    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::int64 takeStart = 0;
    juce::int64 readPosition = 0;

    juce::AudioBuffer<float> ring;
    juce::AbstractFifo fifo;

    std::atomic<bool> hasTake { false };
    std::atomic<juce::int64> samplesToSkip { 0 };
    std::atomic<juce::int64> underrunSamples { 0 };
};

} // namespace rifftrack
