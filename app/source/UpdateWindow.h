#pragma once

#include "Updates.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** Says a newer RoughTrack is out, with its release notes and a way to get it. */
class UpdateComponent final : public juce::Component
{
public:
    UpdateComponent (const updates::Release&, const juce::String& currentVersion, std::function<void()> onSkip);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void close();

    updates::Release release;
    juce::String currentVersion;
    std::function<void()> skipped;

    juce::Image icon;
    juce::TextEditor notes;
    juce::TextButton downloadButton { "Download" }, skipButton { "Skip This Version" }, laterButton { "Later" };
    juce::Rectangle<int> headerArea;
};

/** Opens the update window for a release, replacing one that's already open. onSkip runs
    if the player chooses Skip This Version.
*/
void showUpdateWindow (juce::Component::SafePointer<juce::DialogWindow>& window,
                       const updates::Release&,
                       const juce::String& currentVersion,
                       std::function<void()> onSkip);
