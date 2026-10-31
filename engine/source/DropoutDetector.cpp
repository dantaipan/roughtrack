#include "rifftrack/DropoutDetector.h"

#include <algorithm>

namespace rifftrack
{

void DropoutDetector::reset() noexcept
{
    dropouts = 0;
    timedBlocks = 0;
    firstHostTimeNs = 0;
    lastHostTimeNs = 0;
}

void DropoutDetector::blockStarted (std::uint64_t hostTimeNs, int numSamples, double sampleRate) noexcept
{
    // The gap belongs to the previous block, so it's judged by that block's size.
    if (lastHostTimeNs != 0)
    {
        ++timedBlocks;

        const auto gapNs = hostTimeNs - lastHostTimeNs;

        if (gapNs > expectedGapNs + expectedGapNs / 2)
        {
            const auto count = dropouts.load();

            if (count < logSize)
                log[(size_t) count] = { (double) (hostTimeNs - firstHostTimeNs) / 1.0e9, (double) gapNs / 1.0e6 };

            dropouts = count + 1;
        }
    }
    else
    {
        firstHostTimeNs = hostTimeNs;
    }

    lastHostTimeNs = hostTimeNs;
    expectedGapNs = (std::uint64_t) (1.0e9 * numSamples / sampleRate);
}

std::vector<DropoutDetector::Dropout> DropoutDetector::getLog() const
{
    const auto count = std::min (dropouts.load(), logSize);
    return { log.begin(), log.begin() + count };
}

} // namespace rifftrack
