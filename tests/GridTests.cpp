#include "roughtrack/Grid.h"
#include "roughtrack/Midi.h"

#include <catch2/catch_test_macros.hpp>

using namespace roughtrack;

namespace
{

constexpr double samplesPerBeatAt120 = 24000.0; // 48 kHz

} // namespace

TEST_CASE ("Positions snap to the nearest bar, beat or sixteenth")
{
    CHECK (snapToGrid (40000, SnapGrid::bar, samplesPerBeatAt120) == 0);
    CHECK (snapToGrid (50000, SnapGrid::bar, samplesPerBeatAt120) == 96000);
    CHECK (snapToGrid (50000, SnapGrid::beat, samplesPerBeatAt120) == 48000);
    CHECK (snapToGrid (50000, SnapGrid::sixteenth, samplesPerBeatAt120) == 48000);
    CHECK (snapToGrid (50999, SnapGrid::sixteenth, samplesPerBeatAt120) == 48000);
    CHECK (snapToGrid (51001, SnapGrid::sixteenth, samplesPerBeatAt120) == 54000);
}

TEST_CASE ("With snapping off, positions stay where they are")
{
    CHECK (snapToGrid (50017, SnapGrid::off, samplesPerBeatAt120) == 50017);
}

TEST_CASE ("A bar at 120 BPM is exactly 96000 samples")
{
    CHECK (gridLength (SnapGrid::bar, samplesPerBeatAt120) == 96000.0);
    CHECK (snapToGrid (96000 * 37 + 10, SnapGrid::bar, samplesPerBeatAt120) == 96000 * 37);
}

TEST_CASE ("Bar lines that fall between samples don't drift")
{
    // At 97 BPM a beat is 29690.72... samples, so no bar is a whole number of samples.
    const auto beat = 60.0 * 48000.0 / 97.0;
    const auto exact = 100.0 * 4.0 * beat;

    CHECK (std::abs ((double) snapToGrid ((juce::int64) exact + 5, SnapGrid::bar, beat) - exact) <= 0.5);
}

TEST_CASE ("MIDI lines are whole ticks")
{
    CHECK (gridLength (SnapGrid::sixteenth, ticksPerBeat) == 240.0);
    CHECK (snapToGrid (1000, SnapGrid::sixteenth, ticksPerBeat) == 960);
    CHECK (snapToGrid (5000, SnapGrid::bar, ticksPerBeat) == 3840);
}

TEST_CASE ("The snap choice is saved by name, and anything unknown means bars")
{
    for (const auto grid : snapGrids)
        CHECK (snapFromId (getSnapId (grid)) == grid);

    CHECK (snapFromId ({}) == SnapGrid::bar);
    CHECK (snapFromId ("nonsense") == SnapGrid::bar);
}
