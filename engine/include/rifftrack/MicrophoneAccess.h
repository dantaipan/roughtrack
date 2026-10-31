#pragma once

#include <functional>

namespace rifftrack
{

enum class MicrophoneAccess
{
    notDetermined,
    restricted,
    denied,
    granted
};

/** macOS's current answer, read without prompting the user. */
MicrophoneAccess getMicrophoneAccess();

/** Shows the system prompt if the user hasn't answered it yet.
    The callback may run on any thread.
*/
void requestMicrophoneAccess (std::function<void (bool granted)> callback);

} // namespace rifftrack
