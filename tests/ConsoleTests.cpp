#include "MasterStrip.h"
#include "TrackStrip.h"
#include "TunerRibbon.h"
#include "VuMeter.h"

#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace rifftrack;
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
            strip.updateMeter();
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
