#include "ControllerInput.h"

ControllerInput::ControllerInput (Forward forwardToEngine, std::function<void (roughtrack::ControlEvent)> onCommand)
    : forward (std::move (forwardToEngine)),
      command (std::move (onCommand))
{
}

void ControllerInput::setControllerPort (const juce::String& identifier)
{
    const juce::SpinLock::ScopedLockType scope (lock);
    controllerPort = identifier;
}

juce::String ControllerInput::getControllerPort() const
{
    const juce::SpinLock::ScopedLockType scope (lock);
    return controllerPort;
}

void ControllerInput::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    route (source != nullptr ? source->getIdentifier() : juce::String(), source, message);
}

void ControllerInput::route (const juce::String& sourceIdentifier, juce::MidiInput* source, const juce::MidiMessage& message)
{
    const auto isController = [&]
    {
        const juce::SpinLock::ScopedLockType scope (lock);
        return controllerPort.isNotEmpty() && sourceIdentifier == controllerPort;
    }();

    auto decoded = roughtrack::decodeMmc (message);

    if (! decoded.has_value() && isController)
        decoded = roughtrack::decodeMackie (message);

    if (decoded.has_value())
    {
        if (command != nullptr)
            command (*decoded);

        return;
    }

    if (! isController && forward != nullptr)
        forward (source, message);
}
