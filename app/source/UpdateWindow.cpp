#include "UpdateWindow.h"

#include "BinaryData.h"
#include "ConsoleLookAndFeel.h"

namespace
{

constexpr int windowWidth = 520;
constexpr int windowHeight = 560;
constexpr int iconSize = 64;
constexpr int headerHeight = 96;
constexpr int buttonHeight = 28;

} // namespace

UpdateComponent::UpdateComponent (const updates::Release& releaseToShow, const juce::String& version, std::function<void()> onSkip)
    : release (releaseToShow),
      currentVersion (version),
      skipped (std::move (onSkip)),
      icon (juce::ImageCache::getFromMemory (BinaryData::iconmacos1024_png, BinaryData::iconmacos1024_pngSize))
{
    notes.setMultiLine (true);
    notes.setReadOnly (true);
    notes.setCaretVisible (false);
    notes.setScrollbarsShown (true);
    notes.setColour (juce::TextEditor::backgroundColourId, console::recess);
    notes.setColour (juce::TextEditor::textColourId, console::cream);
    notes.setColour (juce::TextEditor::outlineColourId, console::outline);

    for (const auto& run : updates::notesToRuns (release.notes))
    {
        notes.setFont (juce::FontOptions (run.heading ? 15.0f : 13.0f, run.bold ? juce::Font::bold : juce::Font::plain));
        notes.insertTextAtCaret (run.text);
    }

    notes.moveCaretToTop (false);
    addAndMakeVisible (notes);

    downloadButton.setColour (juce::TextButton::buttonColourId, console::amber.darker (0.2f));
    downloadButton.onClick = [this]
    {
        juce::URL (release.downloadUrl).launchInDefaultBrowser();
        close();
    };

    skipButton.onClick = [this]
    {
        if (skipped != nullptr)
            skipped();

        close();
    };

    laterButton.onClick = [this] { close(); };

    for (auto* button : { &downloadButton, &skipButton, &laterButton })
        addAndMakeVisible (button);

    setSize (windowWidth, windowHeight);
}

void UpdateComponent::close()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);
}

void UpdateComponent::paint (juce::Graphics& g)
{
    g.fillAll (console::panel);

    auto area = headerArea;
    g.drawImage (icon, area.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconSize).toFloat());
    area.removeFromLeft (16);

    const auto shownVersion = release.version.trimCharactersAtStart ("vV");

    g.setColour (console::cream);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("RoughTrack " + shownVersion + " is available", area.removeFromTop (area.getHeight() / 2), juce::Justification::bottomLeft);

    g.setColour (console::dimText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("You have " + currentVersion + ". Here's what's new:", area.withTrimmedTop (4), juce::Justification::topLeft);
}

void UpdateComponent::resized()
{
    auto area = getLocalBounds().reduced (20);

    headerArea = area.removeFromTop (headerHeight).reduced (0, 8);

    auto buttons = area.removeFromBottom (buttonHeight);
    area.removeFromBottom (14);
    notes.setBounds (area);

    downloadButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    laterButton.setBounds (buttons.removeFromRight (80));
    skipButton.setBounds (buttons.removeFromLeft (140));
}

void showUpdateWindow (juce::Component::SafePointer<juce::DialogWindow>& window,
                       const updates::Release& release,
                       const juce::String& currentVersion,
                       std::function<void()> onSkip)
{
    if (window != nullptr)
        delete window.getComponent();

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new UpdateComponent (release, currentVersion, std::move (onSkip)));
    options.dialogTitle = "Software Update";
    options.dialogBackgroundColour = console::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    window = options.launchAsync();
}
