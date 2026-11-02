#include "TestHelpers.h"

#include "rifftrack/Session.h"
#include "TimelineView.h"

#include <catch2/catch_test_macros.hpp>

using namespace rifftrack;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;
constexpr int samplesPerBeat = 24000;
constexpr double samplesPerTick = 25.0;

MidiEvent makeEvent (juce::int64 tick, int status, int data1, int data2)
{
    MidiEvent event;
    event.tick = tick;
    event.data[0] = (juce::uint8) status;
    event.data[1] = (juce::uint8) data1;
    event.data[2] = (juce::uint8) data2;
    return event;
}

/** Note 60 at beat 0 for one beat, and note 64 at beat 2 for one beat. */
std::shared_ptr<const MidiTake> makeTestTake()
{
    return makeMidiTake ({ makeEvent (0, 0x90, 60, 100), makeEvent (960, 0x80, 60, 0),
                           makeEvent (1920, 0x90, 64, 90), makeEvent (2880, 0x80, 64, 0) }, 3840);
}

Clip makeMidiClip (const juce::File& file, juce::int64 start, juce::int64 length, juce::int64 offset = 0, juce::int64 fileLength = 3840)
{
    Clip clip;
    clip.file = file;
    clip.timelineStart = start;
    clip.length = length;
    clip.fileOffset = offset;
    clip.fileLength = fileLength;
    return clip;
}

/** Sounds a spike on each note-on as tall as the note's velocity, and logs what it's sent. */
class TestSynth final : public juce::AudioPluginInstance
{
public:
    TestSynth() : AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())) {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            received.push_back ({ samplesProcessed + metadata.samplePosition, message });

            if (message.isNoteOn())
                buffer.setSample (0, metadata.samplePosition, message.getFloatVelocity());
        }

        samplesProcessed += buffer.getNumSamples();
    }

    struct Received
    {
        juce::int64 sample;
        juce::MidiMessage message;
    };

    std::vector<Received> notesOn() const
    {
        std::vector<Received> notes;

        for (const auto& entry : received)
            if (entry.message.isNoteOn())
                notes.push_back (entry);

        return notes;
    }

    int countNotesOff (int pitch) const
    {
        auto count = 0;

        for (const auto& entry : received)
            count += entry.message.isNoteOff() && entry.message.getNoteNumber() == pitch ? 1 : 0;

        return count;
    }

    void getStateInformation (juce::MemoryBlock& block) override { block.replaceAll (&state, sizeof (state)); }

    void setStateInformation (const void* data, int size) override
    {
        if (size == (int) sizeof (state))
            std::memcpy (&state, data, sizeof (state));
    }

    void fillInPluginDescription (juce::PluginDescription& description) const override
    {
        description.name = "Test Synth";
        description.pluginFormatName = "Test";
        description.fileOrIdentifier = "synth";
        description.isInstrument = true;
    }

    const juce::String getName() const override { return "Test Synth"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    std::vector<Received> received;
    juce::int64 samplesProcessed = 0;
    int state = 0;
};

struct Rig
{
    Rig()
    {
        engine.setSessionDirectory (temp.directory);
        engine.prepare (sampleRate, blockSize);
        engine.setRecordingLatency (0);
        track = engine.addTrack ("Keys", TrackType::midi);

        auto instrument = std::make_unique<TestSynth>();
        synth = instrument.get();
        REQUIRE (engine.setInsert (track, instrumentSlot, std::move (instrument)).wasOk());
    }

    juce::File writeTake (const juce::String& name = "keys.mid")
    {
        const auto file = temp.directory.getChildFile (name);
        REQUIRE (writeMidiTake (*makeTestTake(), file));
        return file;
    }

    void run (int numBlocks, std::vector<float>* output = nullptr)
    {
        std::vector<float> input ((size_t) blockSize), left ((size_t) blockSize), right ((size_t) blockSize);
        const float* inputs[] { input.data() };

        for (int block = 0; block < numBlocks; ++block)
        {
            float* outputs[] { left.data(), right.data() };
            engine.processBlock (inputs, 1, outputs, 2, blockSize);

            if (output != nullptr)
                output->insert (output->end(), left.begin(), left.end());
        }
    }

    void press (int pitch, int velocity = 100)
    {
        engine.handleIncomingMidiMessage (nullptr, juce::MidiMessage::noteOn (1, pitch, (juce::uint8) velocity));
    }

    void release (int pitch)
    {
        engine.handleIncomingMidiMessage (nullptr, juce::MidiMessage::noteOff (1, pitch));
    }

    test::TempDirectory temp;
    AudioEngine engine;
    TestSynth* synth = nullptr;
    int track = -1;
};

int blocksFor (int samples)
{
    return samples / blockSize + 1;
}

} // namespace

TEST_CASE ("A MIDI take survives being saved")
{
    test::TempDirectory temp;
    const auto file = temp.directory.getChildFile ("take.mid");

    REQUIRE (writeMidiTake (*makeTestTake(), file));

    const auto loaded = readMidiTake (file);

    REQUIRE (loaded != nullptr);
    CHECK (loaded->length == 3840);
    REQUIRE (loaded->notes.size() == 2);
    CHECK (loaded->notes[1].tick == 1920);
    CHECK (loaded->notes[1].length == 960);
    CHECK (loaded->notes[1].pitch == 64);
}

TEST_CASE ("Controllers and pitch bends are kept")
{
    test::TempDirectory temp;
    const auto file = temp.directory.getChildFile ("take.mid");

    REQUIRE (writeMidiTake (*makeMidiTake ({ makeEvent (10, 0xb0, 64, 127), makeEvent (20, 0xe0, 0, 64), makeEvent (30, 0xc0, 5, 0) }, 100), file));

    const auto loaded = readMidiTake (file);

    REQUIRE (loaded != nullptr);
    REQUIRE (loaded->events.size() == 3);
    CHECK (loaded->events[0].data[1] == 64);
    CHECK (loaded->events[1].getSize() == 3);
    CHECK (loaded->events[2].getSize() == 2);
}

TEST_CASE ("A file from elsewhere is read at its own resolution")
{
    test::TempDirectory temp;
    const auto file = temp.directory.getChildFile ("other.mid");

    juce::MidiMessageSequence sequence;
    sequence.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 480.0);
    sequence.addEvent (juce::MidiMessage::noteOff (1, 60), 960.0);

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (480);
    midi.addTrack (sequence);

    {
        juce::FileOutputStream stream (file);
        REQUIRE (midi.writeTo (stream));
    }

    const auto loaded = readMidiTake (file);

    REQUIRE (loaded != nullptr);
    REQUIRE (loaded->notes.size() == 1);
    CHECK (loaded->notes[0].tick == 960);
}

TEST_CASE ("A note still held at the end of a take is closed")
{
    const auto take = makeMidiTake ({ makeEvent (100, 0x90, 60, 100) }, 500);

    REQUIRE (take->notes.size() == 1);
    CHECK (take->notes[0].length == 400);
    REQUIRE (take->events.size() == 2);
    CHECK (take->events[1].tick == 500);
    CHECK ((take->events[1].data[0] & 0xf0) == 0x80);
}

TEST_CASE ("A note-on with no velocity ends a note")
{
    const auto take = makeMidiTake ({ makeEvent (0, 0x90, 60, 100), makeEvent (240, 0x90, 60, 0) }, 500);

    REQUIRE (take->notes.size() == 1);
    CHECK (take->notes[0].length == 240);
}

TEST_CASE ("Every tick lands in exactly one block")
{
    for (const auto sampleLength : { 3.0, 25.0, 25.7, 50.25 })
    {
        auto previous = sampleToTick (0, sampleLength);

        for (juce::int64 sample = 64; sample < 100000; sample += 64)
        {
            const auto next = sampleToTick (sample, sampleLength);
            CHECK (next >= previous);
            previous = next;
        }

        CHECK (sampleToTick (0, sampleLength) == 0);
    }
}

TEST_CASE ("MIDI lands on its exact sample at every buffer size")
{
    for (const auto blockLength : { 16, 64, 128, 333, 512 })
    {
        MidiSequencer sequencer;
        sequencer.setClips ({ makeMidiClip ({}, 0, 3840) }, { makeTestTake() });

        juce::MidiBuffer buffer;
        std::vector<juce::int64> notes;

        for (juce::int64 done = 0; done < 96000; done += blockLength)
        {
            buffer.clear();
            sequencer.render (buffer, blockLength, done, samplesPerTick);

            for (const auto metadata : buffer)
                if (metadata.getMessage().isNoteOn())
                    notes.push_back (done + metadata.samplePosition);
        }

        CHECK (notes == std::vector<juce::int64> { 0, 2 * samplesPerBeat });
    }
}

TEST_CASE ("A MIDI track plays its clips through its instrument")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (rig.track, { makeMidiClip (rig.writeTake(), 960, 3840) }).wasOk());
    REQUIRE (rig.engine.play());

    std::vector<float> output;
    rig.run (blocksFor (4 * samplesPerBeat), &output);

    const auto notes = rig.synth->notesOn();

    REQUIRE (notes.size() == 2);
    CHECK (notes[0].sample == samplesPerBeat);
    CHECK (notes[0].message.getNoteNumber() == 60);
    CHECK (notes[1].sample == 3 * samplesPerBeat);
    CHECK (std::abs (output[(size_t) samplesPerBeat]) > 0.5f);
}

TEST_CASE ("MIDI follows the tempo")
{
    Rig rig;
    rig.engine.setTempo (60.0);
    REQUIRE (rig.engine.setClips (rig.track, { makeMidiClip (rig.writeTake(), 0, 3840) }).wasOk());
    REQUIRE (rig.engine.play());

    rig.run (blocksFor (4 * samplesPerBeat + 100));

    const auto notes = rig.synth->notesOn();

    REQUIRE (notes.size() == 2);
    CHECK (notes[1].sample == 4 * samplesPerBeat);
}

TEST_CASE ("A trimmed clip plays only what it shows")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (rig.track, { makeMidiClip (rig.writeTake(), 1920, 1920, 1920) }).wasOk());
    REQUIRE (rig.engine.play());

    rig.run (blocksFor (4 * samplesPerBeat));

    const auto notes = rig.synth->notesOn();

    REQUIRE (notes.size() == 1);
    CHECK (notes[0].message.getNoteNumber() == 64);
    CHECK (notes[0].sample == 2 * samplesPerBeat);
}

TEST_CASE ("A note cut off by the end of its clip doesn't hang")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (rig.track, { makeMidiClip (rig.writeTake(), 0, 480) }).wasOk());
    REQUIRE (rig.engine.play());

    rig.run (blocksFor (samplesPerBeat));

    CHECK (rig.synth->countNotesOff (60) == 1);
}

TEST_CASE ("A note held across a split rings on")
{
    Rig rig;
    const auto clips = splitClip ({ makeMidiClip (rig.writeTake(), 0, 3840) }, 0, 480);

    REQUIRE (clips.size() == 2);
    REQUIRE (rig.engine.setClips (rig.track, clips).wasOk());
    REQUIRE (rig.engine.play());

    rig.run (blocksFor (480 * 25 + 500));

    CHECK (rig.synth->countNotesOff (60) == 0);
}

TEST_CASE ("Stopping ends the notes that are sounding")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (rig.track, { makeMidiClip (rig.writeTake(), 0, 3840) }).wasOk());
    REQUIRE (rig.engine.play());

    rig.run (blocksFor (samplesPerBeat / 2));
    rig.engine.stop();
    rig.run (2);

    CHECK (rig.synth->countNotesOff (60) == 1);
}

TEST_CASE ("A later clip plays over an earlier one")
{
    Rig rig;
    const auto first = rig.writeTake ("a.mid");
    const auto second = rig.temp.directory.getChildFile ("b.mid");

    REQUIRE (writeMidiTake (*makeMidiTake ({ makeEvent (0, 0x90, 72, 100), makeEvent (100, 0x80, 72, 0) }, 960), second));
    REQUIRE (rig.engine.setClips (rig.track, { makeMidiClip (first, 0, 3840), makeMidiClip (second, 1920, 960, 0, 960) }).wasOk());
    REQUIRE (rig.engine.play());

    rig.run (blocksFor (4 * samplesPerBeat));

    const auto notes = rig.synth->notesOn();

    REQUIRE (notes.size() == 2);
    CHECK (notes[0].message.getNoteNumber() == 60);
    CHECK (notes[1].message.getNoteNumber() == 72);
}

TEST_CASE ("A keyboard only reaches an armed track")
{
    Rig rig;

    rig.press (60);
    rig.run (2);
    CHECK (rig.synth->notesOn().empty());

    rig.engine.setTrackArmed (rig.track, true);
    rig.press (62);
    rig.run (2);

    REQUIRE (rig.synth->notesOn().size() == 1);
    CHECK (rig.synth->notesOn()[0].message.getNoteNumber() == 62);
}

TEST_CASE ("A keyboard plays while the transport is stopped")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);

    std::vector<float> output;
    rig.press (60);
    rig.run (1, &output);

    CHECK (std::abs (output[0]) > 0.5f);
    CHECK (rig.engine.getState() == TransportState::stopped);
}

TEST_CASE ("Disarming a track ends what the keyboard was holding")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    rig.press (60);
    rig.run (1);

    rig.engine.setTrackArmed (rig.track, false);
    rig.run (1);

    CHECK (rig.synth->countNotesOff (60) == 1);
}

TEST_CASE ("Clock messages from a keyboard are ignored")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);

    for (int i = 0; i < 5000; ++i)
        rig.engine.handleIncomingMidiMessage (nullptr, juce::MidiMessage::midiClock());

    rig.press (60);
    rig.run (1);

    CHECK (rig.synth->received.size() == 1);
}

TEST_CASE ("Recording a keyboard makes a clip and a MIDI file")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    rig.engine.setPosition (samplesPerBeat);

    REQUIRE (rig.engine.record().wasOk());

    rig.run (blocksFor (samplesPerBeat / 2));
    rig.press (60);
    rig.run (blocksFor (samplesPerBeat / 2));
    rig.release (60);
    rig.run (blocksFor (samplesPerBeat));
    rig.engine.stop();

    const auto& clips = rig.engine.getClips (rig.track);

    REQUIRE (clips.size() == 1);
    CHECK (clips[0].timelineStart == 960);
    CHECK (clips[0].file.getFileName() == "Keys-take.mid");
    CHECK (clips[0].length > 1900);

    const auto take = readMidiTake (clips[0].file);

    REQUIRE (take != nullptr);
    REQUIRE (take->notes.size() == 1);
    CHECK (take->notes[0].pitch == 60);
    CHECK (std::abs (take->notes[0].tick - 480) < 4);
    CHECK (std::abs (take->notes[0].length - 480) < 4);
}

TEST_CASE ("Punching in on a MIDI track ends its part's notes and records from the playhead")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (rig.track, { makeMidiClip (rig.writeTake(), 0, 3840) }).wasOk());
    rig.engine.setTrackArmed (rig.track, true);

    REQUIRE (rig.engine.play());
    rig.run (blocksFor (samplesPerBeat / 4));
    REQUIRE (rig.synth->notesOn().size() == 1);

    const auto punch = rig.engine.getPosition();
    REQUIRE (rig.engine.record().wasOk());
    rig.run (1);
    CHECK (rig.synth->countNotesOff (60) >= 1);

    rig.press (62);
    rig.run (blocksFor (samplesPerBeat / 2));
    rig.release (62);
    rig.run (blocksFor (samplesPerBeat * 2));
    rig.engine.stop();

    // The old part's second note fell inside the punch, so it never played.
    for (const auto& note : rig.synth->notesOn())
        CHECK (note.message.getNoteNumber() != 64);

    const auto& clips = rig.engine.getClips (rig.track);

    REQUIRE (clips.size() == 2);
    CHECK (std::abs (clips[1].timelineStart - punch * 960 / samplesPerBeat) <= 1);
}

TEST_CASE ("A recorded note is placed where the player heard it")
{
    constexpr int latency = 1000;

    Rig rig;
    rig.engine.setRecordingLatency (latency);
    rig.engine.setTrackArmed (rig.track, true);

    REQUIRE (rig.engine.record().wasOk());

    rig.run (blocksFor (samplesPerBeat));
    const auto pressedAt = rig.engine.getPosition();
    rig.press (60);
    rig.run (blocksFor (1000));
    rig.engine.stop();

    const auto take = readMidiTake (rig.engine.getClips (rig.track)[0].file);

    REQUIRE (take != nullptr);
    REQUIRE (take->notes.size() == 1);
    CHECK (std::abs (take->notes[0].tick - (pressedAt - latency / 2) / 25) <= 1);
}

TEST_CASE ("A note held when recording stops is closed in the file")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    REQUIRE (rig.engine.record().wasOk());

    rig.press (60);
    rig.run (blocksFor (samplesPerBeat));
    rig.engine.stop();

    const auto take = readMidiTake (rig.engine.getClips (rig.track)[0].file);

    REQUIRE (take != nullptr);
    REQUIRE (take->notes.size() == 1);
    CHECK (take->notes[0].length > 900);
}

TEST_CASE ("Recording with nothing played leaves no clip")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    REQUIRE (rig.engine.record().wasOk());

    rig.run (10);
    rig.engine.stop();

    CHECK (rig.engine.getClips (rig.track).empty());
    CHECK (rig.temp.directory.findChildFiles (juce::File::findFiles, false, "*.mid").isEmpty());
}

TEST_CASE ("A recorded take plays back in time")
{
    Rig rig;
    rig.engine.setTrackArmed (rig.track, true);
    REQUIRE (rig.engine.record().wasOk());

    rig.run (blocksFor (samplesPerBeat));
    const auto pressedAt = rig.engine.getPosition();
    rig.press (60);
    rig.run (2);
    rig.release (60);
    rig.run (blocksFor (samplesPerBeat));
    rig.engine.stop();
    rig.engine.setTrackArmed (rig.track, false);
    rig.engine.setPosition (0);
    rig.run (2);

    rig.synth->received.clear();
    rig.synth->samplesProcessed = 0;

    REQUIRE (rig.engine.play());
    rig.run (blocksFor (2 * samplesPerBeat));

    const auto notes = rig.synth->notesOn();

    REQUIRE (notes.size() == 1);
    CHECK (std::abs (notes[0].sample - pressedAt) <= 25);
}

TEST_CASE ("MIDI recording doesn't disturb audio tracks recording beside it")
{
    Rig rig;
    const auto guitar = rig.engine.addTrack ("Guitar");

    rig.engine.setTrackArmed (rig.track, true);
    rig.engine.setTrackArmed (guitar, true);
    REQUIRE (rig.engine.record().wasOk());

    rig.press (60);
    rig.run (100);
    rig.engine.stop();

    CHECK (rig.engine.getClips (rig.track).size() == 1);
    CHECK (rig.engine.getClips (guitar).size() == 1);
}

TEST_CASE ("MIDI tracks survive a save and reopen")
{
    Rig saved;
    const auto clip = makeMidiClip (saved.writeTake(), 960, 1920, 240);

    REQUIRE (saved.engine.setClips (saved.track, { clip }).wasOk());
    saved.synth->state = 42;

    const auto xml = createSessionXml (saved.engine, saved.temp.directory);
    const auto inserts = parseSavedInserts (*saved.engine.createInsertsXml());

    REQUIRE (inserts.size() == 1);
    CHECK (inserts[0].slot == instrumentSlot);

    AudioEngine reopened;
    reopened.prepare (sampleRate, blockSize);

    REQUIRE (applySessionXml (reopened, *xml, saved.temp.directory).wasOk());

    CHECK (reopened.getTrackType (saved.track) == TrackType::midi);
    CHECK (reopened.getClips (saved.track) == saved.engine.getClips (saved.track));
    CHECK (reopened.findMidiTake (clip.file) != nullptr);
}

TEST_CASE ("An instrument saves with the session's plugins")
{
    Rig rig;
    rig.synth->state = 42;

    const auto xml = rig.engine.createInsertsXml();
    const auto* element = xml->getChildByName ("INSERT");

    REQUIRE (element != nullptr);
    CHECK (element->getIntAttribute ("slot") == instrumentSlot);
    CHECK (element->getIntAttribute ("track") == rig.track);
}

TEST_CASE ("A missing MIDI file stays on its track, silent")
{
    Rig rig;
    REQUIRE (rig.engine.setClips (rig.track, { makeMidiClip (rig.temp.directory.getChildFile ("gone.mid"), 0, 3840) }).wasOk());
    REQUIRE (rig.engine.play());

    rig.run (100);

    CHECK (rig.synth->notesOn().empty());
    CHECK (rig.engine.getClips (rig.track).size() == 1);
}

TEST_CASE ("Clip edits undo on a MIDI track")
{
    Rig rig;
    const auto file = rig.writeTake();
    const ClipList before { makeMidiClip (file, 0, 3840) };
    const auto after = moveClip (before, 0, 1920);

    REQUIRE (rig.engine.setClips (rig.track, before).wasOk());

    juce::UndoManager undo;
    undo.perform (new ClipEdit (rig.engine, rig.track, before, after));
    CHECK (rig.engine.getClips (rig.track) == after);

    undo.undo();
    CHECK (rig.engine.getClips (rig.track) == before);
}

TEST_CASE ("A removed MIDI track gives up its instrument")
{
    Rig rig;
    REQUIRE (rig.engine.removeTrack (rig.track).wasOk());

    CHECK (rig.engine.getInsert (rig.track, instrumentSlot) == nullptr);
    CHECK (rig.engine.getTrackType (rig.track) == TrackType::audio);
}

TEST_CASE ("A slow instrument stays in time")
{
    constexpr int instrumentDelay = 1000;

    Rig rig;
    rig.synth->setLatencySamples (instrumentDelay);
    REQUIRE (rig.engine.setClips (rig.track, { makeMidiClip (rig.writeTake(), 960, 3840) }).wasOk());
    REQUIRE (rig.engine.play());

    rig.run (blocksFor (2 * samplesPerBeat));

    REQUIRE_FALSE (rig.synth->notesOn().empty());
    CHECK (rig.synth->notesOn()[0].sample == samplesPerBeat - instrumentDelay);
}

TEST_CASE ("Dragging a MIDI clip snaps to sixteenths")
{
    constexpr int laneHeight = 76;

    test::TempDirectory temp;
    AudioEngine engine;
    TimelineView view (engine, laneHeight);

    engine.prepare (sampleRate, blockSize);
    engine.setSessionDirectory (temp.directory);
    view.setSize (800, TimelineView::rulerHeight + laneHeight + TimelineView::scrollBarHeight);

    const auto track = engine.addTrack ("Keys", TrackType::midi);
    const auto file = temp.directory.getChildFile ("keys.mid");

    REQUIRE (writeMidiTake (*makeTestTake(), file));
    REQUIRE (engine.setClips (track, { makeMidiClip (file, 0, 3840) }).wasOk());

    view.onEdit = [&] (int edited, const ClipList& clips, const juce::String&) { engine.setClips (edited, clips); };

    const auto makeEventAt = [&] (juce::Point<float> position, juce::Point<float> downAt, juce::ModifierKeys modifiers)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position, modifiers, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &view, &view, juce::Time::getCurrentTime(), downAt, juce::Time::getCurrentTime(), 1, false);
    };

    const auto y = (float) TimelineView::rulerHeight + 30.0f;
    const juce::Point<float> from { 20.0f, y }, to { 33.0f, y };

    // At 40 pixels a second and 120 BPM a beat is 20 pixels, so this is 0.65 of a
    // beat, which rounds to three sixteenths.
    view.mouseDown (makeEventAt (from, from, juce::ModifierKeys::leftButtonModifier));
    view.mouseDrag (makeEventAt (to, from, juce::ModifierKeys::leftButtonModifier));
    view.mouseUp (makeEventAt (to, from, {}));

    CHECK (engine.getClips (track)[0].timelineStart == 720);
}

namespace
{

std::vector<juce::int64> noteTicks (const MidiTake& take)
{
    std::vector<juce::int64> ticks;

    for (const auto& note : take.notes)
        ticks.push_back (note.tick);

    return ticks;
}

using Ticks = std::vector<juce::int64>;

/** Slightly loose: notes near beats 0 and 2. */
juce::File writeLooseTake (Rig& rig)
{
    const auto file = rig.temp.directory.getChildFile ("keys.mid");
    REQUIRE (writeMidiTake (*makeMidiTake ({ makeEvent (30, 0x90, 60, 100), makeEvent (900, 0x80, 60, 0),
                                             makeEvent (1950, 0x90, 64, 90), makeEvent (2800, 0x80, 64, 0) }, 3840), file));
    return file;
}

} // namespace

TEST_CASE ("Quantizing moves notes to the nearest line and keeps their lengths")
{
    const auto played = makeMidiTake ({ makeEvent (30, 0x90, 60, 100), makeEvent (330, 0x80, 60, 0),
                                        makeEvent (1400, 0x90, 62, 100), makeEvent (1500, 0x80, 62, 0) }, 3840);

    const auto take = quantizeTake (*played, 0, 3840, 0, ticksPerBeat / 2);

    CHECK (noteTicks (*take) == Ticks { 0, 1440 });
    CHECK (take->notes[0].length == 300);
    CHECK (take->notes[1].length == 100);
    CHECK (take->length == 3840);
}

TEST_CASE ("Quantizing lands triplets for a shuffle")
{
    const auto played = makeMidiTake ({ makeEvent (5, 0x90, 60, 100), makeEvent (100, 0x80, 60, 0),
                                        makeEvent (610, 0x90, 60, 100), makeEvent (700, 0x80, 60, 0) }, 960);

    CHECK (noteTicks (*quantizeTake (*played, 0, 960, 0, ticksPerBeat / 3)) == Ticks { 0, 640 });
}

TEST_CASE ("The quantize grid follows the clip on the timeline, not the file")
{
    const auto played = makeMidiTake ({ makeEvent (100, 0x90, 60, 100), makeEvent (200, 0x80, 60, 0) }, 960);

    // Moved to tick 50, the note is at 150 on the timeline, so the beat's sixteenth
    // at 240 is nearest: tick 190 in the take.
    CHECK (noteTicks (*quantizeTake (*played, 0, 960, 50, ticksPerBeat / 4)) == Ticks { 190 });
}

TEST_CASE ("Quantizing leaves notes outside the clip alone")
{
    const auto take = quantizeTake (*makeMidiTake ({ makeEvent (10, 0x90, 60, 100), makeEvent (20, 0x80, 60, 0),
                                                     makeEvent (1000, 0x90, 62, 100), makeEvent (1010, 0x80, 62, 0),
                                                     makeEvent (2000, 0x90, 64, 100), makeEvent (2010, 0x80, 64, 0) }, 3840),
                                    960, 1920, 0, ticksPerBeat);

    CHECK (noteTicks (*take) == Ticks { 10, 960, 2000 });
}

TEST_CASE ("Quantizing keeps notes inside their clip")
{
    const auto played = makeMidiTake ({ makeEvent (305, 0x90, 60, 100), makeEvent (320, 0x80, 60, 0),
                                        makeEvent (900, 0x90, 62, 100), makeEvent (950, 0x80, 62, 0) }, 960);

    // The nearest lines, 0 and 960, are outside, and so is every other beat.
    CHECK (noteTicks (*quantizeTake (*played, 300, 950, 0, ticksPerBeat)) == Ticks { 305, 900 });

    // On eighths, 305 is nearest 480 anyway, and 900 steps back from 960 to 480.
    // Both land together but they're different pitches, so both stay.
    CHECK (noteTicks (*quantizeTake (*played, 300, 950, 0, ticksPerBeat / 2)) == Ticks { 480, 480 });
}

TEST_CASE ("Quick repeats of a note stay playable when quantized")
{
    SECTION ("landing together makes one note, as loud as the louder")
    {
        const auto take = quantizeTake (*makeMidiTake ({ makeEvent (10, 0x90, 60, 70), makeEvent (50, 0x80, 60, 0),
                                                         makeEvent (60, 0x90, 60, 120), makeEvent (300, 0x80, 60, 0) }, 960),
                                        0, 960, 0, ticksPerBeat);

        REQUIRE (take->notes.size() == 1);
        CHECK (take->notes[0].tick == 0);
        CHECK (take->notes[0].length == 240);
        CHECK (juce::roundToInt (take->notes[0].velocity * 127.0f) == 120);
    }

    SECTION ("a long note stops where the next one starts")
    {
        const auto take = quantizeTake (*makeMidiTake ({ makeEvent (0, 0x90, 60, 100), makeEvent (470, 0x80, 60, 0),
                                                         makeEvent (470, 0x90, 60, 100), makeEvent (600, 0x80, 60, 0) }, 960),
                                        0, 960, 0, ticksPerBeat / 2);

        REQUIRE (take->notes.size() == 2);
        CHECK (take->notes[0].length == 470);
        CHECK (take->notes[1].tick == 480);
    }

    SECTION ("the first of two neighbours is cut short when they close up")
    {
        const auto take = quantizeTake (*makeMidiTake ({ makeEvent (10, 0x90, 60, 100), makeEvent (500, 0x80, 60, 0),
                                                         makeEvent (500, 0x90, 60, 100), makeEvent (600, 0x80, 60, 0) }, 960),
                                        0, 960, 0, ticksPerBeat / 2);

        REQUIRE (take->notes.size() == 2);
        CHECK (take->notes[0].length == 480);
        CHECK (take->notes[1].tick == 480);
    }
}

TEST_CASE ("Quantizing leaves pedal and pitch bend where they were played")
{
    const auto take = quantizeTake (*makeMidiTake ({ makeEvent (10, 0x90, 60, 100), makeEvent (15, 0xb0, 64, 127),
                                                     makeEvent (20, 0xe0, 0, 64), makeEvent (50, 0x80, 60, 0) }, 960),
                                    0, 960, 0, ticksPerBeat);

    CHECK (std::any_of (take->events.begin(), take->events.end(), [] (const MidiEvent& e) { return e.tick == 15 && e.data[0] == 0xb0; }));
    CHECK (std::any_of (take->events.begin(), take->events.end(), [] (const MidiEvent& e) { return e.tick == 20 && e.data[0] == 0xe0; }));
    CHECK (take->notes[0].tick == 0);
}

TEST_CASE ("A note ending past the take stretches it, so the file keeps the end")
{
    const auto take = quantizeTake (*makeMidiTake ({ makeEvent (940, 0x90, 60, 100), makeEvent (1000, 0x80, 60, 0) }, 1000),
                                    0, 1000, 0, ticksPerBeat);

    CHECK (take->notes[0].tick == 960);
    CHECK (take->length == 1020);
}

TEST_CASE ("Quantizing a clip keeps the take as played and can be undone")
{
    Rig rig;
    const auto file = writeLooseTake (rig);
    juce::MemoryBlock played, afterwards;
    REQUIRE (file.loadFileAsData (played));
    const ClipList before { makeMidiClip (file, 0, 3840) };

    REQUIRE (rig.engine.setClips (rig.track, before).wasOk());

    const auto after = rig.engine.quantizeClip (rig.track, 0, ticksPerBeat);

    REQUIRE (after[0].file != file);
    CHECK (after[0].file.getFileName() == "keys-quantized.mid");
    CHECK (after[0].file.existsAsFile());
    REQUIRE (file.loadFileAsData (afterwards));
    CHECK (afterwards == played);
    CHECK (rig.engine.getClips (rig.track) == before);

    juce::UndoManager undo;
    undo.perform (new ClipEdit (rig.engine, rig.track, before, after));
    CHECK (rig.engine.getClips (rig.track) == after);

    REQUIRE (rig.engine.play());
    rig.run (5000);
    rig.engine.stop();

    REQUIRE (rig.synth->notesOn().size() == 2);
    CHECK (rig.synth->notesOn()[0].sample == 0);
    CHECK (rig.synth->notesOn()[1].sample == 2 * samplesPerBeat);

    undo.undo();
    CHECK (rig.engine.getClips (rig.track) == before);
}

TEST_CASE ("Quantizing one half of a split clip leaves the other half as played")
{
    Rig rig;
    const auto file = writeLooseTake (rig);
    const auto clips = splitClip ({ makeMidiClip (file, 0, 3840) }, 0, 1920);

    REQUIRE (rig.engine.setClips (rig.track, clips).wasOk());

    const auto after = rig.engine.quantizeClip (rig.track, 1, ticksPerBeat);

    CHECK (after[0] == clips[0]);
    CHECK (after[1].file != file);
    CHECK (noteTicks (*rig.engine.findMidiTake (after[1].file)) == Ticks { 30, 1920 });
}

TEST_CASE ("Quantizing twice starts a new file from the same name")
{
    Rig rig;
    const auto file = writeLooseTake (rig);

    REQUIRE (rig.engine.setClips (rig.track, { makeMidiClip (file, 0, 3840) }).wasOk());

    const auto first = rig.engine.quantizeClip (rig.track, 0, ticksPerBeat);
    REQUIRE (rig.engine.setClips (rig.track, first).wasOk());

    const auto second = rig.engine.quantizeClip (rig.track, 0, ticksPerBeat / 2);

    CHECK (second[0].file.getFileName() == "keys-quantized2.mid");
}

TEST_CASE ("A quantized take comes back with the session")
{
    Rig rig;
    const auto file = writeLooseTake (rig);

    REQUIRE (rig.engine.setClips (rig.track, { makeMidiClip (file, 0, 3840) }).wasOk());

    const auto after = rig.engine.quantizeClip (rig.track, 0, ticksPerBeat);
    const auto reopened = readMidiTake (after[0].file);

    REQUIRE (reopened != nullptr);
    CHECK (noteTicks (*reopened) == Ticks { 0, 1920 });
    CHECK (reopened->length == after[0].fileLength);
}

TEST_CASE ("A clip whose take is missing can't be quantized")
{
    Rig rig;
    const ClipList clips { makeMidiClip (rig.temp.directory.getChildFile ("gone.mid"), 0, 3840) };

    REQUIRE (rig.engine.setClips (rig.track, clips).wasOk());
    CHECK (rig.engine.quantizeClip (rig.track, 0, ticksPerBeat) == clips);
}

TEST_CASE ("A quantize from the timeline is one undoable edit")
{
    constexpr int laneHeight = 76;

    test::TempDirectory temp;
    AudioEngine engine;
    TimelineView view (engine, laneHeight);

    engine.prepare (sampleRate, blockSize);
    engine.setSessionDirectory (temp.directory);

    const auto track = engine.addTrack ("Keys", TrackType::midi);
    const auto file = temp.directory.getChildFile ("keys.mid");

    REQUIRE (writeMidiTake (*makeTestTake(), file));
    REQUIRE (engine.setClips (track, { makeMidiClip (file, 0, 3840) }).wasOk());

    auto edits = 0;
    juce::String editName;

    view.onEdit = [&] (int edited, const ClipList& clips, const juce::String& name)
    {
        ++edits;
        editName = name;
        engine.setClips (edited, clips);
    };

    view.quantizeClip (track, 0, ticksPerBeat);

    CHECK (edits == 1);
    CHECK (editName == "Quantize");
    CHECK (engine.getClips (track)[0].file != file);
}
