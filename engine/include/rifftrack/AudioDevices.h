#pragma once

#include <string>
#include <vector>

namespace rifftrack
{

struct AudioDeviceInfo
{
    std::string name;
    std::string typeName;
    int inputChannels = 0;
    int outputChannels = 0;
    double currentSampleRate = 0.0;
    int currentBufferSize = 0;
    std::vector<double> sampleRates;
    std::vector<int> bufferSizes;
};

/** Lists the audio hardware visible to the app without opening any of it. */
std::vector<AudioDeviceInfo> listAudioDevices();

/** Picks the input to record from. An interface's own inputs win over whatever
    macOS has as its default, usually the built-in mic: they're what the player
    is plugged into, and they share the output's clock so tracks can't drift.
*/
std::string preferredInputDevice (const std::string& outputDevice,
                                  const std::string& currentInputDevice,
                                  const std::vector<std::string>& availableInputDevices);

/** One-way delay added by a single audio buffer. */
double bufferLatencyMs (int bufferSizeSamples, double sampleRate);

} // namespace rifftrack
