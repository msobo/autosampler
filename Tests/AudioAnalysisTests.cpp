#include "engine/AudioAnalysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace autosampler;

namespace
{
    juce::AudioBuffer<float> makeSine (int numChannels, int silenceBefore, int length, int silenceAfter,
                                       double sampleRate = 48000.0, double frequency = 440.0, float amplitude = 0.5f)
    {
        juce::AudioBuffer<float> buffer (numChannels, silenceBefore + length + silenceAfter);
        buffer.clear();

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < length; ++i)
                buffer.setSample (ch, silenceBefore + i,
                                  amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / sampleRate));

        return buffer;
    }
}

TEST_CASE ("TailDetector erkennt anhaltende Stille")
{
    constexpr double sr = 48000.0;
    const auto buffer = makeSine (2, 0, 4800, 48000);

    TailDetector detector (sr, -70.0f, 0.3, 0.01);

    int finishedAt = -1;

    for (int pos = 0; pos < buffer.getNumSamples(); pos += 256)
    {
        if (detector.process (buffer, pos, juce::jmin (256, buffer.getNumSamples() - pos)))
        {
            finishedAt = pos;
            break;
        }
    }

    REQUIRE (finishedAt > 0);
    // 0,1 s Ton + 0,3 s Stille, Auflösung ein Fenster und ein Block
    CHECK (finishedAt >= 4800 + 14400 - 480 - 256);
    CHECK (finishedAt <= 4800 + 14400 + 480 + 256);
}

TEST_CASE ("TailDetector setzt Stille bei erneutem Signal zurück")
{
    juce::AudioBuffer<float> buffer (1, 48000);
    buffer.clear();
    buffer.setSample (0, 9000, 1.0f);   // Knackser nach 0,19 s

    TailDetector detector (48000.0, -70.0f, 0.3, 0.01);
    CHECK_FALSE (detector.process (buffer, 0, 9600 + 4000));
    CHECK (detector.process (buffer, 13600, 48000 - 13600));
}

TEST_CASE ("Anfang und Ende des Klangs")
{
    const auto buffer = makeSine (2, 1000, 4800, 2000);
    CHECK (findFirstSampleAbove (buffer, 0.001f) >= 1000);
    CHECK (findFirstSampleAbove (buffer, 0.001f) < 1010);

    const auto end = findSoundEnd (buffer, juce::Decibels::decibelsToGain (-60.0f), 100);
    CHECK (end >= 5800);
    CHECK (end <= 5900);

    juce::AudioBuffer<float> silence (2, 1000);
    silence.clear();
    CHECK (findFirstSampleAbove (silence, 0.001f) == -1);
    CHECK (findSoundEnd (silence, 0.001f, 100) == 0);
}

TEST_CASE ("Trimmen mit Pre-Roll und Fade-out")
{
    auto buffer = makeSine (2, 1000, 4800, 9000);

    TrimSettings trim;
    trim.preRollSamples = 10;
    trim.fadeOutSamples = 480;
    trim.windowSamples = 100;
    REQUIRE (trimSample (buffer, trim));

    // Der Sinus beginnt bei Phase 0, das erste Sample über -60 dB liegt knapp danach
    const auto onset = findFirstSampleAbove (buffer, juce::Decibels::decibelsToGain (-60.0f));
    CHECK (onset >= 10);
    CHECK (onset <= 12);
    CHECK (buffer.getNumSamples() <= 10 + 4800 + 100 + 2);
    CHECK (buffer.getNumSamples() >= 10 + 4800 - 2);
    CHECK (buffer.getSample (0, buffer.getNumSamples() - 1) == Catch::Approx (0.0f).margin (0.01f));

    auto untrimmedStart = makeSine (1, 1000, 4800, 0);
    trim.trimStart = false;
    REQUIRE (trimSample (untrimmedStart, trim));
    CHECK (findFirstSampleAbove (untrimmedStart, 0.001f) >= 1000);

    juce::AudioBuffer<float> silence (2, 1000);
    silence.clear();
    CHECK_FALSE (trimSample (silence, trim));
}

TEST_CASE ("Resampling erhält Länge, Tonhöhe und Timing")
{
    // 1 kHz-Sinus ab 0,1 s
    const auto source = makeSine (2, 4410, 44100, 0, 44100.0, 1000.0);
    const auto result = resample (source, 44100.0, 48000.0);

    CHECK (result.getNumSamples() == Catch::Approx (source.getNumSamples() * 48000.0 / 44100.0).margin (1));
    CHECK (result.getNumChannels() == 2);

    const auto onset = findFirstSampleAbove (result, 0.05f);
    CHECK (onset == Catch::Approx (4800).margin (4));

    // Nulldurchgänge in 0,5 s Signal mitten im Ton: 1000 Hz -> 1000 Durchgänge
    int crossings = 0;

    for (int i = 9600; i < 9600 + 24000; ++i)
        if ((result.getSample (0, i - 1) < 0.0f) != (result.getSample (0, i) < 0.0f))
            ++crossings;

    CHECK (crossings == Catch::Approx (1000).margin (2));

    const auto unchanged = resample (source, 48000.0, 48000.0);
    CHECK (unchanged.getNumSamples() == source.getNumSamples());
}
