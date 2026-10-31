#include "rifftrack/Session.h"

namespace rifftrack
{

namespace
{
constexpr int sessionVersion = 1;
} // namespace

std::unique_ptr<juce::XmlElement> createSessionXml (const AudioEngine& engine, const juce::File& sessionDirectory)
{
    auto xml = std::make_unique<juce::XmlElement> ("RIFFTRACK_SESSION");
    xml->setAttribute ("version", sessionVersion);
    xml->setAttribute ("sampleRate", engine.getSampleRate());
    xml->setAttribute ("tempo", engine.getTempo());
    xml->setAttribute ("click", engine.isClickEnabled());
    xml->setAttribute ("master", (double) engine.getMasterGain());

    auto* auxXml = xml->createNewChildElement ("AUX");

    for (int i = 0; i < (int) AuxParameter::count; ++i)
    {
        const auto parameter = (AuxParameter) i;
        auxXml->setAttribute (getParameterInfo (parameter).id, (double) engine.getAux().getParameter (parameter));
    }

    for (int track = 0; track < numTracks; ++track)
    {
        const auto settings = engine.getTrackSettings (track);

        auto* trackXml = xml->createNewChildElement ("TRACK");
        trackXml->setAttribute ("index", track);
        trackXml->setAttribute ("gain", (double) settings.gain);
        trackXml->setAttribute ("pan", (double) settings.pan);
        trackXml->setAttribute ("muted", settings.muted);
        trackXml->setAttribute ("soloed", settings.soloed);
        trackXml->setAttribute ("monitoring", settings.monitoring);
        trackXml->setAttribute ("firstInput", settings.firstInput);
        trackXml->setAttribute ("inputChannels", settings.inputChannels);

        const auto& channel = engine.getChannel (track);
        auto* channelXml = trackXml->createNewChildElement ("CHANNEL");

        for (int i = 0; i < (int) ChannelParameter::count; ++i)
        {
            const auto parameter = (ChannelParameter) i;
            channelXml->setAttribute (getParameterInfo (parameter).id, (double) channel.getParameter (parameter));
        }

        // Captures and IRs stay in the player's own library, so these are full paths.
        channelXml->setAttribute ("amp", channel.getAmpFile().getFullPathName());
        channelXml->setAttribute ("cabinet", channel.getCabinetFile().getFullPathName());

        for (const auto& clip : engine.getClips (track))
        {
            auto* clipXml = trackXml->createNewChildElement ("CLIP");
            clipXml->setAttribute ("file", clip.file.getRelativePathFrom (sessionDirectory));
            clipXml->setAttribute ("start", juce::String (clip.timelineStart));
            clipXml->setAttribute ("offset", juce::String (clip.fileOffset));
            clipXml->setAttribute ("length", juce::String (clip.length));
            clipXml->setAttribute ("fileLength", juce::String (clip.fileLength));
        }
    }

    return xml;
}

juce::Result applySessionXml (AudioEngine& engine, const juce::XmlElement& xml, const juce::File& sessionDirectory)
{
    if (! xml.hasTagName ("RIFFTRACK_SESSION"))
        return juce::Result::fail ("That isn't a RiffTrack session.");

    engine.setTempo (xml.getDoubleAttribute ("tempo", engine.getTempo()));
    engine.setClickEnabled (xml.getBoolAttribute ("click"));
    engine.setMasterGain ((float) xml.getDoubleAttribute ("master", 1.0));

    juce::StringArray problems;

    if (const auto* auxXml = xml.getChildByName ("AUX"))
    {
        for (int i = 0; i < (int) AuxParameter::count; ++i)
        {
            const auto parameter = (AuxParameter) i;
            const auto& info = getParameterInfo (parameter);
            engine.getAux().setParameter (parameter, (float) auxXml->getDoubleAttribute (info.id, info.defaultValue));
        }
    }

    for (auto* trackXml : xml.getChildWithTagNameIterator ("TRACK"))
    {
        const auto track = trackXml->getIntAttribute ("index", -1);

        if (! juce::isPositiveAndBelow (track, numTracks))
            continue;

        TrackSettings settings;
        settings.gain = (float) trackXml->getDoubleAttribute ("gain", 1.0);
        settings.pan = (float) trackXml->getDoubleAttribute ("pan", 0.0);
        settings.muted = trackXml->getBoolAttribute ("muted");
        settings.soloed = trackXml->getBoolAttribute ("soloed");
        settings.monitoring = trackXml->getBoolAttribute ("monitoring");
        settings.firstInput = trackXml->getIntAttribute ("firstInput", 0);
        settings.inputChannels = trackXml->getIntAttribute ("inputChannels", 1);
        engine.setTrackSettings (track, settings);

        if (const auto* channelXml = trackXml->getChildByName ("CHANNEL"))
        {
            auto& channel = engine.getChannel (track);

            for (int i = 0; i < (int) ChannelParameter::count; ++i)
            {
                const auto parameter = (ChannelParameter) i;
                const auto& info = getParameterInfo (parameter);
                channel.setParameter (parameter, (float) channelXml->getDoubleAttribute (info.id, info.defaultValue));
            }

            const juce::File ampFile (channelXml->getStringAttribute ("amp"));
            const juce::File cabinetFile (channelXml->getStringAttribute ("cabinet"));

            if (channelXml->getStringAttribute ("amp").isNotEmpty())
            {
                const auto loaded = engine.loadAmp (track, ampFile);

                if (loaded.failed())
                    problems.addIfNotAlreadyThere (ampFile.getFileName() + " (" + loaded.getErrorMessage() + ")");
            }

            if (channelXml->getStringAttribute ("cabinet").isNotEmpty() && channel.loadCabinet (cabinetFile).failed())
                problems.addIfNotAlreadyThere (cabinetFile.getFileName());
        }

        ClipList clips;

        for (auto* clipXml : trackXml->getChildWithTagNameIterator ("CLIP"))
        {
            Clip clip;
            clip.file = sessionDirectory.getChildFile (clipXml->getStringAttribute ("file"));
            clip.timelineStart = clipXml->getStringAttribute ("start").getLargeIntValue();
            clip.fileOffset = clipXml->getStringAttribute ("offset").getLargeIntValue();
            clip.length = clipXml->getStringAttribute ("length").getLargeIntValue();
            clip.fileLength = clipXml->getStringAttribute ("fileLength").getLargeIntValue();

            if (! clip.file.existsAsFile())
                problems.addIfNotAlreadyThere (clip.file.getFileName());

            clips.push_back (clip);
        }

        const auto result = engine.setClips (track, clips);

        if (result.failed())
            return result;
    }

    const auto savedRate = xml.getDoubleAttribute ("sampleRate");

    if (savedRate > 0.0 && engine.getSampleRate() > 0.0 && ! juce::approximatelyEqual (savedRate, engine.getSampleRate()))
        return juce::Result::fail ("This session was recorded at " + juce::String (savedRate) + " Hz, but the device is running at "
                                   + juce::String (engine.getSampleRate()) + " Hz, so it will play at the wrong pitch.");

    if (! problems.isEmpty())
        return juce::Result::fail ("Couldn't load: " + problems.joinIntoString (", "));

    return juce::Result::ok();
}

ClipEdit::ClipEdit (AudioEngine& engineToEdit, int trackToEdit, ClipList clipsBefore, ClipList clipsAfter)
    : engine (engineToEdit), track (trackToEdit), before (std::move (clipsBefore)), after (std::move (clipsAfter))
{
}

bool ClipEdit::perform()
{
    return engine.setClips (track, after).wasOk();
}

bool ClipEdit::undo()
{
    return engine.setClips (track, before).wasOk();
}

} // namespace rifftrack
