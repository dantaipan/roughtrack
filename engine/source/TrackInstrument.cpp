#include "roughtrack/TrackInstrument.h"

namespace roughtrack
{

namespace
{
constexpr int midiCapacity = 4096;
} // namespace

void TrackInstrument::prepare (double sampleRate, int blockSize)
{
    preparedBlockSize = blockSize;
    chunkMidi.ensureSize (midiCapacity);

    if (instance != nullptr)
    {
        instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
        instance->prepareToPlay (sampleRate, blockSize);
    }

    updateBuffer();
}

void TrackInstrument::updateBuffer()
{
    const auto numInputs = instance != nullptr ? instance->getTotalNumInputChannels() : 0;

    numOutputs = instance != nullptr ? instance->getTotalNumOutputChannels() : 0;
    buffer.setSize (juce::jmax (2, numInputs, numOutputs), juce::jmax (1, preparedBlockSize));
}

std::unique_ptr<juce::AudioPluginInstance> TrackInstrument::exchange (std::unique_ptr<juce::AudioPluginInstance> newInstance)
{
    std::swap (instance, newInstance);
    updateBuffer();
    return newInstance;
}

int TrackInstrument::getLatencySamples() const noexcept
{
    return instance != nullptr ? instance->getLatencySamples() : 0;
}

void TrackInstrument::process (float* left, float* right, int numSamples, juce::MidiBuffer& midi) noexcept
{
    if (instance == nullptr)
        return;

    if (numSamples <= preparedBlockSize)
    {
        processChunk (left, right, numSamples, midi);
        return;
    }

    // A device can deliver more than it promised, but a plugin mustn't get it.
    for (int done = 0; done < numSamples;)
    {
        const auto chunk = juce::jmin (numSamples - done, preparedBlockSize);

        chunkMidi.clear();
        chunkMidi.addEvents (midi, done, chunk, -done);
        processChunk (left + done, right + done, chunk, chunkMidi);
        done += chunk;
    }
}

void TrackInstrument::processChunk (float* left, float* right, int numSamples, juce::MidiBuffer& midi) noexcept
{
    const juce::ScopedTryLock lock (instance->getCallbackLock());

    if (! lock.isLocked() || instance->isSuspended())
        return;

    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), numSamples);
    view.clear();

    instance->processBlock (view, midi);

    juce::FloatVectorOperations::copy (left, view.getReadPointer (0), numSamples);
    juce::FloatVectorOperations::copy (right, view.getReadPointer (numOutputs == 1 ? 0 : 1), numSamples);
}

} // namespace roughtrack
