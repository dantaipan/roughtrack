#include "VuMeter.h"

#include "ConsoleLookAndFeel.h"

#include <array>
#include <cmath>
#include <numbers>

namespace
{

constexpr float zeroVuLevel = 0.0890f; // A sine peaking at -18 dBFS, as its RMS level.
constexpr float topOfScaleVu = 3.0f;
constexpr float clipLevel = 0.98f;
constexpr float smallestNeedleMove = 0.002f;
constexpr float swingRadians = 0.85f;
constexpr float scaleEdge = 0.82f;
constexpr std::array<float, 11> scaleMarks { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f };

juce::Colour faceColour()
{
    return juce::Colour (0xffe8d9a8);
}

float positionForVu (float vu) noexcept
{
    return std::pow (10.0f, (vu - topOfScaleVu) / 20.0f);
}

juce::Point<float> pointOnSwing (juce::Point<float> pivot, float radius, float position) noexcept
{
    return pivot.getPointOnCircumference (radius, swingRadians * (2.0f * position - 1.0f));
}

} // namespace

// A real needle moves with voltage, not decibels, which is why 0 VU sits so far across.
float VuMeter::needlePosition (float level) noexcept
{
    return juce::jmin (1.05f, level / zeroVuLevel * positionForVu (0.0f));
}

void VuMeter::update (float level, float peak, bool showsInput)
{
    const auto newPosition = needlePosition (level);
    const auto nowClipped = clipped || peak >= clipLevel;

    if (std::abs (newPosition - position) < smallestNeedleMove && nowClipped == clipped && showsInput == input)
        return;

    position = newPosition;
    clipped = nowClipped;
    input = showsInput;
    repaint();
}

void VuMeter::mouseDown (const juce::MouseEvent&)
{
    clipped = false;
    repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (bounds, 3.0f);

    const auto face = bounds.reduced (1.5f);

    g.setColour (faceColour());
    g.fillRoundedRectangle (face, 2.0f);

    const juce::Point<float> pivot { face.getCentreX(), face.getBottom() + face.getHeight() * 0.25f };
    const auto radius = face.getHeight() * 1.05f;

    g.saveState();
    g.reduceClipRegion (face.toNearestInt());

    juce::Path redArc;
    redArc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, swingRadians * (2.0f * positionForVu (0.0f) - 1.0f), swingRadians, true);

    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.strokePath ([&]
                  {
                      juce::Path arc;
                      arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, -swingRadians, swingRadians * (2.0f * positionForVu (0.0f) - 1.0f), true);
                      return arc;
                  }(),
                  juce::PathStrokeType (1.0f));

    g.setColour (console::red);
    g.strokePath (redArc, juce::PathStrokeType (2.5f));

    for (const auto mark : scaleMarks)
    {
        const auto markPosition = positionForVu (mark);

        g.setColour (mark > 0.0f ? console::red : juce::Colours::black.withAlpha (0.75f));
        g.drawLine ({ pointOnSwing (pivot, radius * scaleEdge * 1.14f, markPosition), pointOnSwing (pivot, radius, markPosition) }, 1.0f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.setFont (juce::FontOptions (9.0f));
    g.drawText (input ? "INPUT" : "VU", face.withTrimmedTop (face.getHeight() * 0.55f), juce::Justification::centred);

    g.setColour (juce::Colours::black);
    g.drawLine ({ pivot, pointOnSwing (pivot, radius * 1.02f, position) }, 1.4f);

    g.restoreState();

    const auto light = juce::Rectangle<float> (7.0f, 7.0f).withPosition (face.getRight() - 9.0f, face.getY() + 2.0f);

    g.setColour (clipped ? console::red : juce::Colours::black.withAlpha (0.25f));
    g.fillEllipse (light);
}
