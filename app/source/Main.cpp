#include "MainComponent.h"

#include <iostream>

namespace
{

constexpr int snapshotDelayMs = 3000;

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, const juce::File& session)
        : DocumentWindow (name,
                          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (backgroundColourId),
                          allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (session), true);
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

    // Plugin scanning relaunches this binary, once for each plugin.
    bool moreThanOneInstanceAllowed() override { return isScanChild() || isSnapshot(); }

    void initialise (const juce::String& commandLine) override
    {
        if (isScanChild())
        {
            juce::Process::setDockIconVisible (false);

            const auto args = getCommandLineParameterArray();

            juce::AudioPluginFormatManager formats;
            rifftrack::addPluginFormats (formats);

            const auto found = args.size() == 4 && rifftrack::writePluginScanResult (formats, { args[1], args[2] }, juce::File (args[3]));

            setApplicationReturnValue (found ? 0 : 1);
            quit();
            return;
        }

        // Lets scripts confirm a signed build can reach the hardware.
        if (commandLine.contains ("--probe"))
        {
            std::cout << describeMicrophoneAccess() << "\n" << describeAudioDevices() << std::flush;
            quit();
            return;
        }

        if (isSnapshot())
        {
            startSnapshot();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), juce::File());
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

private:
    static bool isScanChild()
    {
        return getCommandLineParameterArray()[0] == scanChildArgument;
    }

    static bool isSnapshot()
    {
        return getCommandLineParameterArray()[0] == "--snapshot";
    }

    // Saves a picture of the real UI on a given session, so layout can be checked
    // without a person at the screen: --snapshot <session folder> <png file> [track]
    // With a track, the picture is of that track's channel window.
    void startSnapshot()
    {
        const auto args = getCommandLineParameterArray();
        const juce::File session (args[1]);
        const juce::File picture (args[2]);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), session);

        const auto channelTrack = args.size() > 3 ? args[3].getIntValue() : -1;

        juce::Timer::callAfterDelay (snapshotDelayMs, [this, picture, channelTrack]
        {
            juce::Component* target = mainWindow.get();

            if (channelTrack >= 0)
                if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    target = main->showChannelWindow (channelTrack);

            const auto image = target->createComponentSnapshot (target->getLocalBounds());

            picture.deleteFile();

            if (auto stream = picture.createOutputStream())
                juce::PNGImageFormat().writeImageToStream (image, *stream);

            quit();
        });
    }

    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace

START_JUCE_APPLICATION (RiffTrackApplication)
