#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>

/** A transport button with the usual symbol beside its name, or the symbol
    alone when the window is too narrow for both.
*/
class TransportButton final : public juce::TextButton
{
public:
    enum class Icon
    {
        returnToStart,
        play,
        stop,
        record
    };

    TransportButton (const juce::String& name, Icon);

    void setIconOnly (bool shouldShowIconOnly);

    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

private:
    juce::Path createIcon (juce::Rectangle<float> area) const;

    Icon icon;
    bool iconOnly = false;
};

/** A dashed space after the last track, as a row or a console channel, for adding another. */
class GhostTrack final : public juce::Component, public juce::SettableTooltipClient
{
public:
    GhostTrack();

    /** Given where the click was on screen, so a menu can open right there. */
    std::function<void (juce::Point<int> screenPosition)> onClick;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
};

/** Collects the app's messages behind one icon. It shows how many are unseen, turns
    amber while one of them is a warning, and lists them all when clicked.
*/
class MessageButton final : public juce::Component, public juce::SettableTooltipClient
{
public:
    MessageButton();

    void add (const juce::String& text, bool isWarning);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Message
    {
        juce::String text;
        juce::Time time;
        bool isWarning = false;
    };

    void updateTooltip();

    std::deque<Message> messages;
    int numUnseen = 0;
    bool hasUnseenWarning = false;
};
