#include "ConsoleLookAndFeel.h"

#include <cmath>

namespace
{

constexpr float capLength = 34.0f;
constexpr float capWidth = 26.0f;
constexpr float slotWidth = 4.0f;
constexpr float smallCapLength = 14.0f;
constexpr float smallCapWidth = 18.0f;
constexpr float cornerSize = 3.0f;

void drawCap (juce::Graphics& g, juce::Rectangle<float> cap, bool isVertical)
{
    const auto dark = juce::Colour (0xff202225);
    const auto light = juce::Colour (0xff6a6e75);

    g.setGradientFill (isVertical ? juce::ColourGradient::vertical (light, cap.getY(), dark, cap.getBottom())
                                  : juce::ColourGradient::horizontal (light, cap.getX(), dark, cap.getRight()));
    g.fillRoundedRectangle (cap, 2.0f);

    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (cap, 2.0f, 1.0f);

    g.setColour (console::cream);

    if (isVertical)
        g.fillRect (cap.withHeight (2.0f).withCentre (cap.getCentre()).reduced (1.0f, 0.0f));
    else
        g.fillRect (cap.withWidth (2.0f).withCentre (cap.getCentre()).reduced (0.0f, 1.0f));
}

} // namespace

ConsoleLookAndFeel::ConsoleLookAndFeel()
{
    setColourScheme ({ console::panel, console::widget, console::strip, console::outline, console::cream,
                       console::amber, juce::Colours::black, console::amber, console::cream });

    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::TextButton::textColourOffId, console::cream);
    setColour (juce::Label::textColourId, console::cream);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId, console::cream);
    setColour (juce::ScrollBar::thumbColourId, console::outline);
    setColour (juce::TooltipWindow::backgroundColourId, console::recess);
    setColour (juce::TooltipWindow::textColourId, console::cream);
}

void ConsoleLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                                           float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto isVertical = style == juce::Slider::LinearVertical;

    g.setColour (console::recess);

    if (isVertical)
    {
        g.fillRoundedRectangle (area.withWidth (slotWidth).withCentre (area.getCentre()), 2.0f);
        drawCap (g, juce::Rectangle<float> (juce::jmin (capWidth, area.getWidth()), capLength).withCentre ({ area.getCentreX(), sliderPos }), true);
    }
    else
    {
        g.fillRoundedRectangle (area.withHeight (slotWidth).withCentre (area.getCentre()), 2.0f);
        drawCap (g, juce::Rectangle<float> (smallCapLength, juce::jmin (smallCapWidth, area.getHeight())).withCentre ({ sliderPos, area.getCentreY() }), false);
    }
}

void ConsoleLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPosProportional,
                                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto knob = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (bounds.getCentre());
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff5a5e65), knob.getY(), juce::Colour (0xff1e2023), knob.getBottom()));
    g.fillEllipse (knob);

    g.setColour (juce::Colours::black);
    g.drawEllipse (knob, 1.0f);

    const auto centre = knob.getCentre();
    const auto tip = centre.getPointOnCircumference (radius - 2.0f, angle);
    const auto start = centre.getPointOnCircumference (radius * 0.4f, angle);

    g.setColour (console::cream);
    g.drawLine ({ start, tip }, 2.0f);
}

void ConsoleLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                               bool isHighlighted, bool isDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;

    if (isDown)
        colour = colour.darker (0.2f);
    else if (isHighlighted)
        colour = colour.brighter (0.1f);

    g.setGradientFill (juce::ColourGradient::vertical (colour.brighter (0.12f), bounds.getY(), colour.darker (0.12f), bounds.getBottom()));
    g.fillRoundedRectangle (bounds, cornerSize);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds, cornerSize, 1.0f);
}

int ConsoleLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    if (slider.getSliderStyle() == juce::Slider::LinearVertical)
        return (int) (capLength / 2.0f);

    return (int) (smallCapLength / 2.0f);
}
