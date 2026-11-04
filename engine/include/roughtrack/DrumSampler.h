#pragma once

#include "roughtrack/DrumKit.h"

#include <array>

namespace roughtrack
{

/** Plays a track's drum parts. Like the click, it works out what to play from
    the timeline position alone, so it lands in the same place wherever the
    transport starts, and on every play through.

    setKit(), setParts() and reset() must only be called while render() isn't running.
*/
class DrumSampler
{
public:
    void setKit (const DrumKit* kitToPlay) noexcept;

    /** Parts whose groove is missing are left out. */
    void setParts (const DrumPartList&, const GrooveLibrary&);

    void reset() noexcept;

    /** Fills both channels with the drums for this stretch of timeline. */
    void render (float* left, float* right, int numSamples, juce::int64 timelinePosition, double samplesPerBeat) noexcept;

private:
    struct Part
    {
        const Groove* groove = nullptr;
        int startBar = 0;
        int bars = 0;
        int offsetBars = 0;
    };

    struct Voice
    {
        const juce::AudioBuffer<float>* audio = nullptr;
        DrumPiece piece = DrumPiece::kick;
        float gain = 0.0f;
        int position = 0;
        int startDelay = 0; // Only ever within the block the hit lands in.
        int age = 0;
        int releaseDelay = -1;
        float fade = 1.0f;
    };

    void triggerBar (int bar, int numSamples, juce::int64 timelinePosition, double samplesPerBeat) noexcept;
    void trigger (DrumPiece, float velocity, int sampleOffset, juce::uint32 seed) noexcept;
    void release (Voice&, int sampleOffset) noexcept;
    Voice& findFreeVoice() noexcept;
    void mixVoice (Voice&, float* left, float* right, int numSamples) noexcept;

    static constexpr int numVoices = 64;

    const DrumKit* kit = nullptr;
    std::vector<Part> parts;
    std::array<Voice, numVoices> voices;
    int nextAge = 0;
};

} // namespace roughtrack
