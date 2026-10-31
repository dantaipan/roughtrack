#pragma once

#include "ParameterPanel.h"

#include "rifftrack/AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** Loads a track's amp capture and cabinet, and holds its EQ, compressor and sends. */
class ChannelPanel final : public juce::Component
{
public:
    ChannelPanel (rifftrack::AudioEngine&, int track, juce::File startFolder);

    /** Called after anything loads, with the folder to start from next time. */
    std::function<void (const juce::File& folder)> onLoaded;

    void refresh();
    void resized() override;

private:
    void chooseFile (const juce::String& title, const juce::String& patterns, std::function<void (const juce::File&)> onChosen);
    void showResult (const juce::Result&);

    rifftrack::AudioEngine& engine;
    const int track;
    juce::File folder;

    juce::TextButton ampButton { "Load Capture..." }, clearAmpButton { "Clear" };
    juce::TextButton cabinetButton { "Load Cabinet IR..." }, clearCabinetButton { "Clear" };
    juce::Label ampLabel, cabinetLabel, messageLabel;
    ParameterPanel parameters;

    std::unique_ptr<juce::FileChooser> chooser;
};

/** A plain window around a panel. Closing it only hides it. */
class PanelWindow final : public juce::DocumentWindow
{
public:
    PanelWindow (const juce::String& title, juce::Component* contentToOwn);
    void closeButtonPressed() override { setVisible (false); }
};
