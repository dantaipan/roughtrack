#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <optional>
#include <vector>

namespace roughtrack
{

/** What a keyboard's transport and track buttons ask for. */
enum class ControlCommand
{
    play,
    stop,
    record,
    rewind,
    fastForward,
    previousTrack,
    nextTrack,
    toggleCycle,

    // The buttons under a surface's faders, each for one track of the bank.
    armStrip,
    soloStrip,
    muteStrip,
    selectStrip
};

/** A command, and for the fader buttons, which of the bank's strips it's for. */
struct ControlEvent
{
    ControlCommand command;
    int strip = 0;

    bool operator== (const ControlEvent&) const = default;
};

/** How many tracks a Mackie surface's fader buttons cover at once. */
inline constexpr int stripsPerBank = 8;

/** The notes Mackie Control surfaces send for their buttons, and light them with. */
namespace mackie
{
    inline constexpr int firstRecord = 0;
    inline constexpr int firstSolo = 8;
    inline constexpr int firstMute = 16;
    inline constexpr int firstSelect = 24;
    inline constexpr int cycle = 86;
    inline constexpr int bankLeft = 46;
    inline constexpr int bankRight = 47;
    inline constexpr int channelLeft = 48;
    inline constexpr int channelRight = 49;
    inline constexpr int rewind = 91;
    inline constexpr int fastForward = 92;
    inline constexpr int stop = 93;
    inline constexpr int play = 94;
    inline constexpr int record = 95;
} // namespace mackie

/** A button press from a keyboard in its Mackie Control mode. Releases, and anything that
    isn't one of the buttons RoughTrack uses, give nothing.
*/
std::optional<ControlEvent> decodeMackie (const juce::MidiMessage&);

/** A MIDI Machine Control transport message, which many keyboards without a Mackie mode send. */
std::optional<ControlEvent> decodeMmc (const juce::MidiMessage&);

/** Lights or darkens one of a Mackie surface's buttons. */
juce::MidiMessage mackieLight (int note, bool on);

/** Whether a MIDI port's name says it's the one a keyboard sends its DAW controls on. */
bool looksLikeControlPort (const juce::String& portName);

/** The bar line before a position, or the start. A position just past a bar line goes back to it. */
juce::int64 previousBar (juce::int64 position, double samplesPerBar);

/** The bar line after a position. */
juce::int64 nextBar (juce::int64 position, double samplesPerBar);

/** Which lane to arm when the arm is moved one track up (direction -1) or down (+1).
    It moves on from the last armed lane going down, and the first going up, and stays
    put at the ends. With nothing armed it starts at the first or last lane. Gives -1
    when there are no lanes.
*/
int nextArmedLane (const std::vector<bool>& armedByLane, int direction);

/** The first lane of the bank the fader buttons cover: the eight that hold the first
    armed lane, or the first eight when nothing is armed.
*/
int bankStart (const std::vector<bool>& armedByLane);

} // namespace roughtrack
