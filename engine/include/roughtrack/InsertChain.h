#pragma once

#include "roughtrack/Tracks.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>

namespace roughtrack
{

/** Where a slot's plugin sits relative to the track's amp. */
enum class InsertStage
{
    beforeAmp,  // like a pedal: guitar -> plugin -> amp
    afterAmp    // between amp and EQ, where every plugin went before this existed
};

/** The plugins on one track, processed in order on the audio thread.

    Everything except process() belongs to the message thread. The owner must
    stop the audio thread reaching process() while exchange() runs.
*/
class InsertChain
{
public:
    /** Gets a plugin ready to run before it goes anywhere near the audio thread. */
    static void configure (juce::AudioPluginInstance&,
                           double sampleRate,
                           int blockSize,
                           juce::AudioPlayHead*,
                           const juce::MemoryBlock& state);

    void prepare (double sampleRate, int blockSize);

    /** Puts a configured plugin in a slot and returns what was there, so the
        caller can delete it outside any lock.
    */
    std::unique_ptr<juce::AudioPluginInstance> exchange (int slot, std::unique_ptr<juce::AudioPluginInstance>);

    juce::AudioPluginInstance* get (int slot) const noexcept { return slots[(size_t) slot].instance.get(); }

    void setBypassed (int slot, bool shouldBypass) noexcept { slots[(size_t) slot].bypassed = shouldBypass; }
    bool isBypassed (int slot) const noexcept { return slots[(size_t) slot].bypassed.load(); }

    /** Moving a plugin across the amp changes no latency, so it can happen while playing. */
    void setStage (int slot, InsertStage stage) noexcept { slots[(size_t) slot].beforeAmp = stage == InsertStage::beforeAmp; }
    InsertStage getStage (int slot) const noexcept
    {
        return slots[(size_t) slot].beforeAmp.load() ? InsertStage::beforeAmp : InsertStage::afterAmp;
    }

    /** Combined delay of the plugins that are running. Bypassing one skips it
        entirely, so the player feels its latency go away.
    */
    int getLatencySamples() const noexcept;

    /** Runs, in slot order, the plugins placed at this stage. */
    void process (float* left, float* right, int numSamples, InsertStage) noexcept;

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        std::atomic<bool> bypassed { false };
        std::atomic<bool> beforeAmp { false };
        juce::AudioBuffer<float> buffer;
        int numInputs = 0, numOutputs = 0;
    };

    void updateBuffer (Slot&);
    void processSlot (Slot&, float* left, float* right, int numSamples) noexcept;

    std::array<Slot, numInsertSlots> slots;
    juce::MidiBuffer midi;
    int preparedBlockSize = 0;
};

} // namespace roughtrack
