#include "ConsolePanel.h"
#include "MasterStrip.h"
#include "TrackStrip.h"
#include "TunerRibbon.h"
#include "VuMeter.h"

#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace roughtrack;
using Catch::Approx;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

struct Rig
{
    Rig()
    {
        engine.setSessionDirectory (temp.directory);
        engine.prepare (sampleRate, blockSize);
        engine.setTunerInput (0);
        tuner.start (sampleRate);
    }

    /** Plays a tone into the first input, then gives the tuner a look at it. */
    void play (double frequency, double seconds)
    {
        const auto numBlocks = juce::roundToInt (seconds * sampleRate / blockSize);
        std::vector<float> input ((size_t) blockSize), left ((size_t) blockSize), right ((size_t) blockSize);
        const float* inputs[] { input.data() };
        float* outputs[] { left.data(), right.data() };

        for (int block = 0; block < numBlocks; ++block)
        {
            for (auto& sample : input)
                sample = 0.2f * (float) std::sin (2.0 * std::numbers::pi * frequency * (double) played++ / sampleRate);

            engine.processBlock (inputs, 1, outputs, 2, blockSize);

            if (block % 16 == 0)
                tuner.update (16.0 * blockSize / sampleRate);
        }
    }

    test::TempDirectory temp;
    AudioEngine engine;
    TunerRibbon tuner { engine };
    juce::int64 played = 0;
};

template <typename ComponentType>
ComponentType* findChild (juce::Component& parent, std::function<bool (ComponentType&)> matches)
{
    for (auto* child : parent.getChildren())
        if (auto* typed = dynamic_cast<ComponentType*> (child); typed != nullptr && matches (*typed))
            return typed;

    return nullptr;
}

juce::Button& findButton (juce::Component& strip, const juce::String& text)
{
    auto* button = findChild<juce::Button> (strip, [&text] (juce::Button& b) { return b.getButtonText() == text; });
    REQUIRE (button != nullptr);
    return *button;
}

juce::Slider& findSlider (juce::Component& strip, bool rotary)
{
    auto* slider = findChild<juce::Slider> (strip, [rotary] (juce::Slider& s)
                                            {
                                                return (s.getSliderStyle() == juce::Slider::RotaryVerticalDrag) == rotary;
                                            });
    REQUIRE (slider != nullptr);
    return *slider;
}

} // namespace

TEST_CASE ("The needle sits where a VU's does")
{
    // A sine peaking at -18 dBFS is 0 VU, 3 VU below the end of the scale.
    const auto zeroVu = std::pow (10.0f, -18.0f / 20.0f) / std::numbers::sqrt2_v<float>;

    CHECK (VuMeter::needlePosition (zeroVu) == Approx (std::pow (10.0f, -3.0f / 20.0f)).margin (0.005f));
    CHECK (VuMeter::needlePosition (0.0f) == 0.0f);
}

TEST_CASE ("The needle stops at the end of the scale")
{
    CHECK (VuMeter::needlePosition (1.0f) <= 1.05f);
}

TEST_CASE ("The clip light stays on until it's clicked")
{
    VuMeter meter;

    meter.update (0.1f, 0.5f, true);
    CHECK_FALSE (meter.hasClipped());

    meter.update (0.1f, 0.99f, true);
    CHECK (meter.hasClipped());

    meter.update (0.0f, 0.0f, true);
    CHECK (meter.hasClipped());

    meter.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                       &meter, &meter, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false));
    CHECK_FALSE (meter.hasClipped());
}

TEST_CASE ("The tuner ribbon names the note it hears")
{
    Rig rig;
    rig.play (110.0, 0.6);

    CHECK (rig.tuner.getDisplayedNote() == "A2");
    CHECK (std::abs (rig.tuner.getDisplayedCents()) < 1.0);
}

TEST_CASE ("The tuner ribbon shows sharp and flat")
{
    Rig rig;

    rig.play (110.0 * std::pow (2.0, 20.0 / 1200.0), 0.6);
    CHECK (rig.tuner.getDisplayedCents() == Approx (20.0).margin (1.0));

    rig.play (110.0 * std::pow (2.0, -20.0 / 1200.0), 0.6);
    CHECK (rig.tuner.getDisplayedCents() == Approx (-20.0).margin (1.0));
}

TEST_CASE ("The tuner ribbon moves on to the next string")
{
    Rig rig;

    rig.play (110.0, 0.6);
    rig.play (146.83, 0.6);

    CHECK (rig.tuner.getDisplayedNote() == "D3");
}

TEST_CASE ("The tuner ribbon lets go after the string stops")
{
    Rig rig;

    rig.play (110.0, 0.6);
    rig.play (0.0, 1.0);
    CHECK (rig.tuner.getDisplayedNote() == "A2");

    rig.play (0.0, 1.5);
    CHECK (rig.tuner.getDisplayedNote().isEmpty());
}

TEST_CASE ("Restarting the tuner forgets what it heard")
{
    Rig rig;

    rig.play (110.0, 0.6);
    rig.tuner.start (sampleRate);

    CHECK (rig.tuner.getDisplayedNote().isEmpty());
}

TEST_CASE ("Faders put 0 dB well up their travel")
{
    juce::Slider fader;
    setUpFader (fader);

    const auto zero = fader.valueToProportionOfLength (0.0);

    CHECK (zero > 0.7);
    CHECK (zero < 0.9);
    CHECK (fader.getMaximum() == 6.0);
    CHECK (fader.getMinimum() == -60.0);
}

TEST_CASE ("Strips of every type survive both layouts and keep their settings")
{
    test::TempDirectory temp;
    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    engine.prepare (sampleRate, blockSize);

    for (const auto type : { TrackType::audio, TrackType::drums, TrackType::midi })
    {
        const auto track = engine.addTrack ("Track", type);

        engine.setTrackGain (track, 0.5f);
        engine.setTrackArmed (track, type != TrackType::drums);

        TrackStrip strip (engine, track);
        strip.setSize (104, 500);
        strip.setInputChannels (2);
        strip.refreshFromEngine();

        for (const auto layout : { TrackStrip::Layout::channel, TrackStrip::Layout::row, TrackStrip::Layout::channel })
        {
            strip.setLayout (layout);
            strip.updateMeter (strip.takePeak());
            strip.createComponentSnapshot (strip.getLocalBounds());
        }

        CHECK (engine.getTrackSettings (track).gain == Approx (0.5f));
        CHECK (engine.isTrackArmed (track) == (type != TrackType::drums));
    }
}

TEST_CASE ("A session's master level reaches the master fader")
{
    test::TempDirectory temp;
    AudioEngine engine;
    engine.setMasterGain (0.5f);

    MasterStrip master (engine);
    master.setSize (124, 500);

    CHECK (engine.getMasterGain() == Approx (0.5f));

    engine.setMasterGain (0.25f);
    master.refreshFromEngine();
    master.updateMeter();

    CHECK (engine.getMasterGain() == Approx (0.25f));
}

TEST_CASE ("A track's row and its console channel show the same settings")
{
    test::TempDirectory temp;
    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    engine.prepare (sampleRate, blockSize);
    const auto track = engine.addTrack ("Guitar", TrackType::audio);

    TrackStrip row (engine, track), channel (engine, track);
    channel.setLayout (TrackStrip::Layout::channel);
    row.setSize (300, 76);
    channel.setSize (104, 500);

    for (auto* strip : { &row, &channel })
    {
        strip->setInputChannels (2);
        strip->refreshFromEngine();
    }

    // What one strip changes goes to the engine, and the other strip picks it up from there.
    engine.setTrackMuted (track, true);
    engine.setTrackSoloed (track, true);
    engine.setTrackArmed (track, true);
    engine.setTrackMonitoring (track, true);
    engine.setTrackGain (track, juce::Decibels::decibelsToGain (-12.0f));
    engine.setTrackPan (track, -0.5f);
    engine.setTrackName (track, "Lead");

    for (auto* strip : { &row, &channel })
    {
        strip->syncFromEngine();

        CHECK (findButton (*strip, "M").getToggleState());
        CHECK (findButton (*strip, "S").getToggleState());
        CHECK (findButton (*strip, "Arm").getToggleState());
        CHECK (findButton (*strip, "Mon").getToggleState());
        CHECK (findSlider (*strip, false).getValue() == Approx (-12.0).margin (0.05));
        CHECK (findSlider (*strip, true).getValue() == Approx (-0.5).margin (0.005));

        auto* name = findChild<juce::Label> (*strip, [] (juce::Label&) { return true; });
        REQUIRE (name != nullptr);
        CHECK (name->getText() == "Lead");
    }

    // Syncing doesn't write back, so nothing it shows is changed by it.
    CHECK (engine.getTrackSettings (track).gain == Approx (juce::Decibels::decibelsToGain (-12.0f)));
}

TEST_CASE ("The console lays out its channels in track order and fits a usable fader")
{
    test::TempDirectory temp;
    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    engine.prepare (sampleRate, blockSize);

    const auto first = engine.addTrack ("One", TrackType::audio);
    const auto second = engine.addTrack ("Two", TrackType::midi);
    engine.moveTrack (second, 0);

    ConsolePanel panel (engine);

    for (const auto track : engine.getTrackOrder())
        panel.addChannel (track);

    panel.setSize (ConsolePanel::getWidthFor (2), ConsolePanel::getMinimumHeight());

    REQUIRE (panel.getChannel (first) != nullptr);
    REQUIRE (panel.getChannel (second) != nullptr);
    CHECK (panel.getChannel (second)->getX() < panel.getChannel (first)->getX());
    CHECK (panel.getChannel (first)->getHeight() >= TrackStrip::getMinimumChannelHeight());

    panel.removeChannel (first);
    CHECK (panel.getChannel (first) == nullptr);

    panel.createComponentSnapshot (panel.getLocalBounds());
}
