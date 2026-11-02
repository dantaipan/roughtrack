#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace rifftrack
{

enum class DrumPiece
{
    kick,
    snare,
    sideStick,
    hiHatClosed,
    hiHatOpen,
    hiHatPedal,
    tomHigh,
    tomMid,
    tomLow,
    crash,
    crash2,
    ride,
    rideBell,
    count
};

const char* getPieceName (DrumPiece) noexcept;

inline constexpr int beatsPerBar = 4;

/** A drummer can land this far ahead of the beat. A hit that early belongs to the
    bar it's leading into, so a part keeps the push into its first downbeat.
*/
inline constexpr double grooveEarlyBeats = 0.125;

struct DrumHit
{
    double beat = 0.0;
    DrumPiece piece = DrumPiece::kick;
    float velocity = 1.0f;
};

/** A drummer's performance, timed in beats so it follows the session's tempo. */
struct Groove
{
    juce::String id, style, name;
    bool isFill = false;
    int bars = 0;
    int recordedBpm = 0;

    /** In time order, from grooveEarlyBeats before the first bar. */
    std::vector<DrumHit> hits;

    /** Where each bar starts in hits, plus one past the end. */
    std::vector<int> barStarts;
};

/** A groove plays at any tempo, but sounds most natural close to where it was recorded. */
bool isNearTempo (const Groove&, double beatsPerMinute) noexcept;

class GrooveLibrary
{
public:
    /** Returns false if the file can't be read. */
    bool load (const juce::File&);

    const std::vector<Groove>& getGrooves() const noexcept { return grooves; }
    const Groove* find (const juce::String& id) const noexcept;

private:
    std::vector<Groove> grooves;
};

/** A groove placed on the timeline, in whole bars. It loops if it's longer than
    the groove, and because it's counted in bars it moves with the tempo.
*/
struct DrumPart
{
    juce::String groove;
    int startBar = 0;
    int bars = 0;
    int offsetBars = 0;

    int endBar() const noexcept { return startBar + bars; }
    bool operator== (const DrumPart&) const = default;
};

/** Later parts play over earlier ones, so a fill replaces the bars it covers. */
using DrumPartList = std::vector<DrumPart>;

/** The part heard in a bar, or -1. */
int findPartAt (const DrumPartList&, int bar) noexcept;

DrumPartList movePart (DrumPartList, int index, int newStartBar);
DrumPartList trimPartStart (DrumPartList, int index, int newStartBar);
DrumPartList trimPartEnd (DrumPartList, int index, int newEndBar);
DrumPartList splitPart (DrumPartList, int index, int bar);
DrumPartList removePart (DrumPartList, int index);

} // namespace rifftrack
