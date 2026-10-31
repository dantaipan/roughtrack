#include "rifftrack/TakeWriter.h"

namespace rifftrack
{

namespace
{
constexpr double fifoSeconds = 10.0;
constexpr double flushIntervalSeconds = 1.0;
constexpr int bitsPerSample = 24;
} // namespace

std::unique_ptr<TakeWriter> TakeWriter::create (const juce::File& file,
                                                double sampleRate,
                                                int numChannels,
                                                juce::TimeSliceThread& diskThread)
{
    if (file.getParentDirectory().createDirectory().failed())
        return nullptr;

    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

    if (stream == nullptr)
        return nullptr;

    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                         .withNumChannels (numChannels)
                                                         .withBitsPerSample (bitsPerSample);

    auto writer = juce::WavAudioFormat().createWriterFor (stream, options);

    if (writer == nullptr)
        return nullptr;

    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(),
                                                                               diskThread,
                                                                               (int) (sampleRate * fifoSeconds));
    threaded->setFlushInterval ((int) (sampleRate * flushIntervalSeconds));

    return std::unique_ptr<TakeWriter> (new TakeWriter (file, std::move (threaded)));
}

TakeWriter::TakeWriter (juce::File fileToWrite, std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter)
    : file (std::move (fileToWrite)), writer (std::move (threadedWriter))
{
}

bool TakeWriter::write (const float* const* channels, int numSamples) noexcept
{
    if (writer->write (channels, numSamples))
        return true;

    droppedSamples.fetch_add (numSamples);
    return false;
}

} // namespace rifftrack
