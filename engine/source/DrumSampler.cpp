#include "rifftrack/DrumSampler.h"

#include <cmath>

namespace rifftrack
{

namespace
{

constexpr int maximumVoicesPerPiece = 4;
constexpr float releaseSamples = 480.0f;
constexpr int barSeedStep = 1009;

/** A closed hi-hat stops an open one ringing, as it does on a real stand. */
bool chokes (DrumPiece struck, DrumPiece ringing) noexcept
{
    return ringing == DrumPiece::hiHatOpen && (struck == DrumPiece::hiHatClosed || struck == DrumPiece::hiHatPedal);
}

} // namespace

void DrumSampler::setKit (const DrumKit* kitToPlay) noexcept
{
    kit = kitToPlay;
    reset();
}

void DrumSampler::setParts (const DrumPartList& partsToPlay, const GrooveLibrary& library)
{
    parts.clear();

    for (const auto& part : partsToPlay)
        if (const auto* groove = library.find (part.groove))
            parts.push_back ({ groove, part.startBar, part.bars, part.offsetBars });
}

void DrumSampler::reset() noexcept
{
    for (auto& voice : voices)
        voice.audio = nullptr;
}

void DrumSampler::render (float* left, float* right, int numSamples, juce::int64 timelinePosition, double samplesPerBeat) noexcept
{
    juce::FloatVectorOperations::clear (left, numSamples);
    juce::FloatVectorOperations::clear (right, numSamples);

    if (kit == nullptr)
        return;

    const auto barOf = [samplesPerBeat] (juce::int64 sample)
    {
        return (int) std::floor (((double) sample / samplesPerBeat + grooveEarlyBeats) / beatsPerBar);
    };

    // A hit rounded up onto this block can still belong to the bar before.
    const auto lastBar = barOf (timelinePosition + numSamples);

    for (auto bar = barOf (timelinePosition - 1); bar <= lastBar; ++bar)
        triggerBar (bar, numSamples, timelinePosition, samplesPerBeat);

    for (auto& voice : voices)
        if (voice.audio != nullptr)
            mixVoice (voice, left, right, numSamples);
}

void DrumSampler::triggerBar (int bar, int numSamples, juce::int64 timelinePosition, double samplesPerBeat) noexcept
{
    const Part* part = nullptr;

    for (auto i = (int) parts.size() - 1; i >= 0 && part == nullptr; --i)
        if (bar >= parts[(size_t) i].startBar && bar < parts[(size_t) i].startBar + parts[(size_t) i].bars)
            part = &parts[(size_t) i];

    if (part == nullptr)
        return;

    const auto& groove = *part->groove;
    const auto barInGroove = juce::negativeAwareModulo (bar - part->startBar + part->offsetBars, groove.bars);
    const auto beatsToGrooveStart = (bar - barInGroove) * beatsPerBar;

    for (auto i = groove.barStarts[(size_t) barInGroove]; i < groove.barStarts[(size_t) barInGroove + 1]; ++i)
    {
        const auto& hit = groove.hits[(size_t) i];
        const auto sample = (juce::int64) std::llround ((hit.beat + beatsToGrooveStart) * samplesPerBeat);

        // Rounding to a whole sample means a hit can't fall between two blocks.
        if (sample >= timelinePosition && sample < timelinePosition + numSamples)
            trigger (hit.piece, hit.velocity, (int) (sample - timelinePosition), (juce::uint32) (bar * barSeedStep + i));
    }
}

void DrumSampler::trigger (DrumPiece piece, float velocity, int sampleOffset, juce::uint32 seed) noexcept
{
    const auto choice = kit->pick (piece, velocity, seed);

    if (choice.audio == nullptr)
        return;

    Voice* oldest = nullptr;
    auto ringing = 0;

    for (auto& voice : voices)
    {
        if (voice.audio == nullptr || voice.releaseDelay >= 0)
            continue;

        if (chokes (piece, voice.piece))
            release (voice, sampleOffset);

        if (voice.piece == piece && voice.releaseDelay < 0)
        {
            ++ringing;

            if (oldest == nullptr || voice.age < oldest->age)
                oldest = &voice;
        }
    }

    if (ringing >= maximumVoicesPerPiece)
        release (*oldest, sampleOffset);

    auto& voice = findFreeVoice();
    voice.audio = choice.audio;
    voice.piece = piece;
    voice.gain = choice.gain;
    voice.position = 0;
    voice.startDelay = sampleOffset;
    voice.age = nextAge++;
    voice.releaseDelay = -1;
    voice.fade = 1.0f;
}

void DrumSampler::release (Voice& voice, int sampleOffset) noexcept
{
    voice.releaseDelay = sampleOffset;
}

DrumSampler::Voice& DrumSampler::findFreeVoice() noexcept
{
    auto* oldest = &voices[0];

    for (auto& voice : voices)
    {
        if (voice.audio == nullptr)
            return voice;

        if (voice.age < oldest->age)
            oldest = &voice;
    }

    return *oldest;
}

void DrumSampler::mixVoice (Voice& voice, float* left, float* right, int numSamples) noexcept
{
    const auto start = voice.startDelay;
    const auto count = juce::jmin (numSamples - start, voice.audio->getNumSamples() - voice.position);

    voice.startDelay = 0;

    const auto* sourceLeft = voice.audio->getReadPointer (0) + voice.position;
    const auto* sourceRight = voice.audio->getReadPointer (1) + voice.position;

    if (voice.releaseDelay < 0)
    {
        juce::FloatVectorOperations::addWithMultiply (left + start, sourceLeft, voice.gain, count);
        juce::FloatVectorOperations::addWithMultiply (right + start, sourceRight, voice.gain, count);
    }
    else
    {
        for (int i = 0; i < count && voice.fade > 0.0f; ++i)
        {
            const auto outputIndex = start + i;

            if (outputIndex >= voice.releaseDelay)
                voice.fade = juce::jmax (0.0f, voice.fade - 1.0f / releaseSamples);

            const auto gain = voice.gain * voice.fade;
            left[outputIndex] += sourceLeft[i] * gain;
            right[outputIndex] += sourceRight[i] * gain;
        }

        voice.releaseDelay = juce::jmax (0, voice.releaseDelay - numSamples);
    }

    voice.position += count;

    if (voice.position >= voice.audio->getNumSamples() || voice.fade <= 0.0f)
        voice.audio = nullptr;
}

} // namespace rifftrack
