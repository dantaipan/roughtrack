#pragma once

#include <array>

namespace rifftrack
{

inline constexpr int numTracks = 7;

inline constexpr std::array<const char*, numTracks> trackNames { "Drums",
                                                                 "Bass",
                                                                 "Guitar 1",
                                                                 "Guitar 2",
                                                                 "Guitar 3",
                                                                 "Guitar 4",
                                                                 "Vocal" };

} // namespace rifftrack
