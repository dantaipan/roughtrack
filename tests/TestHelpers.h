#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace roughtrack::test
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

/** Most tests just want tracks to exist. */
template <typename Engine>
void useTracks (Engine& engine, int count = 7)
{
    for (int track = 0; track < count; ++track)
        engine.activateTrack (track, "Track " + juce::String (track + 1));
}

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

/** A small kit where each recording is a single spike as loud as its level, so
    where a spike lands proves the timing and its height proves which one played.
    Every open hat rings for a second to show a closed one stopping it.

    Grooves: "beat" has a kick on 1 and a snare on 2 of bar 1, and a kick on 3 of
    bar 2. "push" has a kick just ahead of bar 1. "fill" is one bar of snare on 1.
*/
inline void writeTestDrums (const juce::File& folder, double sampleRate = 48000.0)
{
    folder.createDirectory();

    const auto writeSample = [&] (const juce::String& name, float level, int length)
    {
        juce::AudioBuffer<float> audio (2, length);
        audio.clear();

        for (int i = 0; i < length; ++i)
            for (int channel = 0; channel < 2; ++channel)
                audio.setSample (channel, i, length > 1000 ? level : (i == 0 ? level : 0.0f));

        writeWav (folder.getChildFile (name), audio, sampleRate);
    };

    writeSample ("kick-soft.wav", 0.25f, 200);
    writeSample ("kick-mid.wav", 0.5f, 200);
    writeSample ("kick-hard.wav", 1.0f, 200);
    writeSample ("snare.wav", 0.75f, 200);
    writeSample ("hat-open.wav", 0.5f, (int) sampleRate);
    writeSample ("hat-closed.wav", 0.5f, 200);

    folder.getChildFile ("kit.xml").replaceWithText (
        R"(<DRUMKIT name="Test">
             <SAMPLE piece="kick" file="kick-soft.wav" level="0.25"/>
             <SAMPLE piece="kick" file="kick-mid.wav" level="0.5"/>
             <SAMPLE piece="kick" file="kick-hard.wav" level="1.0"/>
             <SAMPLE piece="snare" file="snare.wav" level="0.75"/>
             <SAMPLE piece="hiHatOpen" file="hat-open.wav" level="0.5"/>
             <SAMPLE piece="hiHatClosed" file="hat-closed.wav" level="0.5"/>
           </DRUMKIT>)");

    folder.getChildFile ("grooves.xml").replaceWithText (
        R"(<GROOVES>
             <GROOVE id="beat" style="Rock" kind="beat" bars="2" bpm="120" name="Test beat" hits="0,0,127;1,1,127;6,0,127"/>
             <GROOVE id="push" style="Rock" kind="beat" bars="1" bpm="120" name="Test push" hits="-0.1,0,127"/>
             <GROOVE id="fill" style="Rock" kind="fill" bars="1" bpm="120" name="Test fill" hits="0,1,127"/>
           </GROOVES>)");
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

} // namespace roughtrack::test
