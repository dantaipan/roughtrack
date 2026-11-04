#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace nam
{
class DSP;
}

namespace roughtrack
{

/** A Neural Amp Modeler capture. Loading is slow and allocates, so it happens away
    from the audio thread. process() is real-time safe.
*/
class AmpModel
{
public:
    struct LoadResult
    {
        std::unique_ptr<AmpModel> model;
        juce::String error;
    };

    /** preferLite picks the smaller version of an A2 capture, which sounds nearly
        the same for much less CPU. Other captures only come in one size.
    */
    static LoadResult load (const juce::File&, double sampleRate, int maximumBlockSize, bool preferLite = false);

    ~AmpModel();

    /** Must not run alongside process(). A capture made at another sample rate
        stops being usable, and passes nothing through process() until it matches.
    */
    void prepare (double sampleRate, int maximumBlockSize);
    bool isUsable() const noexcept { return usable; }

    void process (float* samples, int numSamples) noexcept;

    /** False for captures that only have one size. */
    bool hasLiteVersion() const noexcept { return slimmable; }

private:
    AmpModel (std::unique_ptr<nam::DSP>, int maximumBlockSize, bool isSlimmable);

    std::unique_ptr<nam::DSP> dsp;
    std::vector<float> output;
    float outputGain = 1.0f;
    bool slimmable = false;
    bool usable = true;
};

} // namespace roughtrack
