#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** A needle meter. 0 VU is a sine peaking at -18 dBFS, which leaves plenty of headroom,
    so aiming for the needle around 0 sets a good recording level. The light stays on
    after a level near full scale, so a clip can't be missed. Click it to clear it.
*/
class VuMeter final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    /** Level is an average from the engine, and peak the highest sample since the last call. */
    void update (float level, float peak, bool showsInput);

    bool hasClipped() const noexcept { return clipped; }

    /** How far the needle swings for a level, from 0 to 1 at +3 VU. */
    static float needlePosition (float level) noexcept;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    float position = 0.0f;
    bool clipped = false, input = false;
};
