#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace console
{

const auto panel = juce::Colour (0xff24262a);
const auto strip = juce::Colour (0xff2e3136);
const auto recess = juce::Colour (0xff15171a);
const auto widget = juce::Colour (0xff3b3f46);
const auto outline = juce::Colour (0xff4a4e55);
const auto cream = juce::Colour (0xffe9e2d0);
const auto dimText = juce::Colour (0xff9a9689);
const auto amber = juce::Colour (0xffe8a33d);
const auto red = juce::Colour (0xffd8432f);
const auto green = juce::Colour (0xff6fbf73);
const auto blue = juce::Colour (0xff5b8fbf);

} // namespace console

/** The look of the hardware: dark panels, cream lettering and faders with real caps. */
class ConsoleLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ConsoleLookAndFeel();

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool isHighlighted, bool isDown) override;

    int getSliderThumbRadius (juce::Slider&) override;
};
