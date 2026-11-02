#include "TunerRibbon.h"

#include "ConsoleLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace
{

constexpr int samplesBetweenAnalyses = 2048;
constexpr size_t readingsSmoothed = 5;
constexpr double secondsNoteStays = 1.5;
constexpr double inTuneCents = 1.0;
constexpr double centsToBarsPerSecond = 0.25;
constexpr float barWidthPixels = 28.0f;
constexpr int noteWidth = 96;
constexpr int centsWidth = 96;
constexpr int sourceWidth = 190;

} // namespace

TunerRibbon::TunerRibbon (rifftrack::AudioEngine& engineToRead) : engine (engineToRead) {}

void TunerRibbon::start (double sampleRate)
{
    detector = std::make_unique<rifftrack::PitchDetector> (sampleRate);
    history.assign ((size_t) detector->getWindowSize(), 0.0f);
    arriving.resize (history.size());
    samplesSinceAnalysis = 0;
    recentCents.clear();
    shownNote = -1;
    shownCents = 0.0;

    while (engine.readTuner (arriving.data(), (int) arriving.size()) > 0) {}
}

void TunerRibbon::setSource (const juce::String& description)
{
    if (description != source)
    {
        source = description;
        repaint();
    }
}

void TunerRibbon::update (double secondsSinceLast)
{
    if (detector == nullptr)
        return;

    secondsSinceNote += secondsSinceLast;
    listen();

    if (shownNote >= 0 && secondsSinceNote > secondsNoteStays)
    {
        shownNote = -1;
        recentCents.clear();
    }

    if (shownNote >= 0)
        strobePhase += shownCents * centsToBarsPerSecond * secondsSinceLast;

    repaint();
}

void TunerRibbon::listen()
{
    for (;;)
    {
        const auto numRead = engine.readTuner (arriving.data(), (int) arriving.size());

        if (numRead == 0)
            return;

        std::move (history.begin() + numRead, history.end(), history.begin());
        std::copy_n (arriving.begin(), numRead, history.end() - numRead);
        samplesSinceAnalysis += numRead;

        if (samplesSinceAnalysis >= samplesBetweenAnalyses)
        {
            samplesSinceAnalysis = 0;

            const auto reading = detector->analyse (history.data());

            if (reading.hasPitch)
                show (reading);
        }
    }
}

// A plucked string starts sharp and wobbles, so a few readings are taken together.
void TunerRibbon::show (const rifftrack::TunerReading& reading)
{
    if (reading.note != shownNote)
        recentCents.clear();

    shownNote = reading.note;
    secondsSinceNote = 0.0;
    recentCents.push_back (reading.cents);

    if (recentCents.size() > readingsSmoothed)
        recentCents.erase (recentCents.begin());

    auto sorted = recentCents;
    std::sort (sorted.begin(), sorted.end());
    shownCents = sorted[sorted.size() / 2];
}

juce::String TunerRibbon::getDisplayedNote() const
{
    return shownNote >= 0 ? rifftrack::getTunerNoteName (shownNote) : juce::String();
}

void TunerRibbon::paintBand (juce::Graphics& g, juce::Rectangle<float> area, float barWidth, float offset, juce::Colour colour) const
{
    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    g.setColour (colour);

    const auto start = std::fmod (offset, 2.0f * barWidth) - 2.0f * barWidth;

    for (auto x = area.getX() + start; x < area.getRight(); x += 2.0f * barWidth)
        g.fillRect (juce::Rectangle<float> (x, area.getY(), barWidth, area.getHeight()));

    g.restoreState();
}

void TunerRibbon::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (0, 4);

    g.setColour (console::recess);
    g.fillRoundedRectangle (area.toFloat(), 3.0f);

    area.reduce (10, 4);

    const auto hasNote = shownNote >= 0;
    const auto inTune = hasNote && std::abs (shownCents) < inTuneCents;

    auto noteArea = area.removeFromLeft (noteWidth);
    auto centsArea = area.removeFromLeft (centsWidth);
    auto sourceArea = area.removeFromRight (sourceWidth);

    g.setColour (inTune ? console::green : (hasNote ? console::cream : console::dimText));
    g.setFont (juce::FontOptions ((float) noteArea.getHeight() * 0.85f, juce::Font::bold));
    g.drawText (hasNote ? getDisplayedNote() : juce::String::fromUTF8 ("\xe2\x80\x93"), noteArea, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (15.0f));
    g.setColour (inTune ? console::green : console::cream);

    if (hasNote)
        g.drawText (inTune ? "In tune" : (shownCents > 0.0 ? "+" : "") + juce::String (shownCents, 1) + " cents",
                    centsArea, juce::Justification::centredLeft);

    g.setColour (console::dimText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (source, sourceArea.withTrimmedLeft (10), juce::Justification::centredRight);

    const auto strobe = area.toFloat();
    const auto colour = hasNote ? (inTune ? console::green : console::amber) : console::widget;

    g.setColour (juce::Colours::black);
    g.fillRect (strobe);

    const auto offset = (float) (strobePhase * 2.0 * barWidthPixels);

    paintBand (g, strobe.withHeight (strobe.getHeight() / 2.0f).reduced (0.0f, 2.0f), barWidthPixels, offset, colour);
    paintBand (g, strobe.withTrimmedTop (strobe.getHeight() / 2.0f).reduced (0.0f, 2.0f), barWidthPixels / 2.0f, offset * 2.0f, colour.withAlpha (0.7f));
}
