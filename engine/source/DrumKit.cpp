#include "rifftrack/DrumKit.h"

#include <algorithm>
#include <cmath>

namespace rifftrack
{

namespace
{

constexpr int numNearbySamples = 3;
constexpr float velocityCurve = 1.6f;
constexpr float minimumGain = 0.05f;
constexpr float maximumGain = 2.0f;

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source, double ratio)
{
    const auto length = (int) std::ceil (source.getNumSamples() / ratio);
    juce::AudioBuffer<float> result (source.getNumChannels(), length);

    for (int channel = 0; channel < source.getNumChannels(); ++channel)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio, source.getReadPointer (channel), result.getWritePointer (channel), length, source.getNumSamples(), 0);
    }

    return result;
}

/** A missing drum falls back to the nearest one the kit has. */
DrumPiece getFallback (DrumPiece piece) noexcept
{
    switch (piece)
    {
        case DrumPiece::sideStick:  return DrumPiece::snare;
        case DrumPiece::hiHatOpen:  return DrumPiece::hiHatClosed;
        case DrumPiece::hiHatPedal: return DrumPiece::hiHatClosed;
        case DrumPiece::crash2:     return DrumPiece::crash;
        case DrumPiece::rideBell:   return DrumPiece::ride;
        case DrumPiece::tomMid:     return DrumPiece::tomHigh;
        case DrumPiece::tomLow:     return DrumPiece::tomMid;
        case DrumPiece::kick:
        case DrumPiece::snare:
        case DrumPiece::hiHatClosed:
        case DrumPiece::tomHigh:
        case DrumPiece::crash:
        case DrumPiece::ride:
        case DrumPiece::count:
            break;
    }

    return piece;
}

} // namespace

DrumKit::LoadResult DrumKit::load (const juce::File& folder, double sampleRate)
{
    const auto xml = juce::parseXML (folder.getChildFile ("kit.xml"));

    if (xml == nullptr || ! xml->hasTagName ("DRUMKIT"))
        return { nullptr, "The drum kit in " + folder.getFullPathName() + " couldn't be read." };

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto kit = std::make_unique<DrumKit>();
    kit->sampleRate = sampleRate;

    for (auto* element : xml->getChildWithTagNameIterator ("SAMPLE"))
    {
        const auto pieceName = element->getStringAttribute ("piece");
        const auto file = folder.getChildFile (element->getStringAttribute ("file"));

        auto piece = DrumPiece::count;

        for (int i = 0; i < (int) DrumPiece::count; ++i)
            if (pieceName == getPieceName ((DrumPiece) i))
                piece = (DrumPiece) i;

        const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (piece == DrumPiece::count || reader == nullptr)
            return { nullptr, "The drum kit is missing " + file.getFileName() + "." };

        juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);

        const auto ratio = reader->sampleRate / sampleRate;

        Sample sample;
        sample.level = (float) element->getDoubleAttribute ("level", 1.0);
        sample.audio = juce::approximatelyEqual (ratio, 1.0) ? std::move (audio) : resample (audio, ratio);

        kit->pieces[(size_t) piece].push_back (std::move (sample));
    }

    for (auto& samples : kit->pieces)
        std::sort (samples.begin(), samples.end(), [] (const Sample& a, const Sample& b) { return a.level < b.level; });

    if (kit->pieces[(size_t) DrumPiece::kick].empty() || kit->pieces[(size_t) DrumPiece::snare].empty())
        return { nullptr, "The drum kit has no kick or snare." };

    return { std::move (kit), {} };
}

const DrumKit::Samples& DrumKit::samplesFor (DrumPiece piece) const noexcept
{
    while (pieces[(size_t) piece].empty() && getFallback (piece) != piece)
        piece = getFallback (piece);

    return pieces[(size_t) piece];
}

DrumKit::Choice DrumKit::pick (DrumPiece piece, float velocity, juce::uint32 seed) const noexcept
{
    const auto& samples = samplesFor (piece);

    if (samples.empty())
        return {};

    const auto count = (int) samples.size();
    const auto target = samples.back().level * std::pow (velocity, velocityCurve);

    const auto above = std::lower_bound (samples.begin(), samples.end(), target,
                                         [] (const Sample& sample, float level) { return sample.level < level; });

    // Starts from the closest, and takes the two around it to choose between.
    auto nearest = (int) std::distance (samples.begin(), above);

    if (nearest == count || (nearest > 0 && target / samples[(size_t) nearest - 1].level < samples[(size_t) nearest].level / target))
        --nearest;

    const auto spread = juce::jmin (numNearbySamples, count);
    const auto first = juce::jlimit (0, count - spread, nearest - spread / 2);
    const auto& chosen = samples[(size_t) (first + (int) (seed % (juce::uint32) spread))];

    return { &chosen.audio, juce::jlimit (minimumGain, maximumGain, target / chosen.level) };
}

} // namespace rifftrack
