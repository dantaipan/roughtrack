#include "ChannelWindow.h"

namespace
{
constexpr int panelWidth = 480;
constexpr int rowHeight = 30;
constexpr int buttonWidth = 140;
constexpr int clearWidth = 60;
constexpr int margin = 12;
constexpr int numLoadRows = 3;
} // namespace

ChannelPanel::ChannelPanel (rifftrack::AudioEngine& engineToUse, int trackIndex, juce::File startFolder)
    : engine (engineToUse),
      track (trackIndex),
      folder (std::move (startFolder)),
      parameters (
          (int) rifftrack::ChannelParameter::count,
          [] (int i) -> const rifftrack::ParameterInfo& { return rifftrack::getParameterInfo ((rifftrack::ChannelParameter) i); },
          [this] (int i) { return engine.getChannel (track).getParameter ((rifftrack::ChannelParameter) i); },
          [this] (int i, float value) { engine.getChannel (track).setParameter ((rifftrack::ChannelParameter) i, value); })
{
    for (auto* child : std::initializer_list<juce::Component*> { &ampButton, &clearAmpButton, &cabinetButton, &clearCabinetButton,
                                                                 &ampLabel, &cabinetLabel, &messageLabel, &parameters })
        addAndMakeVisible (child);

    messageLabel.setColour (juce::Label::textColourId, juce::Colours::orange);

    ampButton.onClick = [this]
    {
        chooseFile ("Choose a NAM capture", "*.nam", [this] (const juce::File& file)
        {
            showResult (engine.loadAmp (track, file));
        });
    };

    cabinetButton.onClick = [this]
    {
        chooseFile ("Choose a cabinet IR", "*.wav;*.aif;*.aiff", [this] (const juce::File& file)
        {
            showResult (engine.getChannel (track).loadCabinet (file));
        });
    };

    clearAmpButton.onClick = [this]
    {
        engine.removeAmp (track);
        showResult (juce::Result::ok());
    };

    clearCabinetButton.onClick = [this]
    {
        engine.getChannel (track).clearCabinet();
        showResult (juce::Result::ok());
    };

    // A drum track is already a finished sound, so it has no amp or cabinet.
    if (hasSources())
    {
        setSize (panelWidth, 2 * margin + numLoadRows * rowHeight + parameters.getIdealHeight());
    }
    else
    {
        for (auto* source : std::initializer_list<juce::Component*> { &ampButton, &clearAmpButton, &cabinetButton, &clearCabinetButton, &ampLabel, &cabinetLabel })
            source->setVisible (false);

        setSize (panelWidth, 2 * margin + rowHeight + parameters.getIdealHeight());
    }

    refresh();
}

bool ChannelPanel::hasSources() const
{
    return engine.getTrackType (track) == rifftrack::TrackType::audio;
}

void ChannelPanel::chooseFile (const juce::String& title, const juce::String& patterns, std::function<void (const juce::File&)> onChosen)
{
    chooser = std::make_unique<juce::FileChooser> (title, folder, patterns);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, useFile = std::move (onChosen)] (const juce::FileChooser& finished)
                          {
                              const auto chosen = finished.getResult();

                              if (chosen == juce::File())
                                  return;

                              folder = chosen.getParentDirectory();
                              useFile (chosen);

                              if (onLoaded != nullptr)
                                  onLoaded (folder);
                          });
}

void ChannelPanel::showResult (const juce::Result& result)
{
    messageLabel.setText (result.getErrorMessage(), juce::dontSendNotification);
    refresh();
}

void ChannelPanel::refresh()
{
    const auto& channel = engine.getChannel (track);

    ampLabel.setText (channel.hasAmp() ? channel.getAmpFile().getFileNameWithoutExtension() : juce::String ("No capture"), juce::dontSendNotification);
    cabinetLabel.setText (channel.getCabinetFile() != juce::File() ? channel.getCabinetFile().getFileNameWithoutExtension() : juce::String ("No cabinet"),
                          juce::dontSendNotification);

    clearAmpButton.setEnabled (channel.hasAmp());
    clearCabinetButton.setEnabled (channel.getCabinetFile() != juce::File());
    parameters.refresh();
}

void ChannelPanel::resized()
{
    auto area = getLocalBounds().reduced (margin);

    const auto layoutRow = [&area] (juce::Component& button, juce::Component& clear, juce::Component& label)
    {
        auto row = area.removeFromTop (rowHeight).reduced (0, 2);
        button.setBounds (row.removeFromLeft (buttonWidth));
        clear.setBounds (row.removeFromRight (clearWidth));
        label.setBounds (row);
    };

    if (hasSources())
    {
        layoutRow (ampButton, clearAmpButton, ampLabel);
        layoutRow (cabinetButton, clearCabinetButton, cabinetLabel);
    }

    messageLabel.setBounds (area.removeFromTop (rowHeight));
    parameters.setBounds (area);
}

PanelWindow::PanelWindow (const juce::String& title, juce::Component* contentToOwn)
    : DocumentWindow (title,
                      juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (backgroundColourId),
                      closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (contentToOwn, true);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}
