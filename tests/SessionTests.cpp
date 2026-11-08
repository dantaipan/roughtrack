#include "TestHelpers.h"

#include "roughtrack/Session.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace roughtrack;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

/** Writes a take where every sample holds its own position, so what plays back
    proves exactly which part of the file was read.
*/
juce::File writeRamp (const juce::File& directory, const juce::String& name, int length, float scale = 1.0e-5f)
{
    juce::AudioBuffer<float> take (1, length);

    for (int i = 0; i < length; ++i)
        take.setSample (0, i, (float) (i + 1) * scale);

    const auto file = directory.getChildFile (name);
    test::writeWav (file, take, sampleRate);
    return file;
}

Clip makeClip (const juce::File& file, juce::int64 start, juce::int64 offset, juce::int64 length, juce::int64 fileLength = 48000)
{
    Clip clip;
    clip.file = file;
    clip.timelineStart = start;
    clip.fileOffset = offset;
    clip.length = length;
    clip.fileLength = fileLength;
    return clip;
}

struct Rig
{
    Rig()
    {
        test::useTracks (engine);
        engine.setSessionDirectory (temp.directory);
        engine.prepare (sampleRate, blockSize);
        engine.setRecordingLatency (0);
    }

    /** Plays from a position and returns what reached the left output. */
    std::vector<float> play (juce::int64 from, int numBlocks)
    {
        engine.setPosition (from);
        REQUIRE (engine.play());

        const float* inputs[] { input.data() };
        std::vector<float> out;

        for (int block = 0; block < numBlocks; ++block)
        {
            float* outputs[] { left.data(), right.data() };
            engine.processBlock (inputs, 1, outputs, 2, blockSize);
            out.insert (out.end(), left.begin(), left.end());
        }

        engine.stop();
        return out;
    }

    void record (float level, int numBlocks)
    {
        std::fill (input.begin(), input.end(), level);
        REQUIRE (engine.record().wasOk());

        const float* inputs[] { input.data() };

        for (int block = 0; block < numBlocks; ++block)
        {
            float* outputs[] { left.data(), right.data() };
            engine.processBlock (inputs, 1, outputs, 2, blockSize);
        }

        engine.stop();
        std::fill (input.begin(), input.end(), 0.0f);
    }

    test::TempDirectory temp;
    AudioEngine engine;
    std::vector<float> input = std::vector<float> ((size_t) blockSize, 0.0f);
    std::vector<float> left = std::vector<float> ((size_t) blockSize);
    std::vector<float> right = std::vector<float> ((size_t) blockSize);
};

} // namespace

TEST_CASE ("A trimmed, moved clip plays the right part of its file at the right time")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, "take.wav", 48000);

    REQUIRE (rig.engine.setClips (0, { makeClip (file, 200, 1000, 300) }).wasOk());

    const auto out = rig.play (0, 20);

    CHECK (out[199] == 0.0f);
    CHECK (out[200] > 0.0f);
    CHECK (out[300] == Catch::Approx (1101 * 1.0e-5f).margin (1.0e-5));
    CHECK (out[499] > 0.0f);
    CHECK (out[500] == 0.0f);
}

TEST_CASE ("Playback changes clip on the exact sample")
{
    Rig rig;
    const auto under = writeRamp (rig.temp.directory, "under.wav", 48000);
    const auto over = writeRamp (rig.temp.directory, "over.wav", 48000, -1.0e-5f);

    REQUIRE (rig.engine.setClips (0, { makeClip (under, 0, 0, 48000), makeClip (over, 100, 0, 50) }).wasOk());

    const auto out = rig.play (0, 10);

    CHECK (out[99] > 0.0f);
    CHECK (out[100] < 0.0f);
    CHECK (out[149] < 0.0f);
    CHECK (out[150] > 0.0f);
    CHECK (out[300] == Catch::Approx (301 * 1.0e-5f).margin (1.0e-5));
}

TEST_CASE ("A cut fades instead of clicking")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, "take.wav", 48000, 1.0e-4f);

    REQUIRE (rig.engine.setClips (0, { makeClip (file, 200, 5000, 400) }).wasOk());

    const auto out = rig.play (0, 20);

    CHECK (std::abs (out[200]) < 0.02f);
    CHECK (out[248] == Catch::Approx (0.2577f).margin (0.01));
    CHECK (out[396] == Catch::Approx (0.5197f).margin (1.0e-3));
    CHECK (std::abs (out[599]) < 0.02f);
}

TEST_CASE ("Punching in replaces only what was recorded over")
{
    Rig rig;
    const auto first = writeRamp (rig.temp.directory, "first.wav", 48000);

    REQUIRE (rig.engine.setClips (0, { makeClip (first, 0, 0, 48000) }).wasOk());
    rig.engine.setTrackArmed (0, true);
    rig.engine.setTrackInput (0, 0, 1);

    rig.engine.setPosition (6400);
    rig.record (0.5f, 100);

    REQUIRE (rig.engine.getClips (0).size() == 2);

    const auto out = rig.play (0, 300);

    CHECK (out[6300] == Catch::Approx (6301 * 1.0e-5f).margin (1.0e-5));
    CHECK (out[6400 + edgeFadeSamples] == Catch::Approx (0.5f).margin (1.0e-4));
    CHECK (out[12799 - edgeFadeSamples] == Catch::Approx (0.5f).margin (1.0e-4));
    CHECK (out[12800 + edgeFadeSamples] == Catch::Approx (12897 * 1.0e-5f).margin (1.0e-5));
}

TEST_CASE ("Clips can't change while the transport runs")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, "take.wav", 48000);

    REQUIRE (rig.engine.play());
    CHECK (rig.engine.setClips (0, { makeClip (file, 0, 0, 100) }).failed());
    rig.engine.stop();

    CHECK (rig.engine.getClips (0).empty());
}

TEST_CASE ("Undo puts an edit back, and redo reapplies it")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, "take.wav", 48000);
    const ClipList before { makeClip (file, 0, 0, 1000) };
    const auto after = moveClip (before, 0, 500);

    REQUIRE (rig.engine.setClips (0, before).wasOk());

    juce::UndoManager undoManager;
    undoManager.beginNewTransaction();
    REQUIRE (undoManager.perform (new ClipEdit (rig.engine, 0, before, after)));
    CHECK (rig.engine.getClips (0) == after);

    REQUIRE (undoManager.undo());
    CHECK (rig.engine.getClips (0) == before);

    REQUIRE (undoManager.redo());
    CHECK (rig.engine.getClips (0) == after);
}

TEST_CASE ("A session comes back with its clips and mixer settings")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, "take.wav", 48000);

    REQUIRE (rig.engine.setClips (3, { makeClip (file, 500, 100, 2000) }).wasOk());

    TrackSettings settings;
    settings.gain = 0.5f;
    settings.pan = -0.25f;
    settings.muted = true;
    settings.monitoring = true;
    settings.firstInput = 1;
    settings.inputChannels = 2;
    rig.engine.setTrackSettings (3, settings);
    rig.engine.setTempo (97.5);
    rig.engine.setClickEnabled (true);
    rig.engine.setMasterGain (0.75f);

    const auto xml = createSessionXml (rig.engine, rig.temp.directory);

    AudioEngine restored;
    restored.prepare (sampleRate, blockSize);

    REQUIRE (applySessionXml (restored, *xml, rig.temp.directory).wasOk());

    REQUIRE (restored.getClips (3).size() == 1);
    CHECK (restored.getClips (3)[0] == rig.engine.getClips (3)[0]);
    CHECK (restored.getTempo() == 97.5);
    CHECK (restored.isClickEnabled());
    CHECK (restored.getMasterGain() == 0.75f);

    const auto loaded = restored.getTrackSettings (3);

    CHECK (loaded.gain == 0.5f);
    CHECK (loaded.pan == -0.25f);
    CHECK (loaded.muted);
    CHECK (loaded.monitoring);
    CHECK (loaded.firstInput == 1);
    CHECK (loaded.inputChannels == 2);
}

TEST_CASE ("A session folder can be moved")
{
    Rig rig;
    test::TempDirectory other;

    const auto file = writeRamp (rig.temp.directory, "take.wav", 1000);
    REQUIRE (rig.engine.setClips (0, { makeClip (file, 0, 0, 1000, 1000) }).wasOk());

    const auto xml = createSessionXml (rig.engine, rig.temp.directory);
    file.copyFileTo (other.directory.getChildFile ("take.wav"));

    AudioEngine restored;
    restored.prepare (sampleRate, blockSize);

    REQUIRE (applySessionXml (restored, *xml, other.directory).wasOk());
    CHECK (restored.getClips (0)[0].file == other.directory.getChildFile ("take.wav"));
}

TEST_CASE ("A missing take is reported but stays on its track")
{
    Rig rig;

    const auto file = writeRamp (rig.temp.directory, "take.wav", 1000);
    REQUIRE (rig.engine.setClips (0, { makeClip (file, 0, 0, 1000, 1000) }).wasOk());

    const auto xml = createSessionXml (rig.engine, rig.temp.directory);
    file.deleteFile();

    AudioEngine restored;
    restored.prepare (sampleRate, blockSize);

    const auto result = applySessionXml (restored, *xml, rig.temp.directory);

    CHECK (result.failed());
    CHECK (result.getErrorMessage().contains ("take.wav"));
    CHECK (restored.getClips (0).size() == 1);
}

TEST_CASE ("A session at another sample rate still opens, and says why it sounds wrong")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, "take.wav", 48000);
    REQUIRE (rig.engine.setClips (0, { makeClip (file, 0, 0, 1000) }).wasOk());
    const auto xml = createSessionXml (rig.engine, rig.temp.directory);

    AudioEngine restored;
    restored.setSessionDirectory (rig.temp.directory);
    restored.prepare (44100.0, blockSize);

    // Opening it is fine; the warning stays with the engine for as long as the rates differ.
    CHECK (applySessionXml (restored, *xml, rig.temp.directory).wasOk());
    CHECK (restored.getSampleRateWarning().isNotEmpty());

    restored.prepare (48000.0, blockSize);
    CHECK (restored.getSampleRateWarning().isEmpty());
}

TEST_CASE ("Something that isn't a session is refused")
{
    AudioEngine engine;
    test::useTracks (engine);
    CHECK (applySessionXml (engine, juce::XmlElement ("NOT_A_SESSION"), juce::File()).failed());
}


TEST_CASE ("Tracks come back with their names, numbers and order")
{
    Rig rig;
    rig.engine.removeAllTracks();

    const auto first = rig.engine.addTrack ("Rhythm");
    const auto second = rig.engine.addTrack ("Lead");
    rig.engine.removeTrack (first);
    const auto third = rig.engine.addTrack ("Bass");

    const auto xml = createSessionXml (rig.engine, rig.temp.directory);

    AudioEngine restored;
    restored.prepare (sampleRate, blockSize);

    REQUIRE (applySessionXml (restored, *xml, rig.temp.directory).wasOk());

    CHECK (restored.getTrackOrder() == rig.engine.getTrackOrder());
    CHECK (restored.getTrackName (second) == "Lead");
    CHECK (restored.getTrackName (third) == "Bass");
}

TEST_CASE ("An empty track survives being saved")
{
    Rig rig;
    rig.engine.removeAllTracks();
    const auto track = rig.engine.addTrack ("Guitar 1");

    const auto xml = createSessionXml (rig.engine, rig.temp.directory);

    AudioEngine restored;
    restored.prepare (sampleRate, blockSize);

    REQUIRE (applySessionXml (restored, *xml, rig.temp.directory).wasOk());
    CHECK (restored.isTrackActive (track));
}

TEST_CASE ("A seven-track session opens with only the tracks it used")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, "take.wav", 1000);

    juce::XmlElement xml ("RIFFTRACK_SESSION"); // saved before the rename

    for (int track = 0; track < 7; ++track)
    {
        auto* trackXml = xml.createNewChildElement ("TRACK");
        trackXml->setAttribute ("index", track);

        if (track == 2)
        {
            auto* clip = trackXml->createNewChildElement ("CLIP");
            clip->setAttribute ("file", file.getFileName());
            clip->setAttribute ("length", 1000);
            clip->setAttribute ("fileLength", 1000);
        }
    }

    AudioEngine restored;
    restored.prepare (sampleRate, blockSize);

    REQUIRE (applySessionXml (restored, xml, rig.temp.directory).wasOk());

    CHECK (restored.getTrackOrder() == std::vector<int> { 2 });
    CHECK (restored.getTrackName (2) == "Guitar 1");
}

TEST_CASE ("Tracks can be reordered, and the order is saved with the session")
{
    test::TempDirectory temp;
    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    engine.activateTrack (0, "Guitar");
    engine.activateTrack (1, "Bass");
    engine.activateTrack (2, "Keys", TrackType::midi);

    engine.moveTrack (2, 0);
    CHECK (engine.getTrackOrder() == std::vector<int> { 2, 0, 1 });

    engine.moveTrack (2, 99);
    CHECK (engine.getTrackOrder() == std::vector<int> { 0, 1, 2 });

    engine.moveTrack (0, 1);
    CHECK (engine.getTrackOrder() == std::vector<int> { 1, 0, 2 });

    // A track that isn't in use is ignored.
    engine.moveTrack (5, 0);
    CHECK (engine.getTrackOrder() == std::vector<int> { 1, 0, 2 });

    const auto xml = createSessionXml (engine, temp.directory);

    AudioEngine restored;
    restored.setSessionDirectory (temp.directory);
    REQUIRE (applySessionXml (restored, *xml, temp.directory).wasOk());
    CHECK (restored.getTrackOrder() == std::vector<int> { 1, 0, 2 });
    CHECK (restored.getTrackName (1) == "Bass");
    CHECK (restored.getTrackType (2) == TrackType::midi);
}

TEST_CASE ("A session with takes keeps its rate when the interface changes")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, "take.wav", 48000);
    REQUIRE (rig.engine.setClips (0, { makeClip (file, 0, 0, 1000) }).wasOk());

    const auto saved = createSessionXml (rig.engine, rig.temp.directory);
    REQUIRE (saved->getDoubleAttribute ("sampleRate") == 48000.0);

    // Another app, such as GarageBand, has the interface at 44.1 kHz.
    AudioEngine reopened;
    reopened.setSessionDirectory (rig.temp.directory);
    reopened.prepare (44100.0, blockSize);
    applySessionXml (reopened, *saved, rig.temp.directory);

    CHECK (reopened.getSessionSampleRate() == 48000.0);

    const auto warning = reopened.getSampleRateWarning();
    CHECK (warning.contains ("48 kHz"));
    CHECK (warning.contains ("44.1 kHz"));
    CHECK (warning.contains ("slow and flat"));

    // Saving now mustn't mark the session as 44.1 kHz, or it would be wrong at 48 kHz too.
    CHECK (createSessionXml (reopened, rig.temp.directory)->getDoubleAttribute ("sampleRate") == 48000.0);
}

TEST_CASE ("A session without audio takes follows the interface")
{
    Rig rig;
    juce::XmlElement xml ("ROUGHTRACK_SESSION");
    xml.setAttribute ("sampleRate", 44100.0);

    rig.engine.removeAllTracks();
    applySessionXml (rig.engine, xml, rig.temp.directory);

    CHECK (rig.engine.getSessionSampleRate() == 0.0);
    CHECK (rig.engine.getSampleRateWarning().isEmpty());
    CHECK (createSessionXml (rig.engine, rig.temp.directory)->getDoubleAttribute ("sampleRate") == 48000.0);
}

TEST_CASE ("The first audio take sets the session's rate")
{
    Rig rig;
    CHECK (rig.engine.getSessionSampleRate() == 0.0);

    rig.engine.setTrackArmed (0, true);
    rig.record (0.1f, 4);

    CHECK (rig.engine.getSessionSampleRate() == 48000.0);
}

TEST_CASE ("Audio isn't recorded while the interface runs at another rate")
{
    Rig rig;
    rig.engine.setSessionSampleRate (48000.0);
    rig.engine.prepare (44100.0, blockSize);
    rig.engine.setTrackArmed (0, true);

    const auto takesBefore = rig.temp.directory.getNumberOfChildFiles (juce::File::findFiles, "*.wav");
    const auto result = rig.engine.record();

    CHECK (result.failed());
    CHECK (result.getErrorMessage().contains ("Audio Settings"));
    CHECK (rig.engine.getState() == TransportState::stopped);
    CHECK (rig.temp.directory.getNumberOfChildFiles (juce::File::findFiles, "*.wav") == takesBefore);
}

TEST_CASE ("MIDI can still be recorded while the interface runs at another rate")
{
    Rig rig;
    rig.engine.setSessionSampleRate (48000.0);
    rig.engine.prepare (44100.0, blockSize);

    // Notes are kept in beats, so they don't depend on the rate.
    const auto keys = rig.engine.addTrack ("Keys", TrackType::midi);
    rig.engine.setTrackArmed (keys, true);

    CHECK (rig.engine.record().wasOk());
    rig.engine.stop();
}

TEST_CASE ("Mixdowns wait until the interface matches the session's rate")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, "take.wav", 48000);
    REQUIRE (rig.engine.setClips (0, { makeClip (file, 0, 0, 1000) }).wasOk());

    rig.engine.setSessionSampleRate (48000.0);
    rig.engine.prepare (44100.0, blockSize);

    const auto mix = rig.engine.bounce (rig.temp.directory.getChildFile ("mix.wav"), 0, 1000);
    const auto stems = rig.engine.bounceStems (rig.temp.directory.getChildFile ("stems"), 0, 1000);

    CHECK (mix.result.failed());
    CHECK (stems.result.failed());
    CHECK_FALSE (rig.temp.directory.getChildFile ("mix.wav").exists());

    rig.engine.prepare (48000.0, blockSize);
    CHECK (rig.engine.bounce (rig.temp.directory.getChildFile ("mix.wav"), 0, 1000).result.wasOk());
}

TEST_CASE ("Clearing the tracks forgets the session's rate")
{
    Rig rig;
    rig.engine.setSessionSampleRate (48000.0);
    rig.engine.removeAllTracks();

    CHECK (rig.engine.getSessionSampleRate() == 0.0);
}

TEST_CASE ("A clip's fades are heard and saved with the session")
{
    Rig rig;

    juce::AudioBuffer<float> steady (1, 48000);
    for (int i = 0; i < steady.getNumSamples(); ++i)
        steady.setSample (0, i, 0.5f);

    const auto file = rig.temp.directory.getChildFile ("steady.wav");
    test::writeWav (file, steady, sampleRate);

    auto clip = makeClip (file, 0, 0, 4000);
    clip.fadeIn = 1000;
    clip.fadeOut = 1000;
    REQUIRE (rig.engine.setClips (0, { clip }).wasOk());

    const auto out = rig.play (0, 70);

    CHECK (std::abs (out[1]) < 0.01f);
    CHECK (out[500] == Catch::Approx (0.5f * 0.7071f).margin (0.005));
    CHECK (out[2000] == Catch::Approx (0.5f).margin (1.0e-4));
    CHECK (out[3500] == Catch::Approx (0.5f * 0.7071f).margin (0.005));
    CHECK (std::abs (out[3998]) < 0.01f);

    const auto xml = createSessionXml (rig.engine, rig.temp.directory);

    AudioEngine restored;
    restored.prepare (sampleRate, blockSize);
    REQUIRE (applySessionXml (restored, *xml, rig.temp.directory).wasOk());
    REQUIRE (restored.getClips (0).size() == 1);
    CHECK (restored.getClips (0)[0].fadeIn == 1000);
    CHECK (restored.getClips (0)[0].fadeOut == 1000);
}

TEST_CASE ("A clip saved without fades loads with none")
{
    Rig rig;
    const auto file = writeRamp (rig.temp.directory, "take.wav", 48000);
    REQUIRE (rig.engine.setClips (0, { makeClip (file, 0, 0, 2000) }).wasOk());

    const auto xml = createSessionXml (rig.engine, rig.temp.directory);
    CHECK_FALSE (xml->toString().contains ("fadeIn"));

    AudioEngine restored;
    restored.prepare (sampleRate, blockSize);
    REQUIRE (applySessionXml (restored, *xml, rig.temp.directory).wasOk());
    CHECK (restored.getClips (0)[0].fadeIn == 0);
    CHECK (restored.getClips (0)[0].fadeOut == 0);
}

TEST_CASE ("A new session starts with default song settings, not the last session's")
{
    AudioEngine engine;
    engine.setTempo (97.5);
    engine.setClickEnabled (true);
    engine.setMasterGain (0.5f);
    engine.setCycleRange ({ 96000, 288000 });
    engine.setCycleEnabled (true);

    engine.resetSessionSettings();

    CHECK (engine.getTempo() == AudioEngine::defaultTempo);
    CHECK_FALSE (engine.isClickEnabled());
    CHECK (engine.getMasterGain() == 1.0f);
    CHECK (engine.getCycleRange().isEmpty());
    CHECK_FALSE (engine.isCycleEnabled());
}
