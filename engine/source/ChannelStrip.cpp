#include "roughtrack/ChannelStrip.h"

#include <cmath>

namespace roughtrack
{

namespace
{

constexpr float lowShelfFrequency = 120.0f;
constexpr float highShelfFrequency = 6000.0f;
constexpr float shelfQ = 0.707f;
constexpr float midQ = 1.0f;
constexpr float compressorAttackMs = 10.0f;
constexpr float compressorReleaseMs = 120.0f;
constexpr double longestDelaySeconds = 2.0;
constexpr double dottedEighthBeats = 0.75;

// A speaker cabinet's sound is over in 200 ms, and anything longer is room. It's
// also what sets the CPU cost, which is the limit at small buffers.
constexpr size_t longestCabinetSamples = 9600;
constexpr int cabinetHeadSamples = 1024;

// JUCE scales every IR to an energy of 0.125, which is what makes cabinets match
// each other. Undoing the 0.125 keeps a cabinet from pulling the track down 18 dB.
constexpr float cabinetMakeup = 8.0f;

const std::array<ParameterInfo, (size_t) ChannelParameter::count> channelParameters { {
    { "ampEnabled", "Amp", 0.0f, 1.0f, 1.0f, "", true },
    { "ampInput", "Amp Input", -24.0f, 24.0f, 0.0f, " dB" },
    { "eqEnabled", "EQ", 0.0f, 1.0f, 0.0f, "", true },
    { "highPass", "High Pass", 0.0f, 400.0f, 0.0f, " Hz" },
    { "lowGain", "Low", -15.0f, 15.0f, 0.0f, " dB" },
    { "midGain", "Mid", -15.0f, 15.0f, 0.0f, " dB" },
    { "midFrequency", "Mid Freq", 200.0f, 5000.0f, 1000.0f, " Hz" },
    { "highGain", "High", -15.0f, 15.0f, 0.0f, " dB" },
    { "compressorEnabled", "Compressor", 0.0f, 1.0f, 0.0f, "", true },
    { "compressorThreshold", "Threshold", -50.0f, 0.0f, -18.0f, " dB" },
    { "compressorRatio", "Ratio", 1.0f, 20.0f, 3.0f, ":1" },
    { "compressorMakeup", "Makeup", 0.0f, 24.0f, 0.0f, " dB" },
    { "sendReverb", "Reverb Send", 0.0f, 1.0f, 0.0f, "" },
    { "sendDelay", "Delay Send", 0.0f, 1.0f, 0.0f, "" },
} };

const std::array<ParameterInfo, (size_t) AuxParameter::count> auxParameters { {
    { "reverbSize", "Size", 0.0f, 1.0f, 0.5f, "" },
    { "reverbDamping", "Damping", 0.0f, 1.0f, 0.5f, "" },
    { "reverbReturn", "Reverb Return", 0.0f, 1.0f, 1.0f, "" },
    { "delayTime", "Time", 10.0f, 2000.0f, 375.0f, " ms" },
    { "delaySync", "Sync to Tempo", 0.0f, 1.0f, 0.0f, "", true },
    { "delayFeedback", "Feedback", 0.0f, 0.9f, 0.3f, "" },
    { "delayReturn", "Delay Return", 0.0f, 1.0f, 1.0f, "" },
} };

} // namespace

const ParameterInfo& getParameterInfo (ChannelParameter parameter)
{
    return channelParameters[(size_t) parameter];
}

const ParameterInfo& getParameterInfo (AuxParameter parameter)
{
    return auxParameters[(size_t) parameter];
}

void ChannelStrip::Biquad::setCoefficients (const std::array<float, 6>& coefficients) noexcept
{
    const auto a0Inverse = 1.0f / coefficients[3];

    b0 = coefficients[0] * a0Inverse;
    b1 = coefficients[1] * a0Inverse;
    b2 = coefficients[2] * a0Inverse;
    a1 = coefficients[4] * a0Inverse;
    a2 = coefficients[5] * a0Inverse;
}

void ChannelStrip::Biquad::process (float* samples, int numSamples, int channel) noexcept
{
    auto& [z1, z2] = state[(size_t) channel];

    for (int i = 0; i < numSamples; ++i)
    {
        const auto input = samples[i];
        const auto output = b0 * input + z1;

        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        samples[i] = output;
    }
}

void ChannelStrip::Biquad::reset() noexcept
{
    state = {};
}

ChannelStrip::ChannelStrip()
    : cabinet (juce::dsp::Convolution::NonUniform { cabinetHeadSamples })
{
    resetParameters();
}

void ChannelStrip::resetParameters() noexcept
{
    for (size_t i = 0; i < parameters.size(); ++i)
        parameters[i] = channelParameters[i].defaultValue;
}

void ChannelStrip::prepare (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = sampleRate;
    maximumSamples = maximumBlockSize;
    filtersValid = false;

    const juce::dsp::ProcessSpec stereo { sampleRate, (juce::uint32) maximumBlockSize, 2 };
    const juce::dsp::ProcessSpec mono { sampleRate, (juce::uint32) maximumBlockSize, 1 };

    compressor.prepare (stereo);
    compressor.setAttack (compressorAttackMs);
    compressor.setRelease (compressorReleaseMs);
    cabinet.prepare (mono);

    for (auto* filter : { &highPassFilter, &lowShelf, &midPeak, &highShelf })
        filter->reset();

    if (amp != nullptr)
        amp->prepare (sampleRate, maximumBlockSize);
}

void ChannelStrip::setParameter (ChannelParameter parameter, float value) noexcept
{
    const auto& info = getParameterInfo (parameter);
    parameters[(size_t) parameter] = juce::jlimit (info.minimum, info.maximum, value);
}

float ChannelStrip::getParameter (ChannelParameter parameter) const noexcept
{
    return parameters[(size_t) parameter].load();
}

std::unique_ptr<AmpModel> ChannelStrip::exchangeAmp (std::unique_ptr<AmpModel> newAmp)
{
    std::swap (amp, newAmp);
    return newAmp;
}

juce::Result ChannelStrip::loadCabinet (const juce::File& file)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("Can't find " + file.getFileName());

    cabinet.loadImpulseResponse (file,
                                 juce::dsp::Convolution::Stereo::no,
                                 juce::dsp::Convolution::Trim::no,
                                 longestCabinetSamples,
                                 juce::dsp::Convolution::Normalise::yes);
    cabinetFile = file;
    cabinetLoaded = true;
    return juce::Result::ok();
}

void ChannelStrip::clearCabinet()
{
    cabinetLoaded = false;
    cabinetFile = juce::File();
}

void ChannelStrip::processAmp (float* left, float* right, int numSamples) noexcept
{
    const auto ampIsOn = amp != nullptr && amp->isUsable() && getParameter (ChannelParameter::ampEnabled) > 0.5f;

    if (! ampIsOn && ! cabinetLoaded.load())
        return;

    // Amps are mono, so a stereo track is folded down first.
    if (left != right)
    {
        juce::FloatVectorOperations::add (left, right, numSamples);
        juce::FloatVectorOperations::multiply (left, 0.5f, numSamples);
    }

    if (ampIsOn)
    {
        juce::FloatVectorOperations::multiply (left, juce::Decibels::decibelsToGain (getParameter (ChannelParameter::ampInput)), numSamples);
        amp->process (left, numSamples);
    }

    if (cabinetLoaded.load())
    {
        float* channels[] { left };
        juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
        cabinet.process (juce::dsp::ProcessContextReplacing<float> (block));
        juce::FloatVectorOperations::multiply (left, cabinetMakeup, numSamples);
    }

    juce::FloatVectorOperations::copy (right, left, numSamples);
}

void ChannelStrip::updateFilters() noexcept
{
    using Coefficients = juce::dsp::IIR::ArrayCoefficients<float>;

    const std::array<float, 5> values { getParameter (ChannelParameter::highPass),
                                        getParameter (ChannelParameter::lowGain),
                                        getParameter (ChannelParameter::midGain),
                                        getParameter (ChannelParameter::midFrequency),
                                        getParameter (ChannelParameter::highGain) };

    if (filtersValid && values == filterValues)
        return;

    filterValues = values;
    filtersValid = true;

    if (values[0] > 0.0f)
        highPassFilter.setCoefficients (Coefficients::makeHighPass (currentSampleRate, values[0]));

    const auto gain = [] (float decibels) { return juce::Decibels::decibelsToGain (decibels, -100.0f); };

    lowShelf.setCoefficients (Coefficients::makeLowShelf (currentSampleRate, lowShelfFrequency, shelfQ, gain (values[1])));
    midPeak.setCoefficients (Coefficients::makePeakFilter (currentSampleRate, values[3], midQ, gain (values[2])));
    highShelf.setCoefficients (Coefficients::makeHighShelf (currentSampleRate, highShelfFrequency, shelfQ, gain (values[4])));
}

void ChannelStrip::processChannel (float* left, float* right, int numSamples) noexcept
{
    if (getParameter (ChannelParameter::eqEnabled) > 0.5f)
    {
        updateFilters();

        float* channels[] { left, right };

        for (int channel = 0; channel < 2; ++channel)
        {
            if (filterValues[0] > 0.0f)
                highPassFilter.process (channels[channel], numSamples, channel);

            lowShelf.process (channels[channel], numSamples, channel);
            midPeak.process (channels[channel], numSamples, channel);
            highShelf.process (channels[channel], numSamples, channel);
        }
    }

    if (getParameter (ChannelParameter::compressorEnabled) > 0.5f)
    {
        compressor.setThreshold (getParameter (ChannelParameter::compressorThreshold));
        compressor.setRatio (getParameter (ChannelParameter::compressorRatio));

        float* channels[] { left, right };
        juce::dsp::AudioBlock<float> block (channels, 2, (size_t) numSamples);
        compressor.process (juce::dsp::ProcessContextReplacing<float> (block));

        const auto makeup = juce::Decibels::decibelsToGain (getParameter (ChannelParameter::compressorMakeup));
        juce::FloatVectorOperations::multiply (left, makeup, numSamples);
        juce::FloatVectorOperations::multiply (right, makeup, numSamples);
    }
}

AuxBuses::AuxBuses()
{
    resetParameters();
}

void AuxBuses::resetParameters() noexcept
{
    for (size_t i = 0; i < parameters.size(); ++i)
        parameters[i] = auxParameters[i].defaultValue;
}

void AuxBuses::prepare (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = sampleRate;

    reverbSend.setSize (2, maximumBlockSize);
    delaySend.setSize (2, maximumBlockSize);
    delayLine.setSize (2, (int) (longestDelaySeconds * sampleRate) + maximumBlockSize + 1);
    delayLine.clear();
    delayWritePosition = 0;

    reverb.setSampleRate (sampleRate);
    reverb.reset();
}

void AuxBuses::setParameter (AuxParameter parameter, float value) noexcept
{
    const auto& info = getParameterInfo (parameter);
    parameters[(size_t) parameter] = juce::jlimit (info.minimum, info.maximum, value);
}

float AuxBuses::getParameter (AuxParameter parameter) const noexcept
{
    return parameters[(size_t) parameter].load();
}

void AuxBuses::clearSends (int numSamples) noexcept
{
    reverbSend.clear (0, numSamples);
    delaySend.clear (0, numSamples);
}

void AuxBuses::addToSends (const float* left, const float* right, float reverbLevel, float delayLevel, int numSamples) noexcept
{
    if (reverbLevel > 0.0f)
    {
        reverbSend.addFrom (0, 0, left, numSamples, reverbLevel);
        reverbSend.addFrom (1, 0, right, numSamples, reverbLevel);
    }

    if (delayLevel > 0.0f)
    {
        delaySend.addFrom (0, 0, left, numSamples, delayLevel);
        delaySend.addFrom (1, 0, right, numSamples, delayLevel);
    }
}

void AuxBuses::addReturns (float* left, float* right, int numSamples, double tempo) noexcept
{
    juce::Reverb::Parameters reverbParameters;
    reverbParameters.roomSize = getParameter (AuxParameter::reverbSize);
    reverbParameters.damping = getParameter (AuxParameter::reverbDamping);
    reverbParameters.wetLevel = 1.0f;
    reverbParameters.dryLevel = 0.0f;
    reverb.setParameters (reverbParameters);
    reverb.processStereo (reverbSend.getWritePointer (0), reverbSend.getWritePointer (1), numSamples);

    const auto reverbReturn = getParameter (AuxParameter::reverbReturn);
    juce::FloatVectorOperations::addWithMultiply (left, reverbSend.getReadPointer (0), reverbReturn, numSamples);
    juce::FloatVectorOperations::addWithMultiply (right, reverbSend.getReadPointer (1), reverbReturn, numSamples);

    const auto delaySeconds = getParameter (AuxParameter::delaySync) > 0.5f
                                  ? dottedEighthBeats * 60.0 / juce::jmax (1.0, tempo)
                                  : (double) getParameter (AuxParameter::delayTime) / 1000.0;

    const auto delaySamples = juce::jlimit (1, delayLine.getNumSamples() - 1, (int) std::round (delaySeconds * currentSampleRate));
    const auto feedback = getParameter (AuxParameter::delayFeedback);
    const auto delayReturn = getParameter (AuxParameter::delayReturn);
    const auto lineLength = delayLine.getNumSamples();

    float* outputs[] { left, right };

    for (int channel = 0; channel < 2; ++channel)
    {
        auto* line = delayLine.getWritePointer (channel);
        const auto* send = delaySend.getReadPointer (channel);
        auto write = delayWritePosition;

        for (int i = 0; i < numSamples; ++i)
        {
            auto read = write - delaySamples;

            if (read < 0)
                read += lineLength;

            const auto delayed = line[read];

            line[write] = send[i] + delayed * feedback;
            outputs[channel][i] += delayed * delayReturn;

            if (++write == lineLength)
                write = 0;
        }
    }

    delayWritePosition = (delayWritePosition + numSamples) % lineLength;
}

} // namespace roughtrack
