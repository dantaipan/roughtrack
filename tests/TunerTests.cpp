#include "rifftrack/AudioEngine.h"
#include "rifftrack/Tuner.h"

#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <numbers>

using namespace rifftrack;
using Catch::Approx;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;

/** A plucked string's shape: strong overtones, the second often louder than the first. */
std::vector<float> makeString (double frequency, int numSamples, double rate = sampleRate)
{
    const std::array<float, 6> harmonics { 0.4f, 1.0f, 0.6f, 0.3f, 0.2f, 0.1f };

    std::vector<float> samples ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto time = (double) i / rate;
        auto value = 0.0f;

        for (size_t h = 0; h < harmonics.size(); ++h)
            value += harmonics[h] * (float) std::sin (twoPi * frequency * (double) (h + 1) * time);

        samples[(size_t) i] = 0.1f * value * (float) std::exp (-2.0 * time);
    }

    return samples;
}

double centsBetween (double a, double b)
{
    return 1200.0 * std::log2 (a / b);
}

} // namespace

TEST_CASE ("Frequencies read as the nearest note and how far off it")
{
    CHECK (getTunerNoteName (readingForFrequency (82.41).note) == "E2");
    CHECK (getTunerNoteName (readingForFrequency (440.0).note) == "A4");
    CHECK (readingForFrequency (440.0).cents == Approx (0.0).margin (0.001));
    CHECK (readingForFrequency (440.0 * std::pow (2.0, 10.0 / 1200.0)).cents == Approx (10.0));
    CHECK (readingForFrequency (440.0 * std::pow (2.0, -10.0 / 1200.0)).cents == Approx (-10.0));
}

TEST_CASE ("The tuner reads strings to within a cent")
{
    // Five-string bass low B, then guitar and bass open strings, then high on the neck.
    const auto frequency = GENERATE (30.87, 41.2, 55.0, 73.42, 82.41, 98.0, 110.0, 146.83, 196.0, 246.94, 329.63, 659.26, 1318.51);

    PitchDetector detector (sampleRate);
    const auto samples = makeString (frequency, detector.getWindowSize());
    const auto reading = detector.analyse (samples.data());

    REQUIRE (reading.hasPitch);
    CHECK (std::abs (centsBetween (reading.frequency, frequency)) < 1.0);
}

TEST_CASE ("The tuner reads a plain sine as well")
{
    const auto frequency = GENERATE (41.2, 82.41, 329.63, 1318.51);

    PitchDetector detector (sampleRate);
    std::vector<float> samples ((size_t) detector.getWindowSize());

    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = 0.2f * (float) std::sin (twoPi * frequency * (double) i / sampleRate);

    const auto reading = detector.analyse (samples.data());

    REQUIRE (reading.hasPitch);
    CHECK (std::abs (centsBetween (reading.frequency, frequency)) < 1.0);
}

TEST_CASE ("The tuner tells a slightly flat string from an in-tune one")
{
    const auto flat = 110.0 * std::pow (2.0, -3.0 / 1200.0);

    PitchDetector detector (sampleRate);
    const auto samples = makeString (flat, detector.getWindowSize());
    const auto reading = detector.analyse (samples.data());

    REQUIRE (reading.hasPitch);
    CHECK (getTunerNoteName (reading.note) == "A2");
    CHECK (reading.cents == Approx (-3.0).margin (1.0));
}

TEST_CASE ("The tuner works at other sample rates")
{
    const auto rate = GENERATE (44100.0, 96000.0);

    PitchDetector detector (rate);
    const auto samples = makeString (82.41, detector.getWindowSize(), rate);
    const auto reading = detector.analyse (samples.data());

    REQUIRE (reading.hasPitch);
    CHECK (std::abs (centsBetween (reading.frequency, 82.41)) < 1.0);
}

TEST_CASE ("The tuner stays quiet without a note")
{
    PitchDetector detector (sampleRate);
    std::vector<float> samples ((size_t) detector.getWindowSize());

    SECTION ("silence")
    {
        CHECK_FALSE (detector.analyse (samples.data()).hasPitch);
    }

    SECTION ("a string that's nearly gone")
    {
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = 0.0002f * (float) std::sin (twoPi * 110.0 * (double) i / sampleRate);

        CHECK_FALSE (detector.analyse (samples.data()).hasPitch);
    }

    SECTION ("noise")
    {
        juce::Random random (1234);

        for (auto& sample : samples)
            sample = 0.1f * (random.nextFloat() * 2.0f - 1.0f);

        CHECK_FALSE (detector.analyse (samples.data()).hasPitch);
    }
}

TEST_CASE ("The tuner hears the chosen input and costs nothing when off")
{
    test::TempDirectory temp;
    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    engine.prepare (sampleRate, 64);

    std::vector<float> first (64, 0.25f), second (64, 0.5f), left (64), right (64), received (256);
    const float* inputs[] { first.data(), second.data() };
    float* outputs[] { left.data(), right.data() };

    engine.processBlock (inputs, 2, outputs, 2, 64);
    CHECK (engine.readTuner (received.data(), 256) == 0);

    engine.setTunerInput (1);
    engine.processBlock (inputs, 2, outputs, 2, 64);
    engine.processBlock (inputs, 2, outputs, 2, 64);

    REQUIRE (engine.readTuner (received.data(), 256) == 128);
    CHECK (received[0] == 0.5f);
    CHECK (received[127] == 0.5f);

    engine.setTunerInput (-1);
    engine.processBlock (inputs, 2, outputs, 2, 64);
    CHECK (engine.readTuner (received.data(), 256) == 0);
}

TEST_CASE ("A tuner nobody reads doesn't disturb the audio")
{
    test::TempDirectory temp;
    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    engine.prepare (sampleRate, 64);
    engine.setTunerInput (0);

    std::vector<float> input (64, 0.25f), left (64), right (64), received (64);
    const float* inputs[] { input.data() };
    float* outputs[] { left.data(), right.data() };

    for (int block = 0; block < 2000; ++block)
        engine.processBlock (inputs, 1, outputs, 2, 64);

    CHECK (engine.readTuner (received.data(), 64) == 64);
}

TEST_CASE ("A note survives the trip from the audio thread to the tuner")
{
    test::TempDirectory temp;
    AudioEngine engine;
    engine.setSessionDirectory (temp.directory);
    engine.prepare (sampleRate, 64);
    engine.setTunerInput (0);

    PitchDetector detector (sampleRate);
    const auto played = makeString (110.0, detector.getWindowSize() + 64);
    std::vector<float> left (64), right (64), received ((size_t) detector.getWindowSize() + 64);

    auto position = 0;

    while (position + 64 <= (int) played.size())
    {
        const float* inputs[] { played.data() + position };
        float* outputs[] { left.data(), right.data() };

        engine.processBlock (inputs, 1, outputs, 2, 64);
        position += 64;
    }

    REQUIRE (engine.readTuner (received.data(), (int) received.size()) >= detector.getWindowSize());

    const auto reading = detector.analyse (received.data());

    REQUIRE (reading.hasPitch);
    CHECK (getTunerNoteName (reading.note) == "A2");
}

namespace
{

/** A real string's overtones run slightly sharp and fade at different speeds. The
    faint tone an octave down stands in for hum and other strings ringing.
*/
std::vector<float> makeRealisticString (double frequency, int numSamples, float wobble)
{
    constexpr double stiffness = 0.0008;

    std::vector<float> samples ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto time = (double) i / sampleRate;
        auto value = wobble * (float) std::sin (twoPi * frequency * 0.5 * time);

        for (int h = 1; h <= 8; ++h)
        {
            const auto partial = frequency * h * std::sqrt (1.0 + stiffness * h * h);
            value += (float) (std::exp (-time * h) * std::sin (twoPi * partial * time) / h);
        }

        samples[(size_t) i] = 0.1f * value;
    }

    return samples;
}

} // namespace

TEST_CASE ("The tuner doesn't drop an octave on a real string")
{
    const auto frequency = GENERATE (82.41, 110.0, 146.83, 196.0);
    const auto wobble = GENERATE (0.02f, 0.05f, 0.1f);

    PitchDetector detector (sampleRate);
    const auto samples = makeRealisticString (frequency, detector.getWindowSize(), wobble);
    const auto reading = detector.analyse (samples.data());

    REQUIRE (reading.hasPitch);
    CHECK (reading.note == readingForFrequency (frequency).note);
}
