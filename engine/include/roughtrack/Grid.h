#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace roughtrack
{

/** What clips land on when they're moved, trimmed or split. */
enum class SnapGrid
{
    bar,
    beat,
    sixteenth,
    off
};

inline constexpr std::array<SnapGrid, 4> snapGrids { SnapGrid::bar, SnapGrid::beat, SnapGrid::sixteenth, SnapGrid::off };

/** For menus: "Bar", "Beat", "1/16", "Off". */
const char* getSnapName (SnapGrid) noexcept;

/** For saving: "bar", "beat", "sixteenth", "off". Anything else reads as a bar. */
const char* getSnapId (SnapGrid) noexcept;
SnapGrid snapFromId (const juce::String&) noexcept;

/** How far apart the lines are, given how long a beat is in the track's units, or 0 for off. */
double gridLength (SnapGrid, double unitsPerBeat) noexcept;

/** The nearest line to a position, counted from the start of the song. Each line is worked
    out from zero, so lines that don't fall on whole samples never drift.
*/
juce::int64 snapToGrid (juce::int64 position, SnapGrid, double unitsPerBeat) noexcept;

} // namespace roughtrack
