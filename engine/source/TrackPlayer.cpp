#include "roughtrack/TrackPlayer.h"

namespace roughtrack
{

namespace
{
constexpr int ringSize = 1 << 17;
constexpr int diskPollMs = 20;

void fadeAtEdges (float* left, float* right, int numSamples, const ClipList& clips, juce::int64 position)
{
    const auto search = edgeFadeSamples + numSamples;
    const auto [sinceEdge, untilEdge] = findEdgeDistances (clips, position, search);

    if (sinceEdge >= edgeFadeSamples && untilEdge >= search)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto fromEdge = juce::jmin (sinceEdge + i + 1, untilEdge - i);
        const auto gain = juce::jmin (1.0f, (float) fromEdge / (float) edgeFadeSamples);

        left[i] *= gain;
        right[i] *= gain;
    }
}
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
    const std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
        return false;

    Clip clip;
    clip.file = file;
    clip.timelineStart = startSample;
    clip.length = reader->lengthInSamples;
    clip.fileLength = reader->lengthInSamples;

    return setClips ({ clip });
}

void TrackPlayer::clear()
{
    setClips ({});
}

bool TrackPlayer::setClips (const ClipList& newClips)
{
    thread.removeTimeSliceClient (this);

    decltype (readers) newReaders;
    auto allFound = true;

    for (const auto& clip : newClips)
    {
        if (newReaders.count (clip.file) != 0)
            continue;

        const auto existing = readers.find (clip.file);

        if (existing != readers.end())
        {
            newReaders[clip.file] = existing->second;
            continue;
        }

        std::shared_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (clip.file));

        if (reader != nullptr)
            newReaders[clip.file] = reader;
        else
            allFound = false;
    }

    clips = newClips;
    readers = std::move (newReaders);
    hasClips = ! clips.empty();

    thread.addTimeSliceClient (this);
    return allFound;
}

void TrackPlayer::prepareToPlay (juce::int64 timelinePosition, juce::Range<juce::int64> newCycle, int maxPrefill)
{
    thread.removeTimeSliceClient (this);

    fifo.reset();
    samplesToSkip = 0;
    readPosition = timelinePosition;
    cycle = newCycle;
    samplesSinceCycleStart = edgeFadeSamples;

    if (! clips.empty())
        fillBuffer (maxPrefill);

    thread.addTimeSliceClient (this);
}

void TrackPlayer::pull (float* left, float* right, int numSamples) noexcept
{
    if (! hasClips.load())
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
    if (! clips.empty())
        fillBuffer();

    return diskPollMs;
}

void TrackPlayer::fillBuffer (int maxSamples)
{
    advance (samplesToSkip.exchange (0));

    const auto space = maxSamples >= 0 ? juce::jmin (maxSamples, fifo.getFreeSpace()) : fifo.getFreeSpace();

    int start1, size1, start2, size2;
    fifo.prepareToWrite (space, start1, size1, start2, size2);

    render (start1, size1);
    render (start2, size2);

    fifo.finishedWrite (size1 + size2);
}

void TrackPlayer::render (int ringStart, int numSamples)
{
    for (int done = 0; done < numSamples;)
    {
        auto* left = ring.getWritePointer (0, ringStart + done);
        auto* right = ring.getWritePointer (1, ringStart + done);

        // A cycle's end is a seam, read up to and then jumped back from.
        const auto beforeCycleEnd = ! cycle.isEmpty() && readPosition < cycle.getEnd();
        const auto wanted = beforeCycleEnd ? (int) juce::jmin<juce::int64> (numSamples - done, cycle.getEnd() - readPosition)
                                           : numSamples - done;

        const auto segment = findSegment (clips, readPosition, wanted);
        const auto length = (int) segment.length;

        std::shared_ptr<juce::AudioFormatReader> reader;

        if (segment.clip >= 0)
        {
            const auto found = readers.find (clips[(size_t) segment.clip].file);

            if (found != readers.end())
                reader = found->second;
        }

        if (reader != nullptr)
        {
            const auto& clip = clips[(size_t) segment.clip];
            float* const destinations[] { left, right };
            const auto numChannels = reader->numChannels > 1 ? 2 : 1;

            reader->read (destinations, numChannels, clip.fileOffset + readPosition - clip.timelineStart, length);

            if (numChannels == 1)
                juce::FloatVectorOperations::copy (right, left, length);

            const auto positionInClip = readPosition - clip.timelineStart;

            if (overlapsFade (clip, positionInClip, length))
            {
                for (int i = 0; i < length; ++i)
                {
                    const auto gain = fadeGain (clip, positionInClip + i);
                    left[i] *= gain;
                    right[i] *= gain;
                }
            }

            fadeAtEdges (left, right, length, clips, readPosition);
        }
        else
        {
            juce::FloatVectorOperations::clear (left, length);
            juce::FloatVectorOperations::clear (right, length);
        }

        // Short fades either side of the seam keep the jump back from clicking.
        if (beforeCycleEnd)
        {
            for (int i = 0; i < length; ++i)
            {
                const auto untilEnd = cycle.getEnd() - (readPosition + i);
                const auto sinceStart = samplesSinceCycleStart + i;
                const auto fromSeam = juce::jmin<juce::int64> (untilEnd, sinceStart + 1);

                if (fromSeam < edgeFadeSamples)
                {
                    const auto gain = (float) fromSeam / (float) edgeFadeSamples;
                    left[i] *= gain;
                    right[i] *= gain;
                }
            }
        }

        advance (length);
        done += length;
    }
}

void TrackPlayer::advance (juce::int64 numSamples)
{
    const auto before = readPosition;
    readPosition += numSamples;
    samplesSinceCycleStart += numSamples;

    if (cycle.isEmpty() || before >= cycle.getEnd() || readPosition < cycle.getEnd())
        return;

    // Skipping past a missed stretch can run past the end more than once.
    readPosition = cycle.getStart() + (readPosition - cycle.getEnd()) % cycle.getLength();
    samplesSinceCycleStart = readPosition - cycle.getStart();
}

} // namespace roughtrack
