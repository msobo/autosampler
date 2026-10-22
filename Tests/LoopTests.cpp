#include "engine/LoopFinder.h"
#include "engine/SampleWriter.h"
#include "engine/Session.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace autosampler;

namespace
{
    constexpr double rate = 48000.0;

    /** Testkorpus: additive Klänge mit Hüllkurve. Haltezeit hold, danach 0,3 s Release. */
    struct ToneSpec
    {
        const char* name = "";
        double frequency = 220.0;
        int harmonics = 6;
        double hold = 1.5;
        double attack = 0.01;
        double decayTime = 0.0;         ///< > 0: exponentielles Abklingen während der Haltezeit
        double vibratoHz = 0.0, vibratoCents = 0.0;
        double detuneCents = 0.0;       ///< > 0: drei Stimmen (-d, 0, +d)
        float noise = 0.0f;
    };

    juce::AudioBuffer<float> makeTone (const ToneSpec& spec)
    {
        const auto release = 0.3;
        const auto numFrames = (int) ((spec.hold + release) * rate);
        juce::AudioBuffer<float> audio (2, numFrames);
        juce::Random random (1234);

        const auto detunes = spec.detuneCents > 0.0 ? std::vector<double> { -spec.detuneCents, 0.0, spec.detuneCents }
                                                    : std::vector<double> { 0.0 };
        std::vector<double> phases (detunes.size(), 0.0);

        for (int i = 0; i < numFrames; ++i)
        {
            const auto t = i / rate;
            auto envelope = juce::jmin (1.0, t / spec.attack);

            if (spec.decayTime > 0.0)
                envelope *= std::exp (-t / spec.decayTime);

            if (t > spec.hold)
                envelope *= juce::jmax (0.0, 1.0 - (t - spec.hold) / release);

            const auto vibrato = spec.vibratoCents * std::sin (juce::MathConstants<double>::twoPi * spec.vibratoHz * t);
            double value = 0.0;

            for (size_t v = 0; v < detunes.size(); ++v)
            {
                const auto frequency = spec.frequency * std::pow (2.0, (detunes[v] + vibrato) / 1200.0);
                phases[v] += juce::MathConstants<double>::twoPi * frequency / rate;

                for (int h = 1; h <= spec.harmonics; ++h)
                    value += std::sin (phases[v] * h) / h;
            }

            const auto sample = (float) (0.3 * envelope * value / (double) detunes.size())
                              + spec.noise * (float) envelope * (random.nextFloat() * 2.0f - 1.0f);
            audio.setSample (0, i, sample);
            audio.setSample (1, i, sample);
        }

        return audio;
    }

    const ToneSpec staticTone { "Statisch", 440.0, 3, 1.5 };
    const ToneSpec lead       { "Lead", 220.0, 8, 1.5, 0.02, 0.0, 5.0, 10.0 };
    const ToneSpec bass       { "Bass", 55.0, 10, 2.0, 0.005, 1.5 };
    const ToneSpec pad        { "Pad", 261.63, 6, 3.0, 0.3, 0.0, 0.0, 0.0, 7.0, 0.01f };

    LoopHints hintsFor (const ToneSpec& spec, int note)
    {
        return { note, (int) (spec.hold * rate) };
    }

    LoopSearchSettings settingsFor (LoopAlgorithm algorithm)
    {
        LoopSearchSettings settings;
        settings.algorithm = algorithm;
        return settings;
    }

    float maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from, int to)
    {
        float difference = 0.0f;

        for (int channel = 0; channel < a.getNumChannels(); ++channel)
            for (int i = from; i < to; ++i)
                difference = juce::jmax (difference, std::abs (a.getSample (channel, i) - b.getSample (channel, i)));

        return difference;
    }
}

//==============================================================================
TEST_CASE ("Crossfade: Kurven")
{
    for (const auto mode : { CrossfadeMode::linear, CrossfadeMode::sCurve })
    {
        for (const auto t : { 0.0f, 0.25f, 0.5f, 0.9f, 1.0f })
        {
            const auto gains = getCrossfadeGains (mode, t);
            CHECK (gains.fadeIn + gains.fadeOut == Catch::Approx (1.0f));
        }
    }

    for (const auto t : { 0.0f, 0.3f, 0.5f, 1.0f })
    {
        const auto gains = getCrossfadeGains (CrossfadeMode::equalPower, t);
        CHECK (gains.fadeIn * gains.fadeIn + gains.fadeOut * gains.fadeOut == Catch::Approx (1.0f));
    }

    CHECK (getCrossfadeGains (CrossfadeMode::sCurve, 0.0f).fadeIn == Catch::Approx (0.0f));
    CHECK (getCrossfadeGains (CrossfadeMode::sCurve, 1.0f).fadeIn == Catch::Approx (1.0f));
    CHECK (getMaxCrossfade (100, 1000) == 100);
    CHECK (getMaxCrossfade (1000, 1300) == 300);

    for (const auto mode : { CrossfadeMode::linear, CrossfadeMode::equalPower, CrossfadeMode::sCurve, CrossfadeMode::spectral })
        CHECK (parseCrossfadeMode (toString (mode), CrossfadeMode::linear) == mode);
}

TEST_CASE ("Crossfade: verändert nur den Bereich vor E und endet im Material vor S")
{
    juce::Random random (42);
    juce::AudioBuffer<float> original (2, 20000);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < original.getNumSamples(); ++i)
            original.setSample (channel, i, random.nextFloat() * 2.0f - 1.0f);

    const auto mode = GENERATE (CrossfadeMode::linear, CrossfadeMode::equalPower, CrossfadeMode::sCurve, CrossfadeMode::spectral);
    const int start = 5000, end = 15000, length = 2000;

    auto faded = original;
    applyLoopCrossfade (faded, start, end, length, mode, rate);

    CHECK (maxDifference (faded, original, 0, end - length) == 0.0f);
    CHECK (maxDifference (faded, original, end, original.getNumSamples()) == 0.0f);
    CHECK (maxDifference (faded, original, end - length, end) > 0.1f);

    // Am Ende des Crossfades fast nur noch das Material vor S (bei Spektral das hohe Band zuletzt)
    for (int channel = 0; channel < 2; ++channel)
        CHECK (std::abs (faded.getSample (channel, end - 1) - original.getSample (channel, start - 1)) < 0.05f);

    // Länge wird auf das Material vor S begrenzt
    auto clamped = original;
    applyLoopCrossfade (clamped, 300, end, length, mode, rate);
    CHECK (maxDifference (clamped, original, 0, end - 300) == 0.0f);
}

TEST_CASE ("Crossfade: spektrale Bänder rekonstruieren exakt")
{
    // Periodisches Signal, Loop über ganze Perioden: beide Seiten sind gleich,
    // der Crossfade darf also nichts ändern, wenn die Bänder sich exakt zu x summieren.
    auto tone = makeTone (staticTone);
    const auto period = 1200;           // 40 Hz-Raster: 440 Hz hat bei 48 kHz keine ganzzahlige Periode
    juce::AudioBuffer<float> periodic (1, 30000);

    for (int i = 0; i < periodic.getNumSamples(); ++i)
        periodic.setSample (0, i, tone.getSample (0, 10000 + i % period));

    auto faded = periodic;
    applyLoopCrossfade (faded, 6000, 6000 + 10 * period, 4000, CrossfadeMode::spectral, rate);
    CHECK (maxDifference (faded, periodic, 0, periodic.getNumSamples()) < 1.0e-3f);
}

//==============================================================================
TEST_CASE ("Loop: Grundton per YIN")
{
    const auto tone = makeTone (staticTone);
    const auto period = detectPitchPeriod (tone.getReadPointer (0, 20000), 8192, rate);
    CHECK (period == Catch::Approx (rate / 440.0).margin (0.05));

    const auto low = makeTone (bass);
    CHECK (detectPitchPeriod (low.getReadPointer (0, 10000), 16384, rate, 20.0, 200.0) == Catch::Approx (rate / 55.0).margin (0.5));

    std::vector<float> silence (8192, 0.0f);
    CHECK (juce::exactlyEqual (detectPitchPeriod (silence.data(), (int) silence.size(), rate), 0.0));
}

TEST_CASE ("Loop: Stille und zu kurze Samples")
{
    juce::AudioBuffer<float> silence (2, 48000);
    silence.clear();
    const auto result = findLoop (silence, rate, settingsFor (LoopAlgorithm::universal));
    CHECK_FALSE (result.found);
    CHECK (result.message.isNotEmpty());

    juce::AudioBuffer<float> tiny (1, 100);
    tiny.clear();
    CHECK_FALSE (findLoop (tiny, rate, {}).found);
}

TEST_CASE ("Loop: Testkorpus mit allen Algorithmen")
{
    struct Case { ToneSpec spec; int note; float minScore; };

    const auto algorithm = GENERATE (LoopAlgorithm::universal, LoopAlgorithm::tonal, LoopAlgorithm::pad);
    const auto testCase = GENERATE (Case { staticTone, 69, 90.0f }, Case { lead, 57, 70.0f },
                                    Case { bass, 33, 70.0f }, Case { pad, 60, 60.0f });

    const auto audio = makeTone (testCase.spec);
    const auto hints = hintsFor (testCase.spec, testCase.note);
    const auto settings = settingsFor (algorithm);
    const auto result = findLoop (audio, rate, settings, hints);

    INFO (testCase.spec.name << " / " << getDisplayName (algorithm) << ": S " << result.start << " E " << result.end
          << " X " << result.crossfade << " Score " << result.score << " " << result.message);

    REQUIRE (result.found);
    CHECK (result.start > (int) (testCase.spec.attack * rate));
    CHECK (result.end <= hints.sustainEndFrame);
    CHECK (result.end - result.start >= (int) (settings.minLoopSeconds * rate));
    CHECK (result.crossfade <= getMaxCrossfade (result.start, result.end));
    CHECK (result.score >= testCase.minScore);
    CHECK (result.score == Catch::Approx (scoreLoop (audio, rate, result.start, result.end, result.crossfade,
                                                     settings.crossfadeMode)).margin (0.01));
}

TEST_CASE ("Loop: Tonal trifft ganze Perioden")
{
    const auto audio = makeTone (staticTone);
    const auto result = findLoop (audio, rate, settingsFor (LoopAlgorithm::tonal), hintsFor (staticTone, 69));

    REQUIRE (result.found);
    CHECK (result.algorithm == LoopAlgorithm::tonal);

    const auto periods = (result.end - result.start) / (rate / 440.0);
    CHECK (std::abs (periods - std::round (periods)) < 0.05);
    CHECK (result.crossfade <= (int) std::ceil (2.0 * rate / 440.0));
    CHECK (result.score > 95.0f);
}

TEST_CASE ("Loop: Pad bevorzugt lange Loops, Crossfade über Pad Amount")
{
    auto settings = settingsFor (LoopAlgorithm::pad);
    settings.padAmount = 0.5f;

    const auto audio = makeTone (pad);
    const auto result = findLoop (audio, rate, settings, hintsFor (pad, 60));

    REQUIRE (result.found);
    CHECK (result.end - result.start > (int) (0.8 * rate));
    CHECK (result.crossfade >= juce::jmin (result.start, (result.end - result.start) / 4) - 1);
}

TEST_CASE ("Loop: Crossfade verbessert den Score bei Rauschen")
{
    juce::Random random (7);
    juce::AudioBuffer<float> noise (1, 96000);

    for (int i = 0; i < noise.getNumSamples(); ++i)
        noise.setSample (0, i, 0.3f * (random.nextFloat() * 2.0f - 1.0f));

    const auto hard = scoreLoop (noise, rate, 20000, 70000, 0, CrossfadeMode::equalPower);
    const auto smooth = scoreLoop (noise, rate, 20000, 70000, 9600, CrossfadeMode::equalPower);
    CHECK (smooth > hard + 20.0f);
}

//==============================================================================
TEST_CASE ("Loop: Session übernimmt Ergebnisse, Lock schützt")
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("AutosamplerLoopTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    const auto package = folder.getChildFile ("Loop.asession");

    SamplingSettings sampling;
    sampling.holdSeconds = lead.hold;

    Session session;
    REQUIRE (session.beginRecording (package, "Loop", {}, {}, sampling).isEmpty());

    const auto audio = makeTone (lead);
    const auto file = session.getSamplesFolder().getChildFile ("lead.wav");
    REQUIRE (writeWavFile (file, audio, rate, 24, 57).isEmpty());
    session.addRecordedSample ({ file, { 57, 127 }, rate, audio.getNumSamples(), audio.getNumChannels() });

    // Einlesen und Hinweise aus den Sampling-Einstellungen
    juce::AudioBuffer<float> loaded;
    double loadedRate = 0.0;
    REQUIRE (readWavFile (file, loaded, loadedRate).isEmpty());
    CHECK (juce::exactlyEqual (loadedRate, rate));
    CHECK (loaded.getNumSamples() == audio.getNumSamples());

    const auto hints = session.getLoopHints (session.getSample (0).getTree());
    CHECK (hints.note == 57);
    CHECK (hints.sustainEndFrame == juce::roundToInt ((lead.hold + 0.002) * rate));

    auto settings = settingsFor (LoopAlgorithm::universal);
    settings.crossfadeMode = CrossfadeMode::sCurve;
    session.setLoopSearchSettings (settings);
    CHECK (session.getLoopSearchSettings().algorithm == LoopAlgorithm::universal);
    CHECK (session.getLoopSearchSettings().crossfadeMode == CrossfadeMode::sCurve);

    const auto result = findLoop (loaded, loadedRate, settings, hints);
    REQUIRE (result.found);

    auto sample = session.getSample (0);
    CHECK_FALSE (sample.getLoopAlgorithm().has_value());
    CHECK (sample.getLoopScore() < 0.0f);

    sample.setLocked (true);
    CHECK_FALSE (sample.applyLoopResult (result, CrossfadeMode::sCurve));
    CHECK_FALSE (sample.isLoopEnabled());

    sample.setLocked (false);
    session.getUndoManager().beginNewTransaction ("Loop suchen");
    REQUIRE (sample.applyLoopResult (result, CrossfadeMode::sCurve));
    CHECK (sample.isLoopEnabled());
    CHECK (sample.getLoopStart() == result.start);
    CHECK (sample.getLoopEnd() == result.end);
    CHECK (sample.getCrossfade() == result.crossfade);
    CHECK (sample.getCrossfadeMode() == CrossfadeMode::sCurve);
    CHECK (sample.getLoopAlgorithm() == LoopAlgorithm::universal);
    CHECK (sample.getLoopScore() == Catch::Approx (result.score));

    // Crossfade wird auf den Loop begrenzt, auch nach dem Verschieben von S
    sample.setCrossfade (10000000);
    CHECK (sample.getCrossfade() == getMaxCrossfade (sample.getLoopStart(), sample.getLoopEnd()));
    sample.setLoopStart (100);
    CHECK (sample.getCrossfade() <= 100);

    // Speichern und Laden
    REQUIRE (session.save().isEmpty());
    Session reloaded;
    REQUIRE (reloaded.load (package).isEmpty());
    CHECK (reloaded.getSample (0).getLoopAlgorithm() == LoopAlgorithm::universal);
    CHECK (reloaded.getSample (0).getCrossfadeMode() == CrossfadeMode::sCurve);
    CHECK (reloaded.getLoopSearchSettings().crossfadeMode == CrossfadeMode::sCurve);

    // Ein Undo-Schritt nimmt das ganze Ergebnis zurück
    session.getUndoManager().undo();
    session.getUndoManager().undo();
    CHECK_FALSE (session.getSample (0).isLoopEnabled());
    CHECK_FALSE (session.getSample (0).getLoopAlgorithm().has_value());

    folder.deleteRecursively();
}
