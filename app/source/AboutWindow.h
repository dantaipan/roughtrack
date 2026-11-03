#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** The app's icon, name and version, and the credits its licenses ask for. */
class AboutComponent final : public juce::Component
{
public:
    AboutComponent();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Image icon;
    juce::TextEditor credits;
    juce::Rectangle<int> headerArea;
};

/** Opens the About window, or brings it to the front if it's already open. */
void showAboutWindow (juce::Component::SafePointer<juce::DialogWindow>& window);
