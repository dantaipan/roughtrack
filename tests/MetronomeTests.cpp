#include "roughtrack/Metronome.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using roughtrack::addClick;

TEST_CASE ("The click sounds on each beat and stays silent between them")
{
    // 120 BPM at 48 kHz puts a beat every 24000 samples.
    std::vector<float> left (48000, 0.0f), right (48000, 0.0f);
    addClick (left.data(), right.data(), 48000, 0, 48000.0, 120.0, 1.0f);

    const auto energyAround = [&] (int start, int length)
    {
        auto total = 0.0f;

        for (int i = start; i < start + length; ++i)
            total += std::abs (left[(size_t) i]);

        return total;
    };

    CHECK (energyAround (0, 1000) > 1.0f);
    CHECK (energyAround (24000, 1000) > 1.0f);
    CHECK (energyAround (12000, 1000) == 0.0f);
    CHECK (left == right);
}

TEST_CASE ("The click comes out the same however it is split into blocks")
{
    std::vector<float> whole (4800, 0.0f), split (4800, 0.0f), unused (4800, 0.0f);

    addClick (whole.data(), unused.data(), 4800, 20000, 48000.0, 120.0, 1.0f);

    for (int start = 0; start < 4800; start += 64)
        addClick (split.data() + start, unused.data() + start, std::min (64, 4800 - start), 20000 + start, 48000.0, 120.0, 1.0f);

    CHECK (whole == split);
}
