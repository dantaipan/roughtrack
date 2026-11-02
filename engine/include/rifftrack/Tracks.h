#pragma once

#include <array>

namespace rifftrack
{

/** Tracks are reserved up front so adding one never touches the audio thread. */
inline constexpr int maxTracks = 24;
inline constexpr int numInsertSlots = 4;

/** A MIDI track's instrument is saved and loaded like one more plugin slot. */
inline constexpr int instrumentSlot = numInsertSlots;

enum class TrackType
{
    audio,
    drums,
    midi
};

/** Sessions saved when there were seven fixed tracks don't store names. */
inline constexpr std::array<const char*, 7> legacyTrackNames { "Drums",
                                                               "Bass",
                                                               "Guitar 1",
                                                               "Guitar 2",
                                                               "Guitar 3",
                                                               "Guitar 4",
                                                               "Vocal" };

} // namespace rifftrack
