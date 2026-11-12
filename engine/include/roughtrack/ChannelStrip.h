#pragma once

#include "roughtrack/AmpModel.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

namespace roughtrack
{

enum class ChannelParameter
{
    ampEnabled,
    ampInput,
    eqEnabled,
    highPass,
    lowGain,
    midGain,
    midFrequency,
    highGain,
    compressorEnabled,
    compressorThreshold,
    compressorRatio,
    compressorMakeup,
    sendReverb,
    sendDelay,
    count
};

struct ParameterInfo
{
    const char* id;
    const char* label;
    float minimum, maximum, defaultValue;
    const char* suffix;
    bool isSwitch = false;
};

const ParameterInfo& getParameterInfo (ChannelParameter);

/** The built-in processing on one track. Each plugin insert runs either before
    the amp, like a pedal, or between the amp and the EQ and compressor.
    Nothing here adds latency, so none of it affects delay compensation.

    Parameters can change from any thread at any time. prepare() and the amp and
    cabinet calls belong to the message thread.
*/
class ChannelStrip
{
public:
    ChannelStrip();

    void prepare (double sampleRate, int maximumBlockSize);

    void setParameter (ChannelParameter, float value) noexcept;
    float getParameter (ChannelParameter) const noexcept;
    void resetParameters() noexcept;

    /** Returns what was there, to delete outside any lock. */
    std::unique_ptr<AmpModel> exchangeAmp (std::unique_ptr<AmpModel>);
    bool hasAmp() const noexcept { return amp != nullptr; }

    juce::File getAmpFile() const { return ampFile; }
    void setAmpFile (const juce::File& file) { ampFile = file; }

    juce::Result loadCabinet (const juce::File&);
    void clearCabinet();
    juce::File getCabinetFile() const { return cabinetFile; }

    void processAmp (float* left, float* right, int numSamples) noexcept;
    void processChannel (float* left, float* right, int numSamples) noexcept;

private:
    struct Biquad
    {
        void setCoefficients (const std::array<float, 6>&) noexcept;
        void process (float* samples, int numSamples, int channel) noexcept;
        void reset() noexcept;

        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        std::array<std::array<float, 2>, 2> state {};
    };

    void updateFilters() noexcept;

    std::array<std::atomic<float>, (size_t) ChannelParameter::count> parameters;

    std::unique_ptr<AmpModel> amp;
    juce::File ampFile, cabinetFile;

    std::atomic<bool> cabinetLoaded { false };
    juce::dsp::Convolution cabinet;

    juce::dsp::Compressor<float> compressor;
    Biquad highPassFilter, lowShelf, midPeak, highShelf;

    std::array<float, 5> filterValues {};
    bool filtersValid = false;

    double currentSampleRate = 48000.0;
    int maximumSamples = 0;
};

enum class AuxParameter
{
    reverbSize,
    reverbDamping,
    reverbReturn,
    delayTime,
    delaySync,
    delayFeedback,
    delayReturn,
    count
};

const ParameterInfo& getParameterInfo (AuxParameter);

/** A shared reverb and delay that tracks send to, so seven tracks can use one
    instance of each.
*/
class AuxBuses
{
public:
    AuxBuses();

    void prepare (double sampleRate, int maximumBlockSize);
    void setParameter (AuxParameter, float value) noexcept;
    float getParameter (AuxParameter) const noexcept;
    void resetParameters() noexcept;

    void clearSends (int numSamples) noexcept;
    void addToSends (const float* left, const float* right, float reverbLevel, float delayLevel, int numSamples) noexcept;

    /** Runs the effects and adds their output to the mix. */
    void addReturns (float* left, float* right, int numSamples, double tempo) noexcept;

private:
    std::array<std::atomic<float>, (size_t) AuxParameter::count> parameters;

    juce::AudioBuffer<float> reverbSend, delaySend, delayLine;
    juce::Reverb reverb;
    int delayWritePosition = 0;
    double currentSampleRate = 48000.0;
};

} // namespace roughtrack
