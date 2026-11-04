#pragma once

#include <cstdint>

namespace roughtrack
{

/** Mixes a click into a stereo pair. It holds no state between blocks, so it
    stays on the beat wherever the transport moves.
*/
void addClick (float* left,
               float* right,
               int numSamples,
               std::int64_t timelinePosition,
               double sampleRate,
               double beatsPerMinute,
               float gain) noexcept;

} // namespace roughtrack
