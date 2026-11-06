#include "roughtrack/Grid.h"

#include "roughtrack/Drums.h"

#include <cmath>

namespace roughtrack
{

const char* getSnapName (SnapGrid grid) noexcept
{
    switch (grid)
    {
        case SnapGrid::bar:       return "Bar";
        case SnapGrid::beat:      return "Beat";
        case SnapGrid::sixteenth: return "1/16";
        case SnapGrid::off:       return "Off";
    }

    return "Bar";
}

const char* getSnapId (SnapGrid grid) noexcept
{
    switch (grid)
    {
        case SnapGrid::bar:       return "bar";
        case SnapGrid::beat:      return "beat";
        case SnapGrid::sixteenth: return "sixteenth";
        case SnapGrid::off:       return "off";
    }

    return "bar";
}

SnapGrid snapFromId (const juce::String& id) noexcept
{
    for (const auto grid : snapGrids)
        if (id == getSnapId (grid))
            return grid;

    return SnapGrid::bar;
}

double gridLength (SnapGrid grid, double unitsPerBeat) noexcept
{
    switch (grid)
    {
        case SnapGrid::bar:       return unitsPerBeat * beatsPerBar;
        case SnapGrid::beat:      return unitsPerBeat;
        case SnapGrid::sixteenth: return unitsPerBeat / 4.0;
        case SnapGrid::off:       return 0.0;
    }

    return 0.0;
}

juce::int64 snapToGrid (juce::int64 position, SnapGrid grid, double unitsPerBeat) noexcept
{
    const auto length = gridLength (grid, unitsPerBeat);

    if (length <= 0.0)
        return position;

    const auto line = std::round ((double) position / length);
    return (juce::int64) std::llround (line * length);
}

} // namespace roughtrack
