#pragma once

#include "roughtrack/ControlSurface.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>

/** Sits between the MIDI inputs and the engine. Button presses from the controller port,
    and MMC transport messages from any port, become commands; everything else goes on to
    the engine to be played. The controller port's messages never reach the instruments,
    so its buttons can't sound notes.
*/
class ControllerInput final : public juce::MidiInputCallback
{
public:
    using Forward = std::function<void (juce::MidiInput*, const juce::MidiMessage&)>;

    /** Both are called on the MIDI thread. */
    ControllerInput (Forward forwardToEngine, std::function<void (roughtrack::ControlEvent)> onCommand);

    /** The identifier of the port to take as the controller, or empty for none. Any thread. */
    void setControllerPort (const juce::String& identifier);
    juce::String getControllerPort() const;

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    /** The same, for a port given by its identifier. */
    void route (const juce::String& sourceIdentifier, juce::MidiInput*, const juce::MidiMessage&);

private:
    Forward forward;
    std::function<void (roughtrack::ControlEvent)> command;

    mutable juce::SpinLock lock;
    juce::String controllerPort;
};
