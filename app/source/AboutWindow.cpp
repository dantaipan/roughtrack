#include "AboutWindow.h"

#include "ConsoleLookAndFeel.h"

#include <BinaryData.h>

namespace
{

constexpr int windowWidth = 480;
constexpr int windowHeight = 560;
constexpr int iconSize = 128;
constexpr int headerHeight = 250;

const char* const creditsText =
    "Drums\n"
    "DRSKit by Lars Muldjord and Bent Bisballe Nyeng, recorded on a kit by Jes Eiler of DRSDrums. "
    "Drum samples provided by DrumGizmo.org, under Creative Commons Attribution 4.0 "
    "(creativecommons.org/licenses/by/4.0). Mixed to stereo for RiffTrack: each drum's close, overhead "
    "and room microphones were combined, a subset of hits was kept, each hit was trimmed and faded, "
    "levels were set, and the audio was converted to 48 kHz.\n\n"
    "Grooves\n"
    "Groove MIDI Dataset by Jon Gillick, Adam Roberts, Peter Rob and David Bansal, Google Magenta, under "
    "Creative Commons Attribution 4.0. Performances were aligned to their bars, steady sections and fills "
    "were selected, and notes were reduced to thirteen drum sounds.\n\n"
    "Software\n"
    "JUCE, under the GNU AGPL 3.0\n"
    "NeuralAmpModelerCore, MIT\n"
    "Eigen, MPL 2.0\n"
    "nlohmann/json, MIT";

} // namespace

AboutComponent::AboutComponent()
    : icon (juce::ImageCache::getFromMemory (BinaryData::iconmacos1024_png, BinaryData::iconmacos1024_pngSize))
{
    credits.setMultiLine (true);
    credits.setReadOnly (true);
    credits.setCaretVisible (false);
    credits.setScrollbarsShown (true);
    credits.setColour (juce::TextEditor::backgroundColourId, console::recess);
    credits.setColour (juce::TextEditor::textColourId, console::cream);
    credits.setColour (juce::TextEditor::outlineColourId, console::outline);
    credits.setFont (juce::FontOptions (13.0f));
    credits.setText (creditsText, false);
    addAndMakeVisible (credits);

    setSize (windowWidth, windowHeight);
}

void AboutComponent::paint (juce::Graphics& g)
{
    g.fillAll (console::panel);

    auto area = headerArea;
    g.drawImage (icon, area.removeFromTop (iconSize + 8).withSizeKeepingCentre (iconSize, iconSize).toFloat());

    g.setColour (console::cream);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("RiffTrack", area.removeFromTop (34), juce::Justification::centred);

    g.setColour (console::dimText);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Version " RIFFTRACK_VERSION, area.removeFromTop (22), juce::Justification::centred);

    g.setColour (console::cream);
    g.drawText ("A recording console for guitarists.", area.removeFromTop (26), juce::Justification::centred);

    g.setColour (console::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String::fromUTF8 ("\xc2\xa9 2026 Dan Mullins. Free software under the GNU AGPL 3.0 or later."),
                area.removeFromTop (20), juce::Justification::centred);
}

void AboutComponent::resized()
{
    auto area = getLocalBounds().reduced (20);
    headerArea = area.removeFromTop (headerHeight);
    credits.setBounds (area);
}

void showAboutWindow (juce::Component::SafePointer<juce::DialogWindow>& window)
{
    if (window != nullptr)
    {
        window->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new AboutComponent());
    options.dialogTitle = "About RiffTrack";
    options.dialogBackgroundColour = console::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    window = options.launchAsync();
}
