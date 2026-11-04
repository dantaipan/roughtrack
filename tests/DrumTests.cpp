#include "TestHelpers.h"

#include "roughtrack/DrumSampler.h"
#include "roughtrack/Session.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace roughtrack;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;
constexpr double samplesPerBeat = 24000.0;
constexpr int samplesPerBar = 96000;

struct Drums
{
    Drums()
    {
        test::writeTestDrums (temp.directory);
        library.load (temp.directory.getChildFile ("grooves.xml"));
        kit = DrumKit::load (temp.directory, sampleRate).kit;
        sampler.setKit (kit.get());
    }

    /** Renders from a position in blocks of a given size. */
    std::vector<float> render (int numSamples, int blockLength = blockSize, juce::int64 start = 0)
    {
        std::vector<float> output ((size_t) numSamples), left ((size_t) blockLength), right ((size_t) blockLength);
        sampler.reset();

        for (int done = 0; done < numSamples; done += blockLength)
        {
            const auto length = juce::jmin (blockLength, numSamples - done);
            sampler.render (left.data(), right.data(), length, start + done, samplesPerBeat);
            std::copy_n (left.begin(), length, output.begin() + done);
        }

        return output;
    }

    test::TempDirectory temp;
    GrooveLibrary library;
    std::unique_ptr<DrumKit> kit;
    DrumSampler sampler;
};

/** Where the output jumps, which is where a test recording starts. */
std::vector<int> findOnsets (const std::vector<float>& samples)
{
    std::vector<int> onsets;

    for (size_t i = 0; i < samples.size(); ++i)
        if (std::abs (samples[i]) > 0.01f)
            onsets.push_back ((int) i);

    return onsets;
}

struct EngineRig
{
    EngineRig()
    {
        test::writeTestDrums (temp.directory.getChildFile ("Drums"));
        engine.setSessionDirectory (temp.directory);
        engine.setDrumFolder (temp.directory.getChildFile ("Drums"));
        engine.prepare (sampleRate, blockSize);
        REQUIRE (engine.loadDrums().wasOk());
        track = engine.addTrack ("Drums", TrackType::drums);
    }

    std::vector<float> run (int numBlocks)
    {
        std::vector<float> output, input ((size_t) blockSize), left ((size_t) blockSize), right ((size_t) blockSize);
        const float* inputs[] { input.data() };

        for (int block = 0; block < numBlocks; ++block)
        {
            float* outputs[] { left.data(), right.data() };
            engine.processBlock (inputs, 1, outputs, 2, blockSize);
            output.insert (output.end(), left.begin(), left.end());
        }

        return output;
    }

    test::TempDirectory temp;
    AudioEngine engine;
    int track = -1;
};

} // namespace

TEST_CASE ("Grooves load in bars, with an early hit in the bar it leads into")
{
    Drums drums;

    const auto* beat = drums.library.find ("beat");
    REQUIRE (beat != nullptr);
    CHECK (beat->bars == 2);
    CHECK (beat->style == "Rock");
    CHECK_FALSE (beat->isFill);
    CHECK (beat->barStarts == std::vector<int> { 0, 2, 3 });
    CHECK (beat->hits[1].piece == DrumPiece::snare);
    CHECK (beat->hits[1].velocity == Catch::Approx (1.0f));

    const auto* push = drums.library.find ("push");
    REQUIRE (push != nullptr);
    CHECK (push->barStarts == std::vector<int> { 0, 1 });

    CHECK (drums.library.find ("fill")->isFill);
    CHECK (drums.library.find ("nothing") == nullptr);
}

TEST_CASE ("Moving a part stops at the start of the timeline")
{
    const DrumPartList parts { { "beat", 4, 8, 0 } };

    CHECK (movePart (parts, 0, 6)[0].startBar == 6);
    CHECK (movePart (parts, 0, -3)[0].startBar == 0);
}

TEST_CASE ("Trimming a part's start keeps the rest where it is")
{
    const auto trimmed = trimPartStart ({ { "beat", 4, 8, 0 } }, 0, 6);

    CHECK (trimmed[0].startBar == 6);
    CHECK (trimmed[0].bars == 6);
    CHECK (trimmed[0].offsetBars == 2);
    CHECK (trimmed[0].endBar() == 12);
}

TEST_CASE ("A part can't be trimmed away")
{
    CHECK (trimPartStart ({ { "beat", 4, 8, 0 } }, 0, 40)[0].bars == 1);
    CHECK (trimPartEnd ({ { "beat", 4, 8, 0 } }, 0, 2)[0].bars == 1);
}

TEST_CASE ("Splitting a part keeps both halves playing what they played")
{
    const auto split = splitPart ({ { "beat", 4, 8, 1 } }, 0, 7);

    REQUIRE (split.size() == 2);
    CHECK (split[0] == DrumPart { "beat", 4, 3, 1 });
    CHECK (split[1] == DrumPart { "beat", 7, 5, 4 });
}

TEST_CASE ("Splitting outside a part changes nothing")
{
    CHECK (splitPart ({ { "beat", 4, 8, 0 } }, 0, 4).size() == 1);
    CHECK (splitPart ({ { "beat", 4, 8, 0 } }, 0, 12).size() == 1);
}

TEST_CASE ("The newest part wins a bar")
{
    const DrumPartList parts { { "beat", 0, 8, 0 }, { "fill", 3, 1, 0 } };

    CHECK (findPartAt (parts, 2) == 0);
    CHECK (findPartAt (parts, 3) == 1);
    CHECK (findPartAt (parts, 4) == 0);
    CHECK (findPartAt (parts, 8) == -1);
}

TEST_CASE ("A harder hit uses a louder recording")
{
    Drums drums;

    const auto peakOf = [] (const DrumKit::Choice& choice) { return choice.audio->getSample (0, 0); };

    for (juce::uint32 seed = 0; seed < 8; ++seed)
        CHECK (peakOf (drums.kit->pick (DrumPiece::kick, 1.0f, seed)) >= peakOf (drums.kit->pick (DrumPiece::kick, 0.1f, seed)));

    const auto soft = drums.kit->pick (DrumPiece::kick, 0.3f, 0);
    const auto loud = drums.kit->pick (DrumPiece::kick, 1.0f, 0);

    CHECK (peakOf (soft) * soft.gain < peakOf (loud) * loud.gain);
}

TEST_CASE ("Level follows velocity whichever recording is chosen")
{
    Drums drums;

    for (juce::uint32 seed = 0; seed < 3; ++seed)
    {
        const auto choice = drums.kit->pick (DrumPiece::kick, 0.6f, seed);
        CHECK (choice.audio->getSample (0, 0) * choice.gain == Catch::Approx (std::pow (0.6f, 1.6f)).margin (0.01));
    }
}

TEST_CASE ("Repeated hits don't all use the same recording")
{
    Drums drums;

    std::set<const juce::AudioBuffer<float>*> used;

    for (juce::uint32 seed = 0; seed < 8; ++seed)
        used.insert (drums.kit->pick (DrumPiece::kick, 0.7f, seed).audio);

    CHECK (used.size() > 1);
}

TEST_CASE ("A drum the kit doesn't have borrows the nearest one")
{
    Drums drums;

    CHECK (drums.kit->pick (DrumPiece::sideStick, 1.0f, 0).audio != nullptr);
    CHECK (drums.kit->pick (DrumPiece::hiHatPedal, 1.0f, 0).audio != nullptr);
    CHECK (drums.kit->pick (DrumPiece::crash, 1.0f, 0).audio == nullptr);
}

TEST_CASE ("A kit recorded at another rate is converted when it loads")
{
    Drums drums;
    const auto converted = DrumKit::load (drums.temp.directory, sampleRate * 2.0).kit;

    REQUIRE (converted != nullptr);
    CHECK (converted->getSampleRate() == sampleRate * 2.0);
    CHECK (converted->pick (DrumPiece::kick, 1.0f, 0).audio->getNumSamples() == 400);
}

TEST_CASE ("A kit with a missing recording says which")
{
    Drums drums;
    drums.temp.directory.getChildFile ("snare.wav").deleteFile();

    const auto loaded = DrumKit::load (drums.temp.directory, sampleRate);

    CHECK (loaded.kit == nullptr);
    CHECK (loaded.error.contains ("snare.wav"));
}

TEST_CASE ("A hit lands on its exact sample at every buffer size")
{
    for (const auto blockLength : { 16, 64, 128, 333, 512, 1024 })
    {
        Drums drums;
        drums.sampler.setParts ({ { "beat", 0, 2, 0 } }, drums.library);

        CHECK (findOnsets (drums.render (2 * samplesPerBar, blockLength)) == std::vector<int> { 0, 24000, 96000 + 48000 });
    }
}

TEST_CASE ("A part loops when it's longer than its groove")
{
    Drums drums;
    drums.sampler.setParts ({ { "beat", 0, 4, 0 } }, drums.library);

    CHECK (findOnsets (drums.render (4 * samplesPerBar))
           == std::vector<int> { 0, 24000, 144000, 2 * samplesPerBar, 2 * samplesPerBar + 24000, 2 * samplesPerBar + 144000 });
}

TEST_CASE ("A trimmed part starts partway through its groove")
{
    Drums drums;
    drums.sampler.setParts ({ { "beat", 3, 1, 1 } }, drums.library);

    CHECK (findOnsets (drums.render (4 * samplesPerBar)) == std::vector<int> { 3 * samplesPerBar + 48000 });
}

TEST_CASE ("A push ahead of the bar plays before the part starts")
{
    Drums drums;
    drums.sampler.setParts ({ { "push", 1, 1, 0 } }, drums.library);

    CHECK (findOnsets (drums.render (2 * samplesPerBar)) == std::vector<int> { samplesPerBar - 2400 });
}

TEST_CASE ("A fill replaces the bar it covers")
{
    Drums drums;
    drums.sampler.setParts ({ { "beat", 0, 2, 0 }, { "fill", 1, 1, 0 } }, drums.library);

    CHECK (findOnsets (drums.render (2 * samplesPerBar)) == std::vector<int> { 0, 24000, samplesPerBar });
}

TEST_CASE ("Drums play the same on every pass")
{
    Drums drums;
    drums.sampler.setParts ({ { "beat", 0, 4, 0 } }, drums.library);

    CHECK (drums.render (2 * samplesPerBar) == drums.render (2 * samplesPerBar));
}

TEST_CASE ("Starting partway through a part plays only what's ahead")
{
    Drums drums;
    drums.sampler.setParts ({ { "beat", 0, 2, 0 } }, drums.library);

    CHECK (findOnsets (drums.render (samplesPerBar + 1000, blockSize, samplesPerBar / 2)) == std::vector<int> { 96000 });
}

TEST_CASE ("A part with a missing groove stays silent")
{
    Drums drums;
    drums.sampler.setParts ({ { "gone", 0, 2, 0 } }, drums.library);

    CHECK (findOnsets (drums.render (samplesPerBar)).empty());
}

TEST_CASE ("A closed hi-hat stops an open one ringing")
{
    Drums drums;
    drums.temp.directory.getChildFile ("grooves.xml").replaceWithText (
        R"(<GROOVES><GROOVE id="hats" style="Rock" kind="beat" bars="1" bpm="120" name="Hats" hits="0,4,127;0.5,3,127"/></GROOVES>)");
    GrooveLibrary library;
    REQUIRE (library.load (drums.temp.directory.getChildFile ("grooves.xml")));
    drums.sampler.setParts ({ { "hats", 0, 1, 0 } }, library);

    const auto output = drums.render (24000);

    CHECK (std::abs (output[11999]) > 0.1f);
    CHECK (std::abs (output[12000 + 1000]) < 0.001f);
}

TEST_CASE ("A drum track plays with the transport and stops with it")
{
    EngineRig rig;
    REQUIRE (rig.engine.setDrumParts (rig.track, { { "beat", 0, 2, 0 } }).wasOk());

    CHECK (findOnsets (rig.run (10)).empty());

    REQUIRE (rig.engine.play());

    CHECK (findOnsets (rig.run (3000)) == std::vector<int> { 0, 24000, 144000 });
}

TEST_CASE ("Drums follow the tempo")
{
    EngineRig rig;
    rig.engine.setTempo (60.0);
    REQUIRE (rig.engine.setDrumParts (rig.track, { { "beat", 0, 2, 0 } }).wasOk());
    REQUIRE (rig.engine.play());

    const auto onsets = findOnsets (rig.run (2000));

    REQUIRE (onsets.size() >= 2);
    CHECK (onsets[1] == 48000);
}

TEST_CASE ("Drums line up with the click")
{
    EngineRig rig;
    REQUIRE (rig.engine.setDrumParts (rig.track, { { "fill", 1, 1, 0 } }).wasOk());
    rig.engine.setPosition (samplesPerBar - blockSize);
    rig.engine.setClickEnabled (true);
    REQUIRE (rig.engine.play());

    rig.run (1);
    const auto output = rig.run (1);

    // The click starts its beat from zero, so the snare's spike is what shows here.
    CHECK (std::abs (output[0]) > 0.5f);
}

TEST_CASE ("A drum track can't be recorded on")
{
    EngineRig rig;
    rig.engine.setTrackArmed (rig.track, true);

    CHECK (rig.engine.record().failed());
    CHECK (rig.engine.getState() == TransportState::stopped);
}

TEST_CASE ("Parts can't change while the transport runs")
{
    EngineRig rig;
    REQUIRE (rig.engine.play());

    CHECK (rig.engine.setDrumParts (rig.track, { { "beat", 0, 2, 0 } }).failed());
}

TEST_CASE ("A removed drum track leaves nothing behind")
{
    EngineRig rig;
    REQUIRE (rig.engine.setDrumParts (rig.track, { { "beat", 0, 2, 0 } }).wasOk());
    REQUIRE (rig.engine.removeTrack (rig.track).wasOk());

    CHECK (rig.engine.getTrackType (rig.track) == TrackType::audio);
    CHECK (rig.engine.getDrumParts (rig.track).empty());
}

TEST_CASE ("Changing the sample rate converts the loaded kit")
{
    EngineRig rig;
    REQUIRE (rig.engine.setDrumParts (rig.track, { { "beat", 0, 2, 0 } }).wasOk());

    rig.engine.prepare (96000.0, blockSize);
    REQUIRE (rig.engine.play());

    const auto onsets = findOnsets (rig.run (1000));

    // Converting delays a hit by a sample or two, which is about 10 microseconds here.
    REQUIRE (onsets.size() >= 1);
    CHECK (onsets[0] <= 2);
}

TEST_CASE ("Drum tracks survive a save and reopen")
{
    EngineRig saved;
    saved.engine.setTrackName (saved.track, "Kit");
    REQUIRE (saved.engine.setDrumParts (saved.track, { { "beat", 2, 16, 1 }, { "fill", 9, 1, 0 } }).wasOk());

    const auto xml = createSessionXml (saved.engine, saved.temp.directory);

    AudioEngine reopened;
    reopened.setDrumFolder (saved.temp.directory.getChildFile ("Drums"));
    reopened.prepare (sampleRate, blockSize);

    REQUIRE (applySessionXml (reopened, *xml, saved.temp.directory).wasOk());

    CHECK (reopened.getTrackType (saved.track) == TrackType::drums);
    CHECK (reopened.getTrackName (saved.track) == "Kit");
    CHECK (reopened.getDrumParts (saved.track) == saved.engine.getDrumParts (saved.track));
}

TEST_CASE ("A part whose groove is gone is kept and reported")
{
    EngineRig saved;
    REQUIRE (saved.engine.setDrumParts (saved.track, { { "retired", 0, 8, 0 } }).wasOk());

    const auto xml = createSessionXml (saved.engine, saved.temp.directory);

    AudioEngine reopened;
    reopened.setDrumFolder (saved.temp.directory.getChildFile ("Drums"));
    reopened.prepare (sampleRate, blockSize);

    const auto result = applySessionXml (reopened, *xml, saved.temp.directory);

    CHECK (result.getErrorMessage().contains ("retired"));
    CHECK (reopened.getDrumParts (saved.track).size() == 1);
}

TEST_CASE ("Drum edits undo and redo")
{
    EngineRig rig;
    const DrumPartList before { { "beat", 0, 2, 0 } }, after { { "beat", 4, 2, 0 } };

    REQUIRE (rig.engine.setDrumParts (rig.track, before).wasOk());

    juce::UndoManager undo;
    undo.perform (new DrumEdit (rig.engine, rig.track, before, after));
    CHECK (rig.engine.getDrumParts (rig.track) == after);

    undo.undo();
    CHECK (rig.engine.getDrumParts (rig.track) == before);

    undo.redo();
    CHECK (rig.engine.getDrumParts (rig.track) == after);
}

TEST_CASE ("Older sessions open with audio tracks")
{
    test::TempDirectory temp;

    juce::XmlElement xml ("ROUGHTRACK_SESSION");
    auto* track = xml.createNewChildElement ("TRACK");
    track->setAttribute ("index", 0);
    track->setAttribute ("name", "Guitar 1");

    AudioEngine engine;
    engine.prepare (sampleRate, blockSize);

    REQUIRE (applySessionXml (engine, xml, temp.directory).wasOk());
    CHECK (engine.getTrackType (0) == TrackType::audio);
}
