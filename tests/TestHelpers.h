#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace rifftrack::test
{

/** A temp directory removed at the end of the test. */
struct TempDirectory
{
    TempDirectory()
    {
        directory = juce::File::createTempFile ("").withFileExtension ("").getNonexistentSibling();
        directory.createDirectory();
    }

    ~TempDirectory() { directory.deleteRecursively(); }

    juce::File directory;
};

inline std::unique_ptr<juce::AudioFormatReader> openReader (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file));
}

inline juce::AudioBuffer<float> readAll (const juce::File& file)
{
    const auto reader = openReader (file);

    if (reader == nullptr)
        return {};

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer;
}

inline void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                         .withNumChannels (buffer.getNumChannels())
                                                         .withBitsPerSample (24);
    auto writer = juce::WavAudioFormat().createWriterFor (stream, options);
    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

/** Index of the sample with the largest magnitude. */
inline int findPeak (const juce::AudioBuffer<float>& buffer, int channel = 0)
{
    int best = 0;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (std::abs (buffer.getSample (channel, i)) > std::abs (buffer.getSample (channel, best)))
            best = i;

    return best;
}

} // namespace rifftrack::test
