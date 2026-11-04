#pragma once

#include "roughtrack/ChannelStrip.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** A column of controls generated from a parameter table, read and written
    through two functions so channels and aux effects can share it.
*/
class ParameterPanel final : public juce::Component
{
public:
    using Getter = std::function<float (int)>;
    using Setter = std::function<void (int, float)>;

    ParameterPanel (int numParameters, std::function<const roughtrack::ParameterInfo& (int)> infoFor, Getter, Setter);

    void refresh();
    void resized() override;

    int getIdealHeight() const;

private:
    struct Control
    {
        juce::Label label;
        juce::Slider slider;
        juce::ToggleButton toggle;
        bool isSwitch = false;
    };

    Getter getValue;
    Setter setValue;
    std::vector<std::unique_ptr<Control>> controls;
};
