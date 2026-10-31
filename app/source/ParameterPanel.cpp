#include "ParameterPanel.h"

namespace
{
constexpr int rowHeight = 28;
constexpr int labelWidth = 110;
} // namespace

ParameterPanel::ParameterPanel (int numParameters,
                                std::function<const rifftrack::ParameterInfo& (int)> infoFor,
                                Getter getter,
                                Setter setter)
    : getValue (std::move (getter)), setValue (std::move (setter))
{
    for (int i = 0; i < numParameters; ++i)
    {
        const auto& info = infoFor (i);

        auto control = std::make_unique<Control>();
        control->isSwitch = info.isSwitch;

        if (info.isSwitch)
        {
            control->toggle.setButtonText (info.label);
            control->toggle.onClick = [this, i, button = &control->toggle] { setValue (i, button->getToggleState() ? 1.0f : 0.0f); };
            addAndMakeVisible (control->toggle);
        }
        else
        {
            control->label.setText (info.label, juce::dontSendNotification);
            addAndMakeVisible (control->label);

            auto& slider = control->slider;
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 20);
            slider.setRange (info.minimum, info.maximum, (info.maximum - info.minimum) > 10.0f ? 0.1 : 0.01);
            slider.setTextValueSuffix (info.suffix);
            slider.setDoubleClickReturnValue (true, info.defaultValue);
            slider.onValueChange = [this, i, sliderToRead = &slider] { setValue (i, (float) sliderToRead->getValue()); };
            addAndMakeVisible (slider);
        }

        controls.push_back (std::move (control));
    }

    refresh();
}

void ParameterPanel::refresh()
{
    for (size_t i = 0; i < controls.size(); ++i)
    {
        const auto value = getValue ((int) i);

        if (controls[i]->isSwitch)
            controls[i]->toggle.setToggleState (value > 0.5f, juce::dontSendNotification);
        else
            controls[i]->slider.setValue (value, juce::dontSendNotification);
    }
}

void ParameterPanel::resized()
{
    auto area = getLocalBounds();

    for (auto& control : controls)
    {
        auto row = area.removeFromTop (rowHeight);

        if (control->isSwitch)
        {
            control->toggle.setBounds (row);
        }
        else
        {
            control->label.setBounds (row.removeFromLeft (labelWidth));
            control->slider.setBounds (row);
        }
    }
}

int ParameterPanel::getIdealHeight() const
{
    return (int) controls.size() * rowHeight;
}
