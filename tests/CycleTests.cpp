#include "TestHelpers.h"

#include "roughtrack/AudioEngine.h"
#include "roughtrack/Session.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace roughtrack;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;
constexpr int rampLength = 120000;
constexpr float rampStep = 1.0e-5f;

/** An engine playing a take whose every sample says where it is: sample i is i * rampStep. */
struct Rig
{
    Rig()
    {
        test::useTracks (engine, 2);
        engine.setSessionDirectory (temp.directory);
        engine.prepare (sampleRate, blockSize);

        juce::AudioBuffer<float> ramp (1, rampLength);

        for (int i = 0; i < rampLength; ++i)
            ramp.setSample (0, i, (float) i * rampStep);

        const auto file = temp.directory.getChildFile ("ramp.wav");
        test::writeWav (file, ramp, sampleRate);
        REQUIRE (engine.loadTake (0, file, 0));
    }

    /** Runs whole blocks and collects the left output. */
    std::vector<float> run (int numSamples, int block = blockSize)
    {
        std::vector<float> heard;
        std::vector<float> input ((size_t) block), left ((size_t) block), right ((size_t) block);
        const float* inputs[] { input.data() };
        float* outputs[] { left.data(), right.data() };

        for (int done = 0; done < numSamples; done += block)
        {
            engine.processBlock (inputs, 1, outputs, 2, block);
            heard.insert (heard.end(), left.begin(), left.end());
        }

        return heard;
    }

    /** Which sample of the ramp an output sample came from. */
    static int sourceOf (float value) { return juce::roundToInt (value / rampStep); }

    test::TempDirectory temp;
    AudioEngine engine;
};

/** Where the playhead should be after a number of samples of going round a cycle from 0. */
juce::int64 expectedPosition (juce::int64 played, juce::Range<juce::int64> cycle)
{
    return played < cycle.getEnd() ? played : cycle.getStart() + (played - cycle.getEnd()) % cycle.getLength();
}

} // namespace

TEST_CASE ("Playback goes round the cycle and jumps back on the exact sample")
{
    Rig rig;

    // Neither end is on a block boundary, so the jump comes part way through a block.
    const juce::Range<juce::int64> cycle { 10000, 20005 };
    rig.engine.setCycleRange (cycle);
    rig.engine.setCycleEnabled (true);
    REQUIRE (rig.engine.play());

    const auto heard = rig.run (50048);

    CHECK (rig.engine.getPosition() == expectedPosition (50048, cycle));

    // Away from the seam's short fades, every sample is the one that should play there.
    auto checked = 0;

    for (juce::int64 i = 0; i < (juce::int64) heard.size(); ++i)
    {
        const auto at = expectedPosition (i, cycle);

        // The take's own start fades in, as every clip's does.
        const auto nearSeam = at >= cycle.getStart() && (at - cycle.getStart() < edgeFadeSamples + 1 || cycle.getEnd() - at <= edgeFadeSamples);

        if (nearSeam || at <= edgeFadeSamples)
            continue;

        if (Rig::sourceOf (heard[(size_t) i]) != at)
        {
            FAIL_CHECK ("Output sample " << i << " came from " << Rig::sourceOf (heard[(size_t) i]) << ", not " << at);
            break;
        }

        ++checked;
    }

    // Four times round, so three jumps back were checked.
    CHECK (checked > 49000);
}

TEST_CASE ("The seam fades out and back in instead of clicking")
{
    Rig rig;
    const juce::Range<juce::int64> cycle { 10000, 20000 };
    rig.engine.setCycleRange (cycle);
    rig.engine.setCycleEnabled (true);
    REQUIRE (rig.engine.play());

    const auto heard = rig.run (20064);

    // Just before the end it's nearly silent, and just after the jump it starts from nearly silent.
    CHECK (std::abs (heard[19999]) < 0.01f);
    CHECK (std::abs (heard[20000]) < 0.01f);
    CHECK (Rig::sourceOf (heard[19000]) == 19000);
}

TEST_CASE ("Play from before the cycle leads into it")
{
    Rig rig;
    const juce::Range<juce::int64> cycle { 9600, 19200 };
    rig.engine.setCycleRange (cycle);
    rig.engine.setCycleEnabled (true);
    rig.engine.setPosition (3200);
    REQUIRE (rig.engine.play());

    CHECK (rig.engine.getPosition() == 3200);

    rig.run (19200);
    CHECK (rig.engine.getPosition() == expectedPosition (3200 + 19200, cycle));
}

TEST_CASE ("Play from past the cycle starts at the cycle")
{
    Rig rig;
    rig.engine.setCycleRange ({ 4800, 9600 });
    rig.engine.setCycleEnabled (true);
    rig.engine.setPosition (12000);
    REQUIRE (rig.engine.play());

    CHECK (rig.engine.getPosition() == 4800);

    const auto heard = rig.run (3200);
    CHECK (Rig::sourceOf (heard[1000]) == 5800);
}

TEST_CASE ("Recording from past the cycle stays where it is")
{
    Rig rig;
    rig.engine.setCycleRange ({ 4800, 9600 });
    rig.engine.setCycleEnabled (true);
    rig.engine.setPosition (12000);
    rig.engine.setTrackArmed (1, true);
    rig.engine.setTrackInput (1, 0, 1);

    REQUIRE (rig.engine.record().wasOk());
    CHECK (rig.engine.getPosition() == 12000);

    rig.run (6400);
    rig.engine.stop();
    CHECK (rig.engine.getPosition() == 18400);
}

TEST_CASE ("A cycle that's off, or too short, is ignored")
{
    Rig rig;

    SECTION ("off")
    {
        rig.engine.setCycleRange ({ 0, 9600 });
    }

    SECTION ("too short")
    {
        rig.engine.setCycleRange ({ 0, AudioEngine::minimumCycleSamples - 1 });
        rig.engine.setCycleEnabled (true);
    }

    REQUIRE (rig.engine.play());
    rig.run (19200);

    CHECK (rig.engine.getPosition() == 19200);
}

TEST_CASE ("Recording plays straight through the cycle")
{
    Rig rig;
    rig.engine.setCycleRange ({ 0, 9600 });
    rig.engine.setCycleEnabled (true);
    rig.engine.setTrackArmed (0, true);
    rig.engine.setTrackInput (0, 0, 1);

    SECTION ("from stopped")
    {
        REQUIRE (rig.engine.record().wasOk());
        rig.run (19200);
        CHECK (rig.engine.getPosition() == 19200);
    }

    SECTION ("punched in while going round")
    {
        rig.engine.setTrackArmed (0, false);
        REQUIRE (rig.engine.play());
        rig.run (6400);

        rig.engine.setTrackArmed (1, true);
        rig.engine.setTrackInput (1, 0, 1);
        REQUIRE (rig.engine.record().wasOk());

        rig.run (12800);
        CHECK (rig.engine.getPosition() == 19200);
    }

    rig.engine.stop();
}

TEST_CASE ("The cycle can be turned on and off while playing")
{
    Rig rig;
    const juce::Range<juce::int64> cycle { 4800, 14400 };
    rig.engine.setCycleRange (cycle);
    REQUIRE (rig.engine.play());

    rig.run (3200);
    rig.engine.setCycleEnabled (true);

    // What's read ahead is read again for the cycle, partly by the disk thread.
    juce::Thread::sleep (100);
    auto heard = rig.run (16000);

    CHECK (rig.engine.getPosition() == expectedPosition (19200, cycle));
    CHECK (Rig::sourceOf (heard[(size_t) (14400 - 3200 + 1000)]) == 4800 + 1000);

    rig.engine.setCycleEnabled (false);
    juce::Thread::sleep (100);
    const auto from = rig.engine.getPosition();
    heard = rig.run (19200);

    CHECK (rig.engine.getPosition() == from + 19200);
    CHECK (Rig::sourceOf (heard[15000]) == from + 15000);
}

TEST_CASE ("A session keeps its cycle")
{
    test::TempDirectory temp;
    AudioEngine engine;
    engine.setCycleRange ({ 96000, 288000 });
    engine.setCycleEnabled (true);

    const auto xml = createSessionXml (engine, temp.directory);

    AudioEngine restored;
    REQUIRE (applySessionXml (restored, *xml, temp.directory).wasOk());

    CHECK (restored.getCycleRange() == juce::Range<juce::int64> { 96000, 288000 });
    CHECK (restored.isCycleEnabled());

    // Sessions from before there was a cycle have none.
    xml->removeAttribute ("cycleStart");
    xml->removeAttribute ("cycleEnd");
    xml->removeAttribute ("cycle");

    AudioEngine older;
    REQUIRE (applySessionXml (older, *xml, temp.directory).wasOk());
    CHECK (older.getCycleRange().isEmpty());
    CHECK_FALSE (older.isCycleEnabled());
}
