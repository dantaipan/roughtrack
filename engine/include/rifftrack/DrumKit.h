#pragma once

#include "rifftrack/Drums.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <memory>

namespace rifftrack
{

/** Recordings of each drum, held in memory and resampled to the session's rate
    when loaded, so playing one never touches the disk or does any resampling.
*/
class DrumKit
{
public:
    struct Choice
    {
        const juce::AudioBuffer<float>* audio = nullptr;
        float gain = 0.0f;
    };

    struct LoadResult
    {
        std::unique_ptr<DrumKit> kit;
        juce::String error;
    };

    /** Reads kit.xml and the recordings it lists from a folder. */
    static LoadResult load (const juce::File& folder, double sampleRate);

    /** The drum struck about this hard. A harder hit is a different recording,
        which is what gives a kit its tone, and the seed varies which one of
        those nearby is used, so repeated hits don't sound like a machine.
    */
    Choice pick (DrumPiece, float velocity, juce::uint32 seed) const noexcept;

    double getSampleRate() const noexcept { return sampleRate; }

private:
    struct Sample
    {
        juce::AudioBuffer<float> audio;
        float level = 0.0f;
    };

    using Samples = std::vector<Sample>;

    const Samples& samplesFor (DrumPiece) const noexcept;

    double sampleRate = 0.0;
    std::array<Samples, (size_t) DrumPiece::count> pieces;
};

} // namespace rifftrack
