#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace roughtrack
{

/** Spots dropouts from the hardware timestamp on each audio callback. A missed
    cycle shows up as a gap of at least two buffers.

    JUCE 9.0.4's AudioIODevice::getXRunCount() can't be used for this because it
    always returns -1 on macOS: its overload counter only reports while a
    `running` flag is true, and nothing ever sets that flag.
*/
class DropoutDetector
{
public:
    struct Dropout
    {
        double atSeconds = 0.0;
        double gapMilliseconds = 0.0;
    };

    void reset() noexcept;
    void blockStarted (std::uint64_t hostTimeNs, int numSamples, double sampleRate) noexcept;

    int getDropouts() const noexcept { return dropouts.load(); }

    /** How many blocks were actually timed, so that zero dropouts can't be
        mistaken for a clean result when no timestamps ever arrived.
    */
    std::int64_t getTimedBlocks() const noexcept { return timedBlocks.load(); }

    /** The earliest dropouts, in order. Only the count continues past the limit. */
    std::vector<Dropout> getLog() const;

private:
    std::atomic<int> dropouts { 0 };
    std::atomic<std::int64_t> timedBlocks { 0 };
    static constexpr int logSize = 64;
    std::array<Dropout, logSize> log;
    std::uint64_t firstHostTimeNs = 0;
    std::uint64_t lastHostTimeNs = 0;
    std::uint64_t expectedGapNs = 0;
};

} // namespace roughtrack
