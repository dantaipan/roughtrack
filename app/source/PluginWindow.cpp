#include "PluginWindow.h"

namespace
{

juce::AudioProcessorEditor* createEditor (juce::AudioPluginInstance& plugin)
{
    if (auto* editor = plugin.createEditorAndMakeActive())
        return editor;

    return new juce::GenericAudioProcessorEditor (plugin);
}

/** JUCE tells a VST3 editor the content scale is 1 even on a Retina display,
    and plugins that believe it draw at half size in one corner of the window.
    Setting the real scale here corrects what they're told.
*/
void applyDisplayScale (juce::AudioPluginInstance& plugin, juce::AudioProcessorEditor& editor)
{
    if (plugin.getPluginDescription().pluginFormatName != "VST3")
        return;

    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        editor.setScaleFactor ((float) display->scale);
}

} // namespace

PluginWindow::PluginWindow (juce::AudioPluginInstance& plugin, std::function<void()> onClose)
    : DocumentWindow (plugin.getName(),
                      juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (backgroundColourId),
                      closeButton | minimiseButton),
      closed (std::move (onClose))
{
    auto* editor = createEditor (plugin);
    applyDisplayScale (plugin, *editor);

    setUsingNativeTitleBar (true);
    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void PluginWindow::closeButtonPressed()
{
    setVisible (false);

    if (closed != nullptr)
        closed();
}
