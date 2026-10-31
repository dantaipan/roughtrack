#pragma once

#include "rifftrack/AudioEngine.h"

#include <juce_data_structures/juce_data_structures.h>

namespace rifftrack
{

/** Takes, positions and mixer settings. Plugins are saved separately because
    their state can be slow to read and is only worth writing when it changes.
*/
std::unique_ptr<juce::XmlElement> createSessionXml (const AudioEngine&, const juce::File& sessionDirectory);

/** Replaces the engine's session. A take that's gone stays on its track, silent,
    so the session isn't damaged if the file comes back. The result says what's wrong.
*/
juce::Result applySessionXml (AudioEngine&, const juce::XmlElement&, const juce::File& sessionDirectory);

/** One undoable change to a track's clips. */
class ClipEdit final : public juce::UndoableAction
{
public:
    ClipEdit (AudioEngine&, int track, ClipList before, ClipList after);

    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 1; }

private:
    AudioEngine& engine;
    const int track;
    const ClipList before, after;
};

} // namespace rifftrack
