#include "roughtrack/AudioDevices.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>

namespace roughtrack
{

namespace
{

AudioDeviceInfo describeDevice (juce::AudioIODeviceType& type, const juce::String& name, bool hasInput, bool hasOutput)
{
    AudioDeviceInfo info;
    info.name = name.toStdString();
    info.typeName = type.getTypeName().toStdString();

    const std::unique_ptr<juce::AudioIODevice> device (type.createDevice (hasOutput ? name : juce::String(),
                                                                          hasInput ? name : juce::String()));

    if (device == nullptr)
        return info;

    info.inputChannels = device->getInputChannelNames().size();
    info.outputChannels = device->getOutputChannelNames().size();
    info.currentSampleRate = device->getCurrentSampleRate();
    info.currentBufferSize = device->getCurrentBufferSizeSamples();

    for (auto rate : device->getAvailableSampleRates())
        info.sampleRates.push_back (rate);

    for (auto size : device->getAvailableBufferSizes())
        info.bufferSizes.push_back (size);

    return info;
}

} // namespace

std::vector<AudioDeviceInfo> listAudioDevices()
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::AudioDeviceManager manager;
    std::vector<AudioDeviceInfo> devices;

    for (auto* type : manager.getAvailableDeviceTypes())
    {
        type->scanForDevices();

        const auto inputs = type->getDeviceNames (true);
        const auto outputs = type->getDeviceNames (false);

        auto names = outputs;
        names.addArray (inputs);
        names.removeDuplicates (false);

        for (const auto& name : names)
            devices.push_back (describeDevice (*type, name, inputs.contains (name), outputs.contains (name)));
    }

    return devices;
}

std::string preferredInputDevice (const std::string& outputDevice,
                                  const std::string& currentInputDevice,
                                  const std::vector<std::string>& availableInputDevices)
{
    const auto outputHasInputs = std::find (availableInputDevices.begin(), availableInputDevices.end(), outputDevice)
                                 != availableInputDevices.end();

    return outputHasInputs ? outputDevice : currentInputDevice;
}

AudioDeviceChoice withSessionRate (AudioDeviceChoice chosen, double sessionRate)
{
    if (sessionRate > 0.0)
        chosen.sampleRate = sessionRate;

    return chosen;
}

AudioDeviceChoice wantedAudioDevice (const AudioDeviceChoice& chosen,
                                     const AudioDeviceChoice& current,
                                     double sessionRate,
                                     const std::vector<std::string>& availableOutputDevices)
{
    const auto chosenIsThere = ! chosen.outputDevice.empty()
                               && std::find (availableOutputDevices.begin(), availableOutputDevices.end(), chosen.outputDevice)
                                      != availableOutputDevices.end();

    return withSessionRate (chosenIsThere ? chosen : current, sessionRate);
}

bool shouldRestoreAudioDevice (const AudioDeviceChoice& chosen,
                               const AudioDeviceChoice& current,
                               const std::vector<std::string>& availableOutputDevices)
{
    if (chosen.outputDevice.empty()
        || std::find (availableOutputDevices.begin(), availableOutputDevices.end(), chosen.outputDevice) == availableOutputDevices.end())
        return false;

    return current.outputDevice != chosen.outputDevice
           || (chosen.bufferSize > 0 && current.bufferSize != chosen.bufferSize)
           || (chosen.sampleRate > 0.0 && ! juce::approximatelyEqual (current.sampleRate, chosen.sampleRate));
}

std::string describeSampleRate (double rate)
{
    auto text = std::to_string (std::round (rate / 10.0) / 100.0);

    while (text.find ('.') != std::string::npos && (text.back() == '0' || text.back() == '.'))
        text.pop_back();

    return text + " kHz";
}

double bufferLatencyMs (int bufferSizeSamples, double sampleRate)
{
    return sampleRate > 0.0 ? 1000.0 * bufferSizeSamples / sampleRate : 0.0;
}

} // namespace roughtrack
