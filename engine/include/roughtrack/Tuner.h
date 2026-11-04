#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace roughtrack
{

struct TunerReading
{
    bool hasPitch = false;
    double frequency = 0.0;

    /** The nearest note as a MIDI number, and how far off it the string is. */
    int note = 0;
    double cents = 0.0;
};

/** The note and cents for a frequency, at A = 440 Hz. */
TunerReading readingForFrequency (double frequency) noexcept;

/** "E2", with middle C as C4. */
juce::String getTunerNoteName (int note);

/** Finds the pitch of a played string, from a five-string bass's low B up past the
    top of a guitar's neck. It's too slow for the audio thread, so run it from a timer.
*/
class PitchDetector
{
public:
    explicit PitchDetector (double sampleRate);

    /** How many samples each call to analyse() needs. */
    int getWindowSize() const noexcept { return windowSize; }

    TunerReading analyse (const float* samples);

private:
    int findCoarseLag();
    double refineLag (const float* samples, int coarseLag) const;

    double sampleRate;
    int reduction, windowSize;
    std::vector<float> reduced, difference;
};

} // namespace roughtrack
