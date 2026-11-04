#pragma once

#include <juce_core/juce_core.h>

/** Mixes a multi-microphone DrumGizmo kit down to the stereo kit RoughTrack ships. */
int runBuildDrumKit (const juce::File& kitFolder, const juce::File& outputFolder);
