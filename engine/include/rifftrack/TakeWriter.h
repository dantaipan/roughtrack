#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

namespace rifftrack
{

/** Streams one recorded take to a 24-bit WAV file from the audio thread.

    The header is rewritten about once a second, so a crash loses at most the
    last second of audio and the file stays readable.
*/
class TakeWriter
{
public:
    static std::unique_ptr<TakeWriter> create (const juce::File& file,
                                               double sampleRate,
                                               int numChannels,
                                               juce::TimeSliceThread& diskThread);

    /** Safe to call from the audio thread. Returns false if the disk has fallen
        so far behind that this block had to be dropped.
    */
    bool write (const float* const* channels, int numSamples) noexcept;

    juce::int64 getDroppedSamples() const noexcept { return droppedSamples.load(); }
    const juce::File& getFile() const noexcept { return file; }

private:
    TakeWriter (juce::File fileToWrite, std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter);

    juce::File file;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::atomic<juce::int64> droppedSamples { 0 };
};

} // namespace rifftrack
