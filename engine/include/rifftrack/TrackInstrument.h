#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace rifftrack
{

/** The plugin that turns a MIDI track's notes into sound.

    Everything except process() belongs to the message thread. The owner must
    stop the audio thread reaching process() while exchange() runs.
*/
class TrackInstrument
{
public:
    void prepare (double sampleRate, int blockSize);

    /** Takes a configured plugin and returns the old one, so the caller can delete
        it outside any lock.
    */
    std::unique_ptr<juce::AudioPluginInstance> exchange (std::unique_ptr<juce::AudioPluginInstance>);

    juce::AudioPluginInstance* get() const noexcept { return instance.get(); }
    int getLatencySamples() const noexcept;

    /** Fills both channels, which must already be clear. */
    void process (float* left, float* right, int numSamples, juce::MidiBuffer&) noexcept;

private:
    void updateBuffer();
    void processChunk (float* left, float* right, int numSamples, juce::MidiBuffer&) noexcept;

    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer chunkMidi;
    int numOutputs = 0;
    int preparedBlockSize = 0;
};

} // namespace rifftrack
