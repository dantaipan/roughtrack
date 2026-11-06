#include "GroovePreview.h"

#include "ConsoleLookAndFeel.h"

#include <algorithm>
#include <array>

namespace
{

constexpr int timerHz = 30;
constexpr int indicatorSize = 12;
constexpr int indicatorSpace = 26;

double now()
{
    return juce::Time::getMillisecondCounterHiRes();
}

} // namespace

HoverDelay::Change HoverDelay::update (int item, double nowMs)
{
    if (item != hovered)
    {
        const auto wasPlaying = playing >= 0;

        hovered = item;
        hoveredSince = nowMs;
        playing = -1;

        return wasPlaying ? Change::stop : Change::none;
    }

    if (hovered >= 0 && playing < 0 && nowMs - hoveredSince >= delayMs)
    {
        playing = hovered;
        return Change::start;
    }

    return Change::none;
}

float HoverDelay::getProgress (double nowMs) const noexcept
{
    if (hovered < 0)
        return 0.0f;

    return (float) juce::jlimit (0.0, 1.0, (nowMs - hoveredSince) / delayMs);
}

GroovePreview::GroovePreview (roughtrack::AudioEngine& engineToUse) : engine (engineToUse)
{
}

GroovePreview::~GroovePreview()
{
    end();
}

void GroovePreview::begin (int trackToPreview)
{
    end();
    track = trackToPreview;
    startTimerHz (timerHz);
}

void GroovePreview::end()
{
    stopTimer();

    if (delay.getPlaying() >= 0)
        engine.stopGroovePreview();

    delay = {};
    track = -1;
    lastHovered = lastPlaying = -1;
}

float GroovePreview::getProgress (int groove) const noexcept
{
    return delay.getHovered() == groove ? delay.getProgress (now()) : 0.0f;
}

bool GroovePreview::isPlaying (int groove) const noexcept
{
    return delay.getPlaying() == groove;
}

void GroovePreview::timerCallback()
{
    // JUCE doesn't tell a custom item when it's highlighted, so look.
    auto hovered = -1;

    for (auto* item : items)
        if (item->isItemHighlighted() && item->isShowing())
            hovered = item->getGrooveIndex();

    const auto& grooves = engine.getGrooves().getGrooves();

    switch (delay.update (hovered, now()))
    {
        case HoverDelay::Change::start:
            if (hovered < (int) grooves.size())
                engine.startGroovePreview (track, grooves[(size_t) hovered].id);
            break;

        case HoverDelay::Change::stop:
            engine.stopGroovePreview();
            break;

        case HoverDelay::Change::none:
            break;
    }

    repaintItems();
}

void GroovePreview::repaintItems()
{
    // The items just left are repainted too, or they'd keep showing a ring or bars.
    const std::array<int, 4> changed { delay.getHovered(), delay.getPlaying(), lastHovered, lastPlaying };

    for (auto* item : items)
        if (std::find (changed.begin(), changed.end(), item->getGrooveIndex()) != changed.end())
            item->repaint();

    lastHovered = delay.getHovered();
    lastPlaying = delay.getPlaying();
}

GrooveMenuItem::GrooveMenuItem (std::shared_ptr<GroovePreview> previewToUse, int index, const juce::String& grooveName)
    : preview (std::move (previewToUse)), grooveIndex (index), name (grooveName)
{
    preview->items.insert (this);
}

GrooveMenuItem::~GrooveMenuItem()
{
    preview->items.erase (this);
}

void GrooveMenuItem::getIdealSize (int& idealWidth, int& idealHeight)
{
    getLookAndFeel().getIdealPopupMenuItemSize (name, false, -1, idealWidth, idealHeight);
    idealWidth += indicatorSpace;
}

void GrooveMenuItem::paint (juce::Graphics& g)
{
    const auto highlighted = isItemHighlighted();

    // Drawn whole, so the highlight runs under the indicator too; the name is short of it.
    getLookAndFeel().drawPopupMenuItem (g, getLocalBounds(), false, true, highlighted, false, false, name, {}, nullptr, nullptr);

    const auto indicator = getLocalBounds().removeFromRight (indicatorSpace).withSizeKeepingCentre (indicatorSize, indicatorSize).toFloat();
    const auto colour = findColour (highlighted ? juce::PopupMenu::highlightedTextColourId : juce::PopupMenu::textColourId);

    if (preview->isPlaying (grooveIndex))
    {
        // Three bars bobbing, like a level meter, while it plays.
        const auto time = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        const auto barWidth = indicator.getWidth() / 5.0f;

        // On the highlight, which is amber itself, they take the text's colour instead.
        g.setColour (highlighted ? colour : console::amber);

        for (int bar = 0; bar < 3; ++bar)
        {
            const auto height = indicator.getHeight() * (0.35f + 0.65f * (float) (0.5 + 0.5 * std::sin (time * 9.0 + bar * 2.1)));
            g.fillRect (indicator.getX() + (float) bar * 2.0f * barWidth, indicator.getBottom() - height, barWidth, height);
        }

        return;
    }

    if (const auto progress = preview->getProgress (grooveIndex); progress > 0.0f)
    {
        // A ring filling up to the moment it plays.
        g.setColour (colour.withAlpha (0.3f));
        g.drawEllipse (indicator.reduced (1.0f), 1.5f);

        juce::Path arc;
        arc.addCentredArc (indicator.getCentreX(), indicator.getCentreY(), indicator.getWidth() / 2.0f - 1.0f, indicator.getHeight() / 2.0f - 1.0f,
                           0.0f, 0.0f, juce::MathConstants<float>::twoPi * progress, true);
        g.setColour (colour);
        g.strokePath (arc, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
