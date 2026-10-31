#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace rifftrack
{

/** Measures true round-trip latency by playing sweeps out of the interface and
    finding them on an input joined to that output with a cable.

    Drivers often under-report latency, and an error of a few milliseconds is
    enough to make overdubs sound late.
*/
class LatencyCalibrator
{
public:
    struct Result
    {
        bool succeeded = false;
        int latencySamples = 0;
        juce::String message;
        float inputPeak = 0.0f;
        float correlation = 0.0f;
    };

    void prepare (double sampleRate);

    /** Audio thread. Writes the test signal to output and captures input. */
    void process (const float* input, float* output, int numSamples) noexcept;

    bool isFinished() const noexcept { return cursor >= (int) captured.size(); }

    /** Message thread, once isFinished() is true. */
    Result analyse() const;

private:
    std::vector<float> sweep;
    std::vector<float> captured;
    int pulsePeriod = 0;
    int cursor = 0;
};

} // namespace rifftrack
