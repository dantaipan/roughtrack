#include "rifftrack/Metronome.h"

#include <cmath>
#include <numbers>

namespace rifftrack
{

namespace
{
constexpr double clickSeconds = 0.03;
constexpr double downbeatHz = 1760.0;
constexpr double beatHz = 1320.0;
constexpr double decayPerSecond = 200.0;
constexpr std::int64_t beatsPerBar = 4;
} // namespace

void addClick (float* left,
               float* right,
               int numSamples,
               std::int64_t timelinePosition,
               double sampleRate,
               double beatsPerMinute,
               float gain) noexcept
{
    const auto samplesPerBeat = sampleRate * 60.0 / beatsPerMinute;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto position = (double) (timelinePosition + i);
        const auto beat = (std::int64_t) std::floor (position / samplesPerBeat);
        const auto seconds = (position - (double) beat * samplesPerBeat) / sampleRate;

        if (seconds >= clickSeconds)
            continue;

        const auto hz = beat % beatsPerBar == 0 ? downbeatHz : beatHz;
        const auto sample = std::sin (2.0 * std::numbers::pi * hz * seconds) * std::exp (-decayPerSecond * seconds);

        left[i] += gain * (float) sample;
        right[i] += gain * (float) sample;
    }
}

} // namespace rifftrack
