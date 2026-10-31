#include "rifftrack/InsertChain.h"

namespace rifftrack
{

namespace
{

constexpr int midiCapacity = 4096;

void chooseLayout (juce::AudioPluginInstance& instance)
{
    instance.disableNonMainBuses();

    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const std::array<std::pair<juce::AudioChannelSet, juce::AudioChannelSet>, 3> layouts { { { stereo, stereo },
                                                                                             { mono, stereo },
                                                                                             { mono, mono } } };

    for (const auto& [input, output] : layouts)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (input);
        layout.outputBuses.add (output);

        if (instance.setBusesLayout (layout))
            return;
    }
}

} // namespace

void InsertChain::configure (juce::AudioPluginInstance& instance,
                             double sampleRate,
                             int blockSize,
                             juce::AudioPlayHead* playHead,
                             const juce::MemoryBlock& state)
{
    chooseLayout (instance);

    instance.setPlayHead (playHead);
    instance.setNonRealtime (false);
    instance.setRateAndBufferSizeDetails (sampleRate, blockSize);
    instance.prepareToPlay (sampleRate, blockSize);

    if (! state.isEmpty())
        instance.setStateInformation (state.getData(), (int) state.getSize());
}

void InsertChain::prepare (double sampleRate, int blockSize)
{
    preparedBlockSize = blockSize;
    midi.ensureSize (midiCapacity);

    for (auto& slot : slots)
    {
        if (slot.instance != nullptr)
        {
            slot.instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
            slot.instance->prepareToPlay (sampleRate, blockSize);
        }

        updateBuffer (slot);
    }
}

void InsertChain::updateBuffer (Slot& slot)
{
    slot.numInputs = slot.instance != nullptr ? slot.instance->getTotalNumInputChannels() : 0;
    slot.numOutputs = slot.instance != nullptr ? slot.instance->getTotalNumOutputChannels() : 0;
    slot.buffer.setSize (juce::jmax (2, slot.numInputs, slot.numOutputs), juce::jmax (1, preparedBlockSize));
}

std::unique_ptr<juce::AudioPluginInstance> InsertChain::exchange (int slotIndex, std::unique_ptr<juce::AudioPluginInstance> instance)
{
    auto& slot = slots[(size_t) slotIndex];

    std::swap (slot.instance, instance);
    slot.bypassed = false;
    updateBuffer (slot);

    return instance;
}

int InsertChain::getLatencySamples() const noexcept
{
    auto total = 0;

    for (const auto& slot : slots)
        if (slot.instance != nullptr && ! slot.bypassed.load())
            total += slot.instance->getLatencySamples();

    return total;
}

void InsertChain::process (float* left, float* right, int numSamples) noexcept
{
    for (auto& slot : slots)
    {
        if (slot.instance == nullptr || slot.bypassed.load())
            continue;

        // A device can deliver more than it promised, but a plugin mustn't get it.
        for (int done = 0; done < numSamples;)
        {
            const auto chunk = juce::jmin (numSamples - done, preparedBlockSize);
            processSlot (slot, left + done, right + done, chunk);
            done += chunk;
        }
    }
}

void InsertChain::processSlot (Slot& slot, float* left, float* right, int numSamples) noexcept
{
    const juce::ScopedTryLock lock (slot.instance->getCallbackLock());

    // Silence rather than dry signal: a plugin that's mid-update may be the amp,
    // and an unamped guitar jumping out for one block is a loud click.
    if (! lock.isLocked() || slot.instance->isSuspended())
    {
        juce::FloatVectorOperations::clear (left, numSamples);
        juce::FloatVectorOperations::clear (right, numSamples);
        return;
    }

    auto* channels = slot.buffer.getArrayOfWritePointers();

    for (int channel = 2; channel < slot.buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::clear (channels[channel], numSamples);

    if (slot.numInputs == 1)
    {
        juce::FloatVectorOperations::copyWithMultiply (channels[0], left, 0.5f, numSamples);
        juce::FloatVectorOperations::addWithMultiply (channels[0], right, 0.5f, numSamples);
    }
    else
    {
        juce::FloatVectorOperations::copy (channels[0], left, numSamples);
        juce::FloatVectorOperations::copy (channels[1], right, numSamples);
    }

    juce::AudioBuffer<float> view (channels, slot.buffer.getNumChannels(), numSamples);
    midi.clear();
    slot.instance->processBlock (view, midi);

    juce::FloatVectorOperations::copy (left, channels[0], numSamples);
    juce::FloatVectorOperations::copy (right, channels[slot.numOutputs == 1 ? 0 : 1], numSamples);
}

} // namespace rifftrack
