#include "rifftrack/LatencyCalibrator.h"

#include <Accelerate/Accelerate.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rifftrack
{

namespace
{
constexpr int numPulses = 3;
constexpr int sweepSamples = 2048;
constexpr double startHz = 500.0;
constexpr double endHz = 12000.0;
constexpr float sweepGain = 0.25f;
constexpr float minimumCorrelation = 0.5f;
constexpr int allowedDisagreement = 2;
constexpr float silenceThreshold = 1.0e-5f;

float dotProduct (const float* a, const float* b, int numSamples) noexcept
{
    float result = 0.0f;
    vDSP_dotpr (a, 1, b, 1, &result, (vDSP_Length) numSamples);
    return result;
}

struct Peak
{
    int offset = 0;
    float correlation = 0.0f;
};

// Normalising by the local signal energy means a quiet loopback still matches,
// while hum or a loud noise doesn't pass for the sweep.
Peak findSweep (const std::vector<float>& sweep, const float* window, int numOffsets)
{
    const auto sweepLength = (int) sweep.size();
    const auto sweepEnergy = dotProduct (sweep.data(), sweep.data(), sweepLength);

    Peak best;

    for (int offset = 0; offset < numOffsets; ++offset)
    {
        const auto* segment = window + offset;
        const auto energy = dotProduct (segment, segment, sweepLength);

        if (energy <= 1.0e-9f)
            continue;

        const auto correlation = std::abs (dotProduct (sweep.data(), segment, sweepLength))
                                 / std::sqrt (sweepEnergy * energy);

        if (correlation > best.correlation)
            best = { offset, correlation };
    }

    return best;
}

} // namespace

void LatencyCalibrator::prepare (double sampleRate)
{
    sweep.resize (sweepSamples);

    const auto seconds = sweepSamples / sampleRate;
    const auto rate = (endHz - startHz) / seconds;

    for (int i = 0; i < sweepSamples; ++i)
    {
        const auto t = i / sampleRate;
        const auto phase = 2.0 * std::numbers::pi * (startHz * t + 0.5 * rate * t * t);
        const auto fade = std::sin (std::numbers::pi * (i + 0.5) / sweepSamples);
        sweep[(size_t) i] = sweepGain * (float) (fade * std::sin (phase));
    }

    pulsePeriod = (int) (sampleRate * 0.5);
    captured.assign ((size_t) (pulsePeriod * numPulses + sweepSamples), 0.0f);
    cursor = 0;
}

void LatencyCalibrator::process (const float* input, float* output, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i, ++cursor)
    {
        if (cursor >= (int) captured.size())
        {
            output[i] = 0.0f;
            continue;
        }

        const auto pulse = cursor / pulsePeriod;
        const auto offset = cursor % pulsePeriod;

        output[i] = pulse < numPulses && offset < sweepSamples ? sweep[(size_t) offset] : 0.0f;
        captured[(size_t) cursor] = input != nullptr ? input[i] : 0.0f;
    }
}

LatencyCalibrator::Result LatencyCalibrator::analyse() const
{
    Result result;

    for (auto sample : captured)
        result.inputPeak = juce::jmax (result.inputPeak, std::abs (sample));

    const auto levelText = "Input peak " + juce::String (juce::Decibels::gainToDecibels (result.inputPeak, -120.0f), 1) + " dB.";

    if (result.inputPeak < silenceThreshold)
    {
        result.message = "Nothing reached the input. Check the cable, and that the interface's output knob isn't turned all the way down. " + levelText;
        return result;
    }

    std::vector<int> measurements;

    for (int pulse = 0; pulse < numPulses; ++pulse)
    {
        const auto peak = findSweep (sweep, captured.data() + pulse * pulsePeriod, pulsePeriod);

        result.correlation = peak.correlation;

        if (peak.correlation < minimumCorrelation)
        {
            result.message = "The input has signal, but it isn't the test sweep. Connect an output to the selected input with a cable. " + levelText;
            return result;
        }

        measurements.push_back (peak.offset);
    }

    std::sort (measurements.begin(), measurements.end());

    if (measurements.back() - measurements.front() > allowedDisagreement)
    {
        result.message = "Measurements disagreed. Check the cable and try a larger buffer size.";
        return result;
    }

    result.succeeded = true;
    result.latencySamples = measurements[1];
    result.message = juce::String (result.latencySamples) + " samples";
    return result;
}

} // namespace rifftrack
