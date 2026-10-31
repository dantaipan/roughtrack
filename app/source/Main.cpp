#include "MainComponent.h"

#include <iostream>

namespace
{

class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow (const juce::String& name)
        : DocumentWindow (name,
                          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (backgroundColourId),
                          allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
        setResizable (true, true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class RiffTrackApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "RiffTrack"; }
    const juce::String getApplicationVersion() override { return RIFFTRACK_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        // Lets scripts confirm a signed build can reach the hardware.
        if (commandLine.contains ("--probe"))
        {
            std::cout << describeMicrophoneAccess() << "\n" << describeAudioDevices() << std::flush;
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace

START_JUCE_APPLICATION (RiffTrackApplication)
