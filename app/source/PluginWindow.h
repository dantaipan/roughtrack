#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** Shows a plugin's own editor. Closing it only hides it, so it must be deleted
    before the plugin is.
*/
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance&, std::function<void()> onClose);

    void closeButtonPressed() override;

private:
    std::function<void()> closed;
};
