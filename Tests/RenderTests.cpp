#include "TestSynth.h"
#include "engine/NoteRenderer.h"
#include "engine/SamplingJob.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace autosampler;
using autosampler::test::TestSynth;

namespace
{
    constexpr double sampleRate = 48000.0;

    NoteRenderSpec makeSpec (double holdSeconds, double maxTailSeconds)
    {
        NoteRenderSpec spec;
        spec.note = 69;
        spec.velocity = 127;
        spec.holdSamples = juce::roundToInt (holdSeconds * sampleRate);
        spec.maxTailSamples = juce::roundToInt (maxTailSeconds * sampleRate);
        spec.tailThresholdDb = -70.0f;
        return spec;
    }

    RenderedNote renderOffline (TestSynth& synth, const NoteRenderSpec& spec)
    {
        OfflineRenderer renderer (synth, sampleRate, 512);
        renderer.prepareNextNote (nullptr);

        juce::String error;
        auto result = renderer.render (spec, nullptr, error);
        REQUIRE (result.has_value());
        REQUIRE (error.isEmpty());
        return std::move (*result);
    }

    struct TempFolder
    {
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }

        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("AutosamplerTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    };
}

TEST_CASE ("Offline-Render: Note-On am Anfang, Ende nach dem Release")
{
    TestSynth synth;
    const auto result = renderOffline (synth, makeSpec (0.5, 5.0));

    CHECK_FALSE (result.hitMaxTail);
    CHECK (result.audio.getNumChannels() == 2);
    CHECK (findFirstSampleAbove (result.audio, 0.001f) < 5);

    // 0,5 s Halten + 0,25 s Release + 0,3 s Stille bis zur Erkennung (+ Block)
    const auto seconds = result.audio.getNumSamples() / sampleRate;
    CHECK (seconds > 0.5 + 0.25);
    CHECK (seconds < 0.5 + 0.25 + 0.3 + 0.05);
}

TEST_CASE ("Offline-Render zieht die Plugin-Latenz ab")
{
    TestSynth synth (1234);
    const auto result = renderOffline (synth, makeSpec (0.5, 5.0));

    const auto onset = findFirstSampleAbove (result.audio, 0.001f);
    CHECK (onset >= 0);
    CHECK (onset < 5);

    const auto seconds = result.audio.getNumSamples() / sampleRate;
    CHECK (seconds > 0.5 + 0.25);
    CHECK (seconds < 0.5 + 0.25 + 0.3 + 0.05);
}

TEST_CASE ("Offline-Render endet beim maximalen Tail")
{
    TestSynth synth (0, 10.0);
    const auto spec = makeSpec (0.2, 0.5);
    const auto result = renderOffline (synth, spec);

    CHECK (result.hitMaxTail);
    CHECK (result.audio.getNumSamples() == spec.holdSamples + spec.maxTailSamples);
}

TEST_CASE ("OfflineRenderer setzt den Realtime-Modus wieder zurück")
{
    TestSynth synth;

    {
        OfflineRenderer renderer (synth, sampleRate);
        CHECK (synth.isNonRealtime());
    }

    CHECK_FALSE (synth.isNonRealtime());
}

TEST_CASE ("Kompletter Lauf schreibt WAV-Dateien mit Metadaten")
{
    TempFolder temp;
    TestSynth synth (100);

    SamplingContext context { synth };
    context.settings.sampleRate = 44100.0;
    context.settings.bitDepth = 24;
    context.settings.holdSeconds = 0.3;
    context.settings.maxTailSeconds = 2.0;
    context.settings.renderMode = RenderMode::offline;
    context.requests = juce::Array<SampleRequest> { SampleRequest { 60, 64 }, SampleRequest { 60, 127 }, SampleRequest { 72, 64 }, SampleRequest { 72, 127 } };
    context.outputFolder = temp.folder;
    context.filePrefix = "Test";
    synth.getStateInformation (context.pluginState);

    juce::StringArray logLines;
    int started = 0;

    SamplingCallbacks callbacks;
    callbacks.onLog = [&] (const juce::String& line, LogLevel) { logLines.add (line); };
    callbacks.onSampleStarted = [&] (int, int, const SampleRequest&) { ++started; };

    const auto result = runSampling (context, callbacks);

    INFO (logLines.joinIntoString ("\n"));
    CHECK (result.error.isEmpty());
    CHECK_FALSE (result.aborted);
    CHECK (result.numWritten == 4);
    CHECK (result.numSilent == 0);
    CHECK (result.modeUsed == RenderMode::offline);
    CHECK (started == 4);
    CHECK (synth.numStateRestores == 4);

    const auto file = temp.folder.getChildFile ("Test_072_C4_v127.wav");
    REQUIRE (file.existsAsFile());
    CHECK (temp.folder.getNumberOfChildFiles (juce::File::findFiles, "*.wav") == 4);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    REQUIRE (reader != nullptr);
    CHECK (reader->sampleRate == Catch::Approx (44100.0));
    CHECK (reader->bitsPerSample == 24);
    CHECK (reader->numChannels == 2);
    CHECK (reader->metadataValues["MidiUnityNote"] == "72");

    const auto seconds = (double) reader->lengthInSamples / reader->sampleRate;
    CHECK (seconds > 0.3 + 0.2);
    CHECK (seconds < 0.3 + 0.25 + 0.05);

    // Velocity 127 -> Amplitude 0,5
    juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
    CHECK (audio.getMagnitude (0, audio.getNumSamples()) == Catch::Approx (0.5f).margin (0.01f));
}

TEST_CASE ("32-Bit-Float und Lauf ohne State-Wiederherstellung")
{
    TempFolder temp;
    TestSynth synth;

    SamplingContext context { synth };
    context.settings.bitDepth = 32;
    context.settings.holdSeconds = 0.1;
    context.settings.restoreStatePerSample = false;
    context.requests = juce::Array<SampleRequest> { SampleRequest { 60, 100 } };
    context.outputFolder = temp.folder;
    synth.getStateInformation (context.pluginState);

    const auto result = runSampling (context, {});
    CHECK (result.numWritten == 1);
    CHECK (synth.numStateRestores == 0);

    juce::WavAudioFormat wav;
    const auto file = temp.folder.getChildFile ("060_C3_v100.wav");
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    REQUIRE (reader != nullptr);
    CHECK (reader->bitsPerSample == 32);
    CHECK (reader->usesFloatingPointData);
}

TEST_CASE ("Stille Samples werden übersprungen")
{
    TempFolder temp;
    TestSynth synth (0, 0.25, true);    // offline stumm, kein Audio-Gerät für Echtzeit

    SamplingContext context { synth };
    context.settings.holdSeconds = 0.1;
    context.settings.renderMode = RenderMode::automatic;
    context.requests = juce::Array<SampleRequest> { SampleRequest { 60, 100 }, SampleRequest { 62, 100 } };
    context.outputFolder = temp.folder;

    const auto result = runSampling (context, {});
    CHECK (result.numWritten == 0);
    CHECK (result.numSilent == 2);
    CHECK (result.error.isEmpty());
    CHECK (temp.folder.getNumberOfChildFiles (juce::File::findFiles) == 0);
}

TEST_CASE ("Abbruch nach dem ersten Sample")
{
    TempFolder temp;
    TestSynth synth;

    SamplingContext context { synth };
    context.settings.holdSeconds = 0.1;
    context.requests = juce::Array<SampleRequest> { SampleRequest { 60, 100 }, SampleRequest { 62, 100 }, SampleRequest { 64, 100 } };
    context.outputFolder = temp.folder;

    int started = 0;
    SamplingCallbacks callbacks;
    callbacks.onSampleStarted = [&] (int, int, const SampleRequest&) { ++started; };
    callbacks.shouldAbort = [&] { return started >= 2; };

    const auto result = runSampling (context, callbacks);
    CHECK (result.aborted);
    CHECK (result.numWritten == 1);
}

TEST_CASE ("Echtzeit ohne Audio-Gerät liefert einen Fehler")
{
    TestSynth synth;

    SamplingContext context { synth };
    context.settings.renderMode = RenderMode::realtime;
    context.requests = juce::Array<SampleRequest> { SampleRequest { 60, 100 } };
    context.outputFolder = juce::File::getSpecialLocation (juce::File::tempDirectory);

    const auto result = runSampling (context, {});
    CHECK (result.error.isNotEmpty());
    CHECK (result.numWritten == 0);
}
