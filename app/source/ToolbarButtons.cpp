#include "ToolbarButtons.h"

#include "ConsoleLookAndFeel.h"

namespace
{
constexpr float iconSize = 12.0f;
constexpr float iconGap = 6.0f;
constexpr size_t maxMessages = 50;
constexpr int clearMenuId = 1;
} // namespace

TransportButton::TransportButton (const juce::String& name, Icon iconToShow) : juce::TextButton (name), icon (iconToShow)
{
}

void TransportButton::setIconOnly (bool shouldShowIconOnly)
{
    if (iconOnly == shouldShowIconOnly)
        return;

    iconOnly = shouldShowIconOnly;
    setTooltip (iconOnly ? getButtonText() : juce::String());
    repaint();
}

juce::Path TransportButton::createIcon (juce::Rectangle<float> area) const
{
    juce::Path path;

    switch (icon)
    {
        case Icon::returnToStart:
            path.addRectangle (area.removeFromLeft (2.0f));
            path.addTriangle (area.getRight(), area.getY(), area.getRight(), area.getBottom(), area.getX() + 1.0f, area.getCentreY());
            break;

        case Icon::play:
            path.addTriangle (area.getX() + 1.0f, area.getY(), area.getX() + 1.0f, area.getBottom(), area.getRight(), area.getCentreY());
            break;

        case Icon::stop:
            path.addRectangle (area.reduced (1.0f));
            break;

        case Icon::record:
            path.addEllipse (area);
            break;
    }

    return path;
}

void TransportButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    auto& lookAndFeel = getLookAndFeel();
    const auto background = findColour (getToggleState() ? buttonOnColourId : buttonColourId);
    lookAndFeel.drawButtonBackground (g, *this, background, isHighlighted, isDown);

    const auto text = findColour (textColourOffId);
    const auto font = lookAndFeel.getTextButtonFont (*this, getHeight());
    const auto textWidth = iconOnly ? 0.0f : juce::GlyphArrangement::getStringWidth (font, getButtonText());
    const auto contentWidth = iconSize + (iconOnly ? 0.0f : iconGap + textWidth);

    auto area = getLocalBounds().toFloat();
    const auto left = area.getCentreX() - contentWidth / 2.0f;
    const juce::Rectangle<float> iconArea (left, area.getCentreY() - iconSize / 2.0f, iconSize, iconSize);

    // The record dot stays red, except on the red of a take in progress.
    const auto isRecordOnRed = icon == Icon::record && background == console::red;
    g.setColour (icon == Icon::record && ! isRecordOnRed ? console::red : text);
    g.fillPath (createIcon (iconArea));

    if (iconOnly)
        return;

    g.setColour (text);
    g.setFont (font);
    g.drawText (getButtonText(), juce::Rectangle<float> (iconArea.getRight() + iconGap, area.getY(), textWidth + 2.0f, area.getHeight()),
                juce::Justification::centredLeft, false);
}

GhostTrack::GhostTrack()
{
    setTooltip ("Add a track");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void GhostTrack::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (3.0f, 1.0f);
    const auto colour = isMouseOver() ? console::cream : console::dimText.withAlpha (0.6f);

    juce::Path outline;
    outline.addRoundedRectangle (area, 3.0f);

    juce::Path dashed;
    const float dashes[] { 5.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);

    g.setColour (colour);
    g.fillPath (dashed);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText ("+ Track", area.withSizeKeepingCentre (area.getWidth(), 24.0f), juce::Justification::centred);
}

void GhostTrack::mouseUp (const juce::MouseEvent& event)
{
    if (contains (event.getPosition()) && onClick != nullptr)
        onClick (event.getScreenPosition());
}

MessageButton::MessageButton()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    updateTooltip();
}

void MessageButton::add (const juce::String& text, bool isWarning)
{
    messages.push_front ({ text, juce::Time::getCurrentTime(), isWarning });

    if (messages.size() > maxMessages)
        messages.pop_back();

    ++numUnseen;
    hasUnseenWarning = hasUnseenWarning || isWarning;
    updateTooltip();
    repaint();
}

void MessageButton::updateTooltip()
{
    setTooltip (messages.empty() ? juce::String ("No messages") : juce::String ("Messages"));
}

void MessageButton::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (3.0f);
    const auto colour = hasUnseenWarning ? console::amber : (numUnseen > 0 ? console::cream : console::dimText.withAlpha (0.7f));

    // A speech bubble with a tail at the bottom left.
    auto bubble = area.withTrimmedBottom (4.0f).withTrimmedRight (4.0f);
    juce::Path path;
    path.addRoundedRectangle (bubble, 3.0f);
    path.startNewSubPath (bubble.getX() + 4.0f, bubble.getBottom() - 0.5f);
    path.lineTo (bubble.getX() + 3.0f, area.getBottom());
    path.lineTo (bubble.getX() + 9.0f, bubble.getBottom() - 0.5f);

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (1.5f));

    for (int line = 0; line < 2; ++line)
        g.fillRect (bubble.getX() + 4.0f, bubble.getY() + 5.0f + (float) line * 4.0f, bubble.getWidth() - 8.0f - (float) line * 4.0f, 1.5f);

    if (numUnseen == 0)
        return;

    const auto badge = juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ area.getRight() - 4.0f, area.getY() + 4.0f });
    g.setColour (colour);
    g.fillEllipse (badge);
    g.setColour (console::panel);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (numUnseen > 9 ? juce::String ("9+") : juce::String (numUnseen), badge, juce::Justification::centred);
}

void MessageButton::mouseUp (const juce::MouseEvent& event)
{
    if (! contains (event.getPosition()))
        return;

    juce::PopupMenu menu;

    if (messages.empty())
        menu.addItem (-1, "No messages", false);

    for (const auto& message : messages)
    {
        juce::PopupMenu::Item item (message.time.formatted ("%H:%M  ") + message.text);
        item.setEnabled (false);

        if (message.isWarning)
            item.setColour (console::amber);

        menu.addItem (item);
    }

    if (! messages.empty())
    {
        menu.addSeparator();
        menu.addItem (clearMenuId, "Clear");
    }

    numUnseen = 0;
    hasUnseenWarning = false;
    repaint();

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safeThis = juce::Component::SafePointer (this)] (int result)
                        {
                            if (safeThis != nullptr && result == clearMenuId)
                            {
                                safeThis->messages.clear();
                                safeThis->updateTooltip();
                                safeThis->repaint();
                            }
                        });
}
