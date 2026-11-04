#include "ConsoleLookAndFeel.h"
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
        setResizeLimits (MainComponent::getMinimumWidth(), 480, 16384, 16384);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class RoughTrackApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "RoughTrack"; }
    const juce::String getApplicationVersion() override { return ROUGHTRACK_VERSION; }

    // Plugin scanning relaunches this binary, once for each plugin.
    bool moreThanOneInstanceAllowed() override { return isScanChild() || isSnapshot(); }

    void initialise (const juce::String& commandLine) override
    {
        if (isScanChild())
        {
            juce::Process::setDockIconVisible (false);

            const auto args = getCommandLineParameterArray();

            juce::AudioPluginFormatManager formats;
            roughtrack::addPluginFormats (formats);

            const auto found = args.size() == 4 && roughtrack::writePluginScanResult (formats, { args[1], args[2] }, juce::File (args[3]));

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

        lookAndFeel = std::make_unique<ConsoleLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

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
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
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
    // without a person at the screen: --snapshot <session folder> <png file> [track] [console] [timeline] [tuner] [2x]
    // With a track, the picture is of that track's channel window. 2x saves it at Retina resolution.
    void startSnapshot()
    {
        const auto args = getCommandLineParameterArray();
        const juce::File session (args[1]);
        const juce::File picture (args[2]);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), session);

        const auto channelTrack = args[3].containsOnly ("0123456789") && args[3].isNotEmpty() ? args[3].getIntValue() : -1;
        auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());

        if (args.contains ("console") || args.contains ("timeline"))
            main->showConsole (args.contains ("console"));

        if (args.contains ("tuner"))
            main->showTuner (true);

        const auto scale = args.contains ("2x") ? 2.0f : 1.0f;

        juce::Timer::callAfterDelay (snapshotDelayMs, [this, picture, channelTrack, scale]
        {
            juce::Component* target = mainWindow.get();

            if (auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            {
                content->prepareForSnapshot();

                if (channelTrack >= 0)
                    target = content->showChannelWindow (channelTrack);
            }

            const auto image = target->createComponentSnapshot (target->getLocalBounds(), true, scale);

            picture.deleteFile();

            if (auto stream = picture.createOutputStream())
                juce::PNGImageFormat().writeImageToStream (image, *stream);

            quit();
        });
    }

    std::unique_ptr<ConsoleLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace

START_JUCE_APPLICATION (RoughTrackApplication)
