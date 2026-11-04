#include "roughtrack/LatencyCalibrator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <deque>
#include <random>

using roughtrack::LatencyCalibrator;

namespace
{

constexpr int blockSize = 64;

/** Runs a measurement with the output fed back through a delay line. */
LatencyCalibrator::Result measure (int delay, float loopGain, float noiseLevel)
{
    LatencyCalibrator calibrator;
    calibrator.prepare (48000.0);

    std::deque<float> cable ((size_t) delay, 0.0f);
    std::mt19937 random (1234);
    std::uniform_real_distribution<float> noise (-noiseLevel, noiseLevel);

    std::vector<float> input (blockSize), output (blockSize);

    while (! calibrator.isFinished())
    {
        for (int i = 0; i < blockSize; ++i)
            input[(size_t) i] = cable[(size_t) i] * loopGain + noise (random);

        calibrator.process (input.data(), output.data(), blockSize);

        cable.erase (cable.begin(), cable.begin() + blockSize);
        cable.insert (cable.end(), output.begin(), output.end());
    }

    return calibrator.analyse();
}

} // namespace

TEST_CASE ("The calibrator measures the exact delay of a loopback")
{
    const auto delay = GENERATE (64, 165, 317, 1024, 4800);

    const auto result = measure (delay, 1.0f, 0.0f);

    CHECK (result.succeeded);
    CHECK (result.latencySamples == delay);
}

TEST_CASE ("The calibrator copes with a quiet, inverted, noisy loopback")
{
    const auto result = measure (317, -0.05f, 0.002f);

    CHECK (result.succeeded);
    CHECK (result.latencySamples == 317);
}

TEST_CASE ("The calibrator fails rather than guessing when nothing comes back")
{
    SECTION ("silence")
    {
        CHECK_FALSE (measure (317, 0.0f, 0.0f).succeeded);
    }

    SECTION ("noise with no cable connected")
    {
        CHECK_FALSE (measure (317, 0.0f, 0.1f).succeeded);
    }
}
