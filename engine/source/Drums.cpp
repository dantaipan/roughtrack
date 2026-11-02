#include "rifftrack/Drums.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace rifftrack
{

namespace
{

constexpr float maximumMidiVelocity = 127.0f;
constexpr double nearTempoRatio = 0.05;

constexpr std::array<const char*, (size_t) DrumPiece::count> pieceNames { "kick", "snare", "sideStick", "hiHatClosed", "hiHatOpen",
                                                                          "hiHatPedal", "tomHigh", "tomMid", "tomLow", "crash",
                                                                          "crash2", "ride", "rideBell" };

std::vector<DrumHit> parseHits (const juce::String& text)
{
    std::vector<DrumHit> hits;

    for (const auto& entry : juce::StringArray::fromTokens (text, ";", {}))
    {
        const auto fields = juce::StringArray::fromTokens (entry, ",", {});

        if (fields.size() != 3 || ! juce::isPositiveAndBelow (fields[1].getIntValue(), (int) DrumPiece::count))
            continue;

        hits.push_back ({ fields[0].getDoubleValue(), (DrumPiece) fields[1].getIntValue(), (float) fields[2].getIntValue() / maximumMidiVelocity });
    }

    std::stable_sort (hits.begin(), hits.end(), [] (const DrumHit& a, const DrumHit& b) { return a.beat < b.beat; });
    return hits;
}

std::vector<int> findBarStarts (const std::vector<DrumHit>& hits, int bars)
{
    std::vector<int> starts;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto firstBeat = bar * beatsPerBar - grooveEarlyBeats;
        const auto first = std::find_if (hits.begin(), hits.end(), [firstBeat] (const DrumHit& hit) { return hit.beat >= firstBeat; });
        starts.push_back ((int) std::distance (hits.begin(), first));
    }

    starts.push_back ((int) hits.size());
    return starts;
}

} // namespace

const char* getPieceName (DrumPiece piece) noexcept
{
    return pieceNames[(size_t) piece];
}

bool isNearTempo (const Groove& groove, double beatsPerMinute) noexcept
{
    return std::abs (groove.recordedBpm - beatsPerMinute) <= beatsPerMinute * nearTempoRatio;
}

bool GrooveLibrary::load (const juce::File& file)
{
    const auto xml = juce::parseXML (file);

    if (xml == nullptr || ! xml->hasTagName ("GROOVES"))
        return false;

    grooves.clear();

    for (auto* element : xml->getChildWithTagNameIterator ("GROOVE"))
    {
        Groove groove;
        groove.id = element->getStringAttribute ("id");
        groove.style = element->getStringAttribute ("style");
        groove.name = element->getStringAttribute ("name");
        groove.isFill = element->getStringAttribute ("kind") == "fill";
        groove.bars = element->getIntAttribute ("bars");
        groove.recordedBpm = element->getIntAttribute ("bpm");
        groove.hits = parseHits (element->getStringAttribute ("hits"));
        groove.barStarts = findBarStarts (groove.hits, groove.bars);

        if (groove.bars > 0)
            grooves.push_back (std::move (groove));
    }

    return true;
}

const Groove* GrooveLibrary::find (const juce::String& id) const noexcept
{
    for (const auto& groove : grooves)
        if (groove.id == id)
            return &groove;

    return nullptr;
}

int findPartAt (const DrumPartList& parts, int bar) noexcept
{
    for (auto i = (int) parts.size() - 1; i >= 0; --i)
        if (bar >= parts[(size_t) i].startBar && bar < parts[(size_t) i].endBar())
            return i;

    return -1;
}

DrumPartList movePart (DrumPartList parts, int index, int newStartBar)
{
    parts[(size_t) index].startBar = juce::jmax (0, newStartBar);
    return parts;
}

DrumPartList trimPartStart (DrumPartList parts, int index, int newStartBar)
{
    auto& part = parts[(size_t) index];
    const auto start = juce::jlimit (0, part.endBar() - 1, newStartBar);

    part.offsetBars += start - part.startBar;
    part.bars -= start - part.startBar;
    part.startBar = start;
    return parts;
}

DrumPartList trimPartEnd (DrumPartList parts, int index, int newEndBar)
{
    auto& part = parts[(size_t) index];

    part.bars = juce::jmax (1, newEndBar - part.startBar);
    return parts;
}

DrumPartList splitPart (DrumPartList parts, int index, int bar)
{
    const auto part = parts[(size_t) index];

    if (bar <= part.startBar || bar >= part.endBar())
        return parts;

    auto second = part;
    second.startBar = bar;
    second.bars = part.endBar() - bar;
    second.offsetBars = part.offsetBars + bar - part.startBar;

    parts[(size_t) index].bars = bar - part.startBar;
    parts.insert (parts.begin() + index + 1, second);
    return parts;
}

DrumPartList removePart (DrumPartList parts, int index)
{
    parts.erase (parts.begin() + index);
    return parts;
}

} // namespace rifftrack
