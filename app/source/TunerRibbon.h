#pragma once

#include "roughtrack/AudioEngine.h"
#include "roughtrack/Tuner.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** A strobe tuner across the top of the window. The bands drift right when a string
    is sharp and left when it's flat, and stand still when it's in tune.
*/
class TunerRibbon final : public juce::Component
{
public:
    explicit TunerRibbon (roughtrack::AudioEngine&);

    /** Says where the sound is coming from, and clears what was heard before. */
    void start (double sampleRate);

    /** Shows which track and input are being read. */
    void setSource (const juce::String& description);

    /** Call this regularly while the tuner is showing. */
    void update (double secondsSinceLast);

    /** What's on the display, or an empty name when there's no note. */
    juce::String getDisplayedNote() const;
    double getDisplayedCents() const noexcept { return shownCents; }

    void paint (juce::Graphics&) override;

private:
    void listen();
    void show (const roughtrack::TunerReading&);
    void paintBand (juce::Graphics&, juce::Rectangle<float>, float barWidth, float offset, juce::Colour) const;

    roughtrack::AudioEngine& engine;
    std::unique_ptr<roughtrack::PitchDetector> detector;

    std::vector<float> history, arriving;
    int samplesSinceAnalysis = 0;

    std::vector<double> recentCents;
    int shownNote = -1;
    double shownCents = 0.0;
    double secondsSinceNote = 0.0;
    double strobePhase = 0.0;
    juce::String source;
};
