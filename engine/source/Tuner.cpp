#include "rifftrack/Tuner.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace rifftrack
{

namespace
{

constexpr double referencePitch = 440.0;
constexpr int referenceNote = 69;
constexpr double centsPerOctave = 1200.0;

constexpr double windowSeconds = 0.17;
constexpr double reducedRate = 12000.0;
constexpr double lowestFrequency = 27.5;
constexpr double highestFrequency = 2000.0;

// Below this it's room noise or a string that's nearly died away.
constexpr float quietestLevel = 0.001f;

// A dip this low in the normalised difference means the signal really repeats there.
constexpr float repetitionThreshold = 0.15f;

constexpr int octaveForMiddleC = 4;

} // namespace

TunerReading readingForFrequency (double frequency) noexcept
{
    TunerReading reading;

    const auto exactNote = referenceNote + 12.0 * std::log2 (frequency / referencePitch);

    reading.hasPitch = true;
    reading.frequency = frequency;
    reading.note = juce::roundToInt (exactNote);
    reading.cents = (exactNote - reading.note) * centsPerOctave / 12.0;
    return reading;
}

juce::String getTunerNoteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, octaveForMiddleC);
}

PitchDetector::PitchDetector (double rate)
    : sampleRate (rate),
      reduction (juce::jmax (1, juce::roundToInt (rate / reducedRate))),
      windowSize (juce::roundToInt (rate * windowSeconds))
{
    reduced.resize ((size_t) (windowSize / reduction));
    difference.resize (reduced.size() / 2 + 1);
}

TunerReading PitchDetector::analyse (const float* samples)
{
    auto sum = 0.0f;

    for (int i = 0; i < windowSize; ++i)
        sum += samples[i] * samples[i];

    if (std::sqrt (sum / (float) windowSize) < quietestLevel)
        return {};

    for (size_t i = 0; i < reduced.size(); ++i)
    {
        auto total = 0.0f;

        for (int j = 0; j < reduction; ++j)
            total += samples[(int) i * reduction + j];

        reduced[i] = total / (float) reduction;
    }

    const auto coarseLag = findCoarseLag();

    if (coarseLag == 0)
        return {};

    return readingForFrequency (sampleRate / refineLag (samples, coarseLag));
}

// YIN's cumulative mean normalised difference, at the reduced rate. Taking the first
// dip rather than the deepest one keeps it from reading a note an octave down.
int PitchDetector::findCoarseLag()
{
    const auto rate = sampleRate / reduction;
    const auto highestLag = juce::jmin ((int) difference.size() - 1, juce::roundToInt (rate / lowestFrequency));
    const auto lowestLag = juce::jmax (2, juce::roundToInt (rate / highestFrequency));
    const auto length = (int) reduced.size() - highestLag;

    auto runningSum = 0.0f;

    for (int lag = 1; lag <= highestLag; ++lag)
    {
        auto total = 0.0f;

        for (int i = 0; i < length; ++i)
        {
            const auto gap = reduced[(size_t) i] - reduced[(size_t) (i + lag)];
            total += gap * gap;
        }

        runningSum += total;
        difference[(size_t) lag] = runningSum > 0.0f ? total * (float) lag / runningSum : 1.0f;
    }

    for (int lag = lowestLag; lag <= highestLag; ++lag)
    {
        if (difference[(size_t) lag] < repetitionThreshold)
        {
            while (lag < highestLag && difference[(size_t) lag + 1] < difference[(size_t) lag])
                ++lag;

            return lag;
        }
    }

    return 0;
}

// The reduced rate only finds the note. Cents need the full rate, between its samples.
double PitchDetector::refineLag (const float* samples, int coarseLag) const
{
    const auto centre = coarseLag * reduction;
    const auto firstLag = juce::jmax (2, centre - reduction - 1);
    const auto lastLag = centre + reduction + 1;
    const auto length = windowSize - lastLag - 1;

    const auto differenceAt = [samples, length] (int lag)
    {
        auto total = 0.0;

        for (int i = 0; i < length; ++i)
        {
            const auto gap = (double) samples[i] - (double) samples[i + lag];
            total += gap * gap;
        }

        return total;
    };

    auto best = firstLag;
    auto lowest = differenceAt (firstLag);

    for (int lag = firstLag + 1; lag <= lastLag; ++lag)
    {
        const auto value = differenceAt (lag);

        if (value < lowest)
        {
            lowest = value;
            best = lag;
        }
    }

    const auto before = differenceAt (best - 1);
    const auto after = differenceAt (best + 1);
    const auto curve = before - 2.0 * lowest + after;

    return curve > 0.0 ? best + 0.5 * (before - after) / curve : (double) best;
}

} // namespace rifftrack
