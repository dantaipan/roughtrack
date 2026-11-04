#include "BuildDrumKit.h"

#include "roughtrack/Drums.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <iostream>

namespace
{

constexpr double kitSampleRate = 48000.0;
constexpr int layersPerDrum = 14;
constexpr int layersPerCymbal = 10;
constexpr int bitsPerSample = 24;
constexpr float onsetThreshold = 0.1f;
constexpr int samplesKeptBeforeOnset = 12;
constexpr float silenceThresholdDb = -62.0f;
constexpr double fadeOutSeconds = 0.05;
constexpr double levelWindowSeconds = 0.03;
constexpr int polarityCheckSamples = 2000;
constexpr float headroomDb = 6.0f;

struct Microphone
{
    const char* channel;
    float gain;
    float pan;
};

struct KitPiece
{
    roughtrack::DrumPiece piece;
    const char* instrument;
    float loudestDb;
    double maximumSeconds;
    bool isCymbal;
    std::vector<Microphone> closeMicrophones;
};

/** Left and right are as the drummer sees the kit. */
const std::vector<Microphone> roomMicrophones { { "OHL", 0.7f, -1.0f }, { "OHR", 0.7f, 1.0f }, { "AmbL", 0.45f, -1.0f }, { "AmbR", 0.45f, 1.0f } };

const std::vector<KitPiece>& getKitPieces()
{
    using Piece = roughtrack::DrumPiece;

    static const std::vector<KitPiece> pieces {
        { Piece::kick,        "Kdrum_with_contact", -11.0f, 1.0, false, { { "Kdrum_back", 1.0f, 0.0f }, { "Kdrum_front", 0.6f, 0.0f } } },
        { Piece::snare,       "Snare",              -11.0f, 1.2, false, { { "Snare_top", 1.0f, 0.0f }, { "Snare_bottom", 0.4f, 0.0f } } },
        { Piece::sideStick,   "Snare_rim",          -17.0f, 0.6, false, { { "Snare_top", 1.0f, 0.0f } } },
        { Piece::hiHatClosed, "Hihat_closed",       -21.0f, 0.8, true,  { { "Hihat", 0.7f, -0.45f } } },
        { Piece::hiHatOpen,   "Hihat_open",         -19.0f, 3.0, true,  { { "Hihat", 0.7f, -0.45f } } },
        { Piece::hiHatPedal,  "Hihat_foot",         -25.0f, 0.6, true,  { { "Hihat", 0.7f, -0.45f } } },
        { Piece::tomHigh,     "Tom1",               -13.0f, 1.5, false, { { "Tom1", 1.0f, -0.25f } } },
        { Piece::tomMid,      "Tom2",               -13.0f, 1.8, false, { { "Tom2", 1.0f, 0.2f } } },
        { Piece::tomLow,      "Tom3",               -13.0f, 2.2, false, { { "Tom3", 1.0f, 0.5f } } },
        { Piece::crash,       "Crash_left_tip",     -17.0f, 5.0, true,  {} },
        { Piece::crash2,      "Crash_right_tip",    -17.0f, 5.0, true,  {} },
        { Piece::ride,        "Ride_tip",           -22.0f, 4.0, true,  { { "Ride", 0.7f, 0.45f } } },
        { Piece::rideBell,    "Ride_tip_bell",      -20.0f, 4.0, true,  { { "Ride", 0.7f, 0.45f } } },
    };

    return pieces;
}

using ChannelFiles = std::map<juce::String, juce::File>;

/** The kit's hits for one drum, softest first, each with its file for every microphone. */
std::vector<ChannelFiles> readHits (const juce::File& kitFolder, const KitPiece& piece)
{
    const auto folder = kitFolder.getChildFile (piece.instrument);
    const auto xml = juce::parseXML (folder.getChildFile (juce::String (piece.instrument) + ".xml"));

    std::vector<ChannelFiles> hits;

    if (xml == nullptr)
        return hits;

    for (auto* sample : xml->getChildByName ("samples")->getChildWithTagNameIterator ("sample"))
    {
        ChannelFiles files;

        for (auto* audio : sample->getChildWithTagNameIterator ("audiofile"))
            files[audio->getStringAttribute ("channel")] = folder.getChildFile (audio->getStringAttribute ("file"));

        hits.push_back (std::move (files));
    }

    return hits;
}

juce::AudioBuffer<float> readMono (juce::AudioFormatManager& formats, const juce::File& file)
{
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return {};

    juce::AudioBuffer<float> audio (1, (int) reader->lengthInSamples);
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, false);
    return audio;
}

/** Two mics facing opposite sides of a drum move in opposite directions, and would
    cancel. Returns -1 if this one needs flipping to match the first.
*/
float findPolarity (const juce::AudioBuffer<float>& reference, const juce::AudioBuffer<float>& microphone)
{
    const auto length = juce::jmin (polarityCheckSamples, reference.getNumSamples(), microphone.getNumSamples());
    auto correlation = 0.0f;

    for (int i = 0; i < length; ++i)
        correlation += reference.getSample (0, i) * microphone.getSample (0, i);

    return correlation < 0.0f ? -1.0f : 1.0f;
}

juce::AudioBuffer<float> mixHit (juce::AudioFormatManager& formats, const ChannelFiles& files, const KitPiece& piece, float& polarityOfSecondMic)
{
    juce::AudioBuffer<float> mix;
    juce::AudioBuffer<float> firstClose;

    const auto addMicrophone = [&] (const Microphone& microphone, float polarity)
    {
        const auto audio = readMono (formats, files.at (microphone.channel));

        if (mix.getNumSamples() == 0)
        {
            mix.setSize (2, audio.getNumSamples());
            mix.clear();
        }

        const auto angle = (microphone.pan + 1.0f) * juce::MathConstants<float>::pi / 4.0f;
        const auto length = juce::jmin (audio.getNumSamples(), mix.getNumSamples());

        mix.addFrom (0, 0, audio, 0, 0, length, microphone.gain * polarity * std::cos (angle) * juce::MathConstants<float>::sqrt2);
        mix.addFrom (1, 0, audio, 0, 0, length, microphone.gain * polarity * std::sin (angle) * juce::MathConstants<float>::sqrt2);
        return audio;
    };

    for (size_t i = 0; i < piece.closeMicrophones.size(); ++i)
    {
        if (i == 0)
        {
            firstClose = addMicrophone (piece.closeMicrophones[i], 1.0f);
        }
        else
        {
            polarityOfSecondMic = findPolarity (firstClose, readMono (formats, files.at (piece.closeMicrophones[i].channel)));
            addMicrophone (piece.closeMicrophones[i], polarityOfSecondMic);
        }
    }

    for (const auto& microphone : roomMicrophones)
        addMicrophone (microphone, 1.0f);

    return mix;
}

int findOnset (const juce::AudioBuffer<float>& audio)
{
    const auto threshold = audio.getMagnitude (0, audio.getNumSamples()) * onsetThreshold;

    for (int i = 0; i < audio.getNumSamples(); ++i)
        if (std::abs (audio.getSample (0, i)) >= threshold || std::abs (audio.getSample (1, i)) >= threshold)
            return i;

    return 0;
}

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source, double sourceRate)
{
    const auto ratio = sourceRate / kitSampleRate;
    const auto length = (int) std::ceil (source.getNumSamples() / ratio);

    juce::AudioBuffer<float> result (2, length);

    for (int channel = 0; channel < 2; ++channel)
    {
        juce::WindowedSincInterpolator interpolator;
        interpolator.process (ratio, source.getReadPointer (channel), result.getWritePointer (channel), length, source.getNumSamples(), 0);
    }

    return result;
}

/** Starts a hit on its first sound, so it plays exactly when it's told to, and
    ends it when it has died away.
*/
juce::AudioBuffer<float> trimHit (const juce::AudioBuffer<float>& audio, double maximumSeconds)
{
    const auto start = juce::jmax (0, findOnset (audio) - samplesKeptBeforeOnset);
    const auto silence = juce::Decibels::decibelsToGain (silenceThresholdDb) * audio.getMagnitude (0, audio.getNumSamples());

    auto end = juce::jmin (audio.getNumSamples(), start + (int) (maximumSeconds * kitSampleRate));

    while (end > start + 1 && std::abs (audio.getSample (0, end - 1)) < silence && std::abs (audio.getSample (1, end - 1)) < silence)
        --end;

    const auto fadeLength = juce::jmin (end - start, (int) (fadeOutSeconds * kitSampleRate));

    juce::AudioBuffer<float> trimmed (2, end - start);

    for (int channel = 0; channel < 2; ++channel)
        trimmed.copyFrom (channel, 0, audio, channel, start, end - start);

    trimmed.applyGainRamp (trimmed.getNumSamples() - fadeLength, fadeLength, 1.0f, 0.0f);
    return trimmed;
}

float measureLevel (const juce::AudioBuffer<float>& audio)
{
    const auto length = juce::jmin (audio.getNumSamples(), (int) (levelWindowSeconds * kitSampleRate));
    return juce::jmax (audio.getRMSLevel (0, 0, length), audio.getRMSLevel (1, 0, length));
}

bool writeFlac (const juce::File& file, const juce::AudioBuffer<float>& audio)
{
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (kitSampleRate)
                                                         .withNumChannels (2)
                                                         .withBitsPerSample (bitsPerSample);

    auto writer = juce::FlacAudioFormat().createWriterFor (stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

} // namespace

int runBuildDrumKit (const juce::File& kitFolder, const juce::File& outputFolder)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    outputFolder.createDirectory();

    juce::XmlElement kitXml ("DRUMKIT");
    kitXml.setAttribute ("name", "DRSKit");

    for (const auto& piece : getKitPieces())
    {
        const auto hits = readHits (kitFolder, piece);

        if (hits.empty())
        {
            std::cout << "No hits found for " << piece.instrument << std::endl;
            return 1;
        }

        const auto layers = juce::jmin ((int) hits.size(), piece.isCymbal ? layersPerCymbal : layersPerDrum);

        std::vector<juce::AudioBuffer<float>> mixes;
        auto polarity = 1.0f;

        for (int layer = 0; layer < layers; ++layer)
        {
            const auto hit = juce::roundToInt ((double) layer * (double) (hits.size() - 1) / juce::jmax (1, layers - 1));
            const auto mix = mixHit (formats, hits[(size_t) hit], piece, polarity);
            mixes.push_back (trimHit (resample (mix, 44100.0), piece.maximumSeconds));
        }

        auto loudest = 0.0f;

        for (const auto& mix : mixes)
            loudest = juce::jmax (loudest, measureLevel (mix));

        const auto gain = juce::Decibels::decibelsToGain (piece.loudestDb - headroomDb) / loudest;
        const juce::String name (roughtrack::getPieceName (piece.piece));

        for (int layer = 0; layer < layers; ++layer)
        {
            auto& mix = mixes[(size_t) layer];
            mix.applyGain (gain);

            const auto fileName = name + "-" + juce::String (layer + 1).paddedLeft ('0', 2) + ".flac";

            if (! writeFlac (outputFolder.getChildFile (fileName), mix))
            {
                std::cout << "Couldn't write " << fileName << std::endl;
                return 1;
            }

            auto* element = kitXml.createNewChildElement ("SAMPLE");
            element->setAttribute ("piece", name);
            element->setAttribute ("file", fileName);
            element->setAttribute ("level", measureLevel (mix));
        }

        std::cout << name << ": " << layers << " of " << hits.size() << " hits, "
                  << mixes.back().getNumSamples() / kitSampleRate << " s, softest "
                  << juce::Decibels::gainToDecibels (measureLevel (mixes.front())) << " dB, peak "
                  << juce::Decibels::gainToDecibels (mixes.back().getMagnitude (0, mixes.back().getNumSamples())) << " dB"
                  << (polarity < 0.0f ? ", second mic flipped" : "") << std::endl;
    }

    return kitXml.writeTo (outputFolder.getChildFile ("kit.xml")) ? 0 : 1;
}
