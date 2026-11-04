#include "roughtrack/DropoutDetector.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using roughtrack::DropoutDetector;

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;
constexpr std::uint64_t blockNs = 1'333'333;
constexpr std::uint64_t startNs = 5'000'000'000;
} // namespace

TEST_CASE ("Steady callbacks aren't dropouts")
{
    DropoutDetector detector;

    for (std::uint64_t block = 0; block < 1000; ++block)
        detector.blockStarted (startNs + block * blockNs, blockSize, sampleRate);

    CHECK (detector.getDropouts() == 0);
    CHECK (detector.getTimedBlocks() == 999);
}

TEST_CASE ("Clock jitter isn't a dropout")
{
    DropoutDetector detector;

    detector.blockStarted (startNs, blockSize, sampleRate);
    detector.blockStarted (startNs + blockNs + blockNs / 4, blockSize, sampleRate);
    detector.blockStarted (startNs + 2 * blockNs, blockSize, sampleRate);

    CHECK (detector.getDropouts() == 0);
}

TEST_CASE ("A missed cycle counts as one dropout")
{
    DropoutDetector detector;

    detector.blockStarted (startNs, blockSize, sampleRate);
    detector.blockStarted (startNs + blockNs, blockSize, sampleRate);
    detector.blockStarted (startNs + 3 * blockNs, blockSize, sampleRate);
    detector.blockStarted (startNs + 4 * blockNs, blockSize, sampleRate);

    CHECK (detector.getDropouts() == 1);

    const auto log = detector.getLog();
    REQUIRE (log.size() == 1);
    CHECK (log[0].atSeconds == Catch::Approx (0.004).margin (1.0e-5));
    CHECK (log[0].gapMilliseconds == Catch::Approx (2.667).margin (1.0e-2));
}

TEST_CASE ("Without timestamps nothing is reported as timed")
{
    DropoutDetector detector;
    CHECK (detector.getTimedBlocks() == 0);
}

TEST_CASE ("Reset clears the count and doesn't judge the gap across a restart")
{
    DropoutDetector detector;

    detector.blockStarted (startNs, blockSize, sampleRate);
    detector.blockStarted (startNs + 10 * blockNs, blockSize, sampleRate);
    REQUIRE (detector.getDropouts() == 1);

    detector.reset();
    detector.blockStarted (startNs + 1000 * blockNs, blockSize, sampleRate);

    CHECK (detector.getDropouts() == 0);
    CHECK (detector.getTimedBlocks() == 0);
}
