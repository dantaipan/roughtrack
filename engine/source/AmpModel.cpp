#include "rifftrack/AmpModel.h"

#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <filesystem>

namespace rifftrack
{

namespace
{

// Captures come out at whatever level their amp was, so each is brought to the
// same loudness. Otherwise changing amps could jump 20 dB.
constexpr double targetLoudness = -18.0;

bool canRunAt (nam::DSP& dsp, double sampleRate)
{
    return dsp.SupportsArbitrarySampleRate() || dsp.GetExpectedSampleRate() <= 0.0
           || juce::approximatelyEqual (dsp.GetExpectedSampleRate(), sampleRate);
}

} // namespace

AmpModel::LoadResult AmpModel::load (const juce::File& file, double sampleRate, int maximumBlockSize, bool preferLite)
{
    try
    {
        auto dsp = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));

        if (dsp == nullptr)
            return { nullptr, "It isn't a NAM capture." };

        if (dsp->NumInputChannels() != 1 || dsp->NumOutputChannels() != 1)
            return { nullptr, "Only mono captures are supported." };

        if (! canRunAt (*dsp, sampleRate))
            return { nullptr, "This capture was made at " + juce::String (dsp->GetExpectedSampleRate()) + " Hz, but the device is running at "
                                  + juce::String (sampleRate) + " Hz." };

        auto* slimmableModel = dynamic_cast<nam::SlimmableModel*> (dsp.get());

        if (slimmableModel != nullptr)
            slimmableModel->SetSlimmableSize (preferLite ? 0.0 : 1.0);

        dsp->Reset (sampleRate, maximumBlockSize);

        return { std::unique_ptr<AmpModel> (new AmpModel (std::move (dsp), maximumBlockSize, slimmableModel != nullptr)), {} };
    }
    catch (const std::exception& error)
    {
        return { nullptr, error.what() };
    }
}

AmpModel::AmpModel (std::unique_ptr<nam::DSP> dspToUse, int maximumBlockSize, bool isSlimmable)
    : dsp (std::move (dspToUse)), output ((size_t) maximumBlockSize), slimmable (isSlimmable)
{
    if (dsp->HasLoudness())
        outputGain = (float) std::pow (10.0, (targetLoudness - dsp->GetLoudness()) / 20.0);
}

AmpModel::~AmpModel() = default;

void AmpModel::prepare (double sampleRate, int maximumBlockSize)
{
    usable = canRunAt (*dsp, sampleRate);

    if (usable)
    {
        dsp->Reset (sampleRate, maximumBlockSize);
        output.resize ((size_t) maximumBlockSize);
    }
}

void AmpModel::process (float* samples, int numSamples) noexcept
{
    const auto capacity = (int) output.size();

    for (int done = 0; done < numSamples;)
    {
        const auto chunk = juce::jmin (numSamples - done, capacity);

        float* inputs[] { samples + done };
        float* outputs[] { output.data() };

        dsp->process (inputs, outputs, chunk);
        juce::FloatVectorOperations::copyWithMultiply (samples + done, output.data(), outputGain, chunk);

        done += chunk;
    }
}

} // namespace rifftrack
