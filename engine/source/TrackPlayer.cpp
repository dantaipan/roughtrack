#include "rifftrack/TrackPlayer.h"

namespace rifftrack
{

namespace
{
constexpr int ringSize = 1 << 17;
constexpr int diskPollMs = 20;
} // namespace

TrackPlayer::TrackPlayer (juce::TimeSliceThread& diskThread, juce::AudioFormatManager& formats)
    : thread (diskThread), formatManager (formats), ring (2, ringSize), fifo (ringSize)
{
    thread.addTimeSliceClient (this);
}

TrackPlayer::~TrackPlayer()
{
    thread.removeTimeSliceClient (this);
}

bool TrackPlayer::load (const juce::File& file, juce::int64 startSample)
{
    std::unique_ptr<juce::AudioFormatReader> newReader (formatManager.createReaderFor (file));

    if (newReader == nullptr)
        return false;

    thread.removeTimeSliceClient (this);
    reader = std::move (newReader);
    takeStart = startSample;
    hasTake = true;
    thread.addTimeSliceClient (this);
    return true;
}

void TrackPlayer::clear()
{
    thread.removeTimeSliceClient (this);
    reader.reset();
    hasTake = false;
    thread.addTimeSliceClient (this);
}

void TrackPlayer::prepareToPlay (juce::int64 timelinePosition)
{
    thread.removeTimeSliceClient (this);

    fifo.reset();
    samplesToSkip = 0;
    readPosition = timelinePosition;

    if (reader != nullptr)
        fillBuffer();

    thread.addTimeSliceClient (this);
}

void TrackPlayer::pull (float* left, float* right, int numSamples) noexcept
{
    if (! hasTake.load())
    {
        juce::FloatVectorOperations::clear (left, numSamples);
        juce::FloatVectorOperations::clear (right, numSamples);
        return;
    }

    int start1, size1, start2, size2;
    fifo.prepareToRead (numSamples, start1, size1, start2, size2);

    juce::FloatVectorOperations::copy (left, ring.getReadPointer (0, start1), size1);
    juce::FloatVectorOperations::copy (right, ring.getReadPointer (1, start1), size1);
    juce::FloatVectorOperations::copy (left + size1, ring.getReadPointer (0, start2), size2);
    juce::FloatVectorOperations::copy (right + size1, ring.getReadPointer (1, start2), size2);

    const auto numRead = size1 + size2;
    fifo.finishedRead (numRead);

    if (numRead < numSamples)
    {
        const auto missing = numSamples - numRead;
        juce::FloatVectorOperations::clear (left + numRead, missing);
        juce::FloatVectorOperations::clear (right + numRead, missing);

        // Skipping what was missed keeps the rest of the take in time.
        samplesToSkip.fetch_add (missing);
        underrunSamples.fetch_add (missing);
    }
}

int TrackPlayer::useTimeSlice()
{
    if (reader != nullptr)
        fillBuffer();

    return diskPollMs;
}

void TrackPlayer::fillBuffer()
{
    readPosition += samplesToSkip.exchange (0);

    int start1, size1, start2, size2;
    fifo.prepareToWrite (fifo.getFreeSpace(), start1, size1, start2, size2);

    render (start1, size1);
    render (start2, size2);

    fifo.finishedWrite (size1 + size2);
}

void TrackPlayer::render (int ringStart, int numSamples)
{
    if (numSamples <= 0)
        return;

    auto* left = ring.getWritePointer (0, ringStart);
    auto* right = ring.getWritePointer (1, ringStart);

    const auto filePosition = readPosition - takeStart;
    const auto numBeforeTake = (int) juce::jlimit<juce::int64> (0, numSamples, -filePosition);

    juce::FloatVectorOperations::clear (left, numBeforeTake);
    juce::FloatVectorOperations::clear (right, numBeforeTake);

    if (numBeforeTake < numSamples)
    {
        float* const destinations[] { left + numBeforeTake, right + numBeforeTake };
        const auto numFromFile = numSamples - numBeforeTake;
        const auto numChannels = reader->numChannels > 1 ? 2 : 1;

        reader->read (destinations, numChannels, filePosition + numBeforeTake, numFromFile);

        if (numChannels == 1)
            juce::FloatVectorOperations::copy (destinations[1], destinations[0], numFromFile);
    }

    readPosition += numSamples;
}

} // namespace rifftrack
