#include "Text.h"
#include "engine/Exporter.h"
#include "engine/SampleWriter.h"
#include "engine/SamplerLoader.h"
#include "engine/Session.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

using namespace autosampler;
using Catch::Approx;

namespace
{
    struct TempFolder
    {
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }

        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("AutosamplerExportTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    };

    /** Golden Files liegen in Tests/Golden. Mit AUTOSAMPLER_UPDATE_GOLDEN=1 werden sie neu geschrieben. */
    void checkGolden (const juce::String& actual, const juce::String& goldenName)
    {
        const auto golden = juce::File (AUTOSAMPLER_GOLDEN_DIR).getChildFile (goldenName);

        if (juce::SystemStats::getEnvironmentVariable ("AUTOSAMPLER_UPDATE_GOLDEN", {}) == "1")
            REQUIRE (golden.replaceWithText (actual, false, false, "\n"));

        REQUIRE (golden.existsAsFile());
        CHECK (actual == golden.loadFileAsString());
    }

    /** Sinus mit Note und Pegel, damit sich die Samples unterscheiden lassen. */
    juce::AudioBuffer<float> makeTone (int numFrames, double sampleRate, float frequency, float level, int numChannels = 2)
    {
        juce::AudioBuffer<float> audio (numChannels, numFrames);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numFrames; ++i)
                audio.setSample (ch, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / sampleRate)
                                            * (ch == 0 ? 1.0f : 0.5f));

        return audio;
    }

    SamplerZoneSource writeSource (const juce::File& folder, const juce::String& fileName, int note, int velocity,
                                   double sampleRate = 48000.0, float level = 0.25f, int numFrames = 4800)
    {
        SamplerZoneSource source;
        source.file = folder.getChildFile (fileName);
        source.note = note;
        source.velocity = velocity;
        REQUIRE (writeWavFile (source.file, makeTone (numFrames, sampleRate, 440.0f, level), sampleRate, 24, note).isEmpty());
        return source;
    }

    /** Zwei Noten × zwei Velocities, ein Loop und ein Round-Robin-Paar (C3, 127). */
    std::vector<SamplerZoneSource> makeInstrument (const juce::File& folder, double sampleRate = 48000.0)
    {
        std::vector<SamplerZoneSource> sources;
        sources.push_back (writeSource (folder, "a.wav", 60, 127, sampleRate, 0.5f));
        sources.push_back (writeSource (folder, "b.wav", 48, 64, sampleRate, 0.1f));
        sources.push_back (writeSource (folder, "c.wav", 48, 127, sampleRate, 0.3f));
        sources.push_back (writeSource (folder, "d.wav", 60, 64, sampleRate, 0.2f));
        sources.push_back (writeSource (folder, "e.wav", 60, 127, sampleRate, 0.4f));

        sources[0].loop = true;
        sources[0].loopStart = 1000;
        sources[0].loopEnd = 3000;
        sources[0].crossfade = 200;
        sources[0].crossfadeMode = CrossfadeMode::linear;
        return sources;
    }

    std::unique_ptr<juce::AudioFormatReader> openWav (const juce::File& file)
    {
        juce::WavAudioFormat wav;
        return std::unique_ptr<juce::AudioFormatReader> (wav.createReaderFor (file.createInputStream().release(), true));
    }
}

TEST_CASE ("Export: WAV-Metadaten (smpl und inst)")
{
    TempFolder temp;
    const auto file = temp.folder.getChildFile ("meta.wav");

    WavMetadata metadata;
    metadata.rootNote = 62;
    metadata.loop = juce::Range<int> (1000, 3000);
    metadata.zone = ZoneRange { 55, 66, 65, 127 };
    REQUIRE (writeWavFile (file, makeTone (4800, 48000.0, 440.0f, 0.5f), 48000.0, 24, metadata).isEmpty());

    auto reader = openWav (file);
    REQUIRE (reader != nullptr);
    const auto& values = reader->metadataValues;

    CHECK (values["MidiUnityNote"] == "62");
    CHECK (values["NumSampleLoops"] == "1");
    CHECK (values["Loop0Start"] == "1000");
    CHECK (values["Loop0End"] == "2999");      // inklusiv
    CHECK (values["SamplePeriod"] == "20833");
    CHECK (values["LowNote"] == "55");
    CHECK (values["HighNote"] == "66");
    CHECK (values["LowVelocity"] == "65");
    CHECK (values["HighVelocity"] == "127");
    CHECK (reader->bitsPerSample == 24);

    SECTION ("ohne Loop und ohne smpl-Chunk (Tonverk)")
    {
        WavMetadata plain;
        plain.sampleChunkWithoutLoop = false;
        REQUIRE (writeWavFile (file, makeTone (480, 48000.0, 440.0f, 0.5f), 48000.0, 16, plain).isEmpty());

        auto plainReader = openWav (file);
        REQUIRE (plainReader != nullptr);
        CHECK (plainReader->metadataValues.getValue ("MidiUnityNote", {}).isEmpty());
        CHECK (plainReader->metadataValues.getValue ("LowNote", {}).isEmpty());
    }
}

TEST_CASE ("Export: Plan mit Bereichen, Velocity-Layern und Round Robin")
{
    TempFolder temp;
    const auto plan = planExport (makeInstrument (temp.folder), ExportTarget::sfz, "Pad");
    REQUIRE (plan.size() == 5);

    // Sortiert nach Note und Velocity, Round Robin in der Reihenfolge der Session
    CHECK (plan[0].fileName == "Pad_048_C2_v064.wav");
    CHECK (plan[1].fileName == "Pad_048_C2_v127.wav");
    CHECK (plan[2].fileName == "Pad_060_C3_v064.wav");
    CHECK (plan[3].fileName == "Pad_060_C3_v127_rr1.wav");
    CHECK (plan[4].fileName == "Pad_060_C3_v127_rr2.wav");
    CHECK (plan[3].source.file.getFileName() == "a.wav");
    CHECK (plan[4].source.file.getFileName() == "e.wav");

    CHECK (plan[0].range == ZoneRange { 0, 54, 1, 64 });
    CHECK (plan[1].range == ZoneRange { 0, 54, 65, 127 });
    CHECK (plan[3].range == ZoneRange { 55, 127, 65, 127 });
    CHECK (plan[3].roundRobinCount == 2);
    CHECK (plan[4].roundRobinIndex == 1);
    CHECK (plan[2].velocityLayer == 0);
    CHECK (plan[4].velocityLayer == 1);

    const auto tonverk = planExport (makeInstrument (temp.folder), ExportTarget::tonverk, "Pad");
    CHECK (tonverk[0].fileName == "Pad-000-048-c2.wav");
    CHECK (tonverk[3].fileName == "Pad-001-060-c3.wav");
    CHECK (tonverk[4].fileName == "Pad-001-060-c3-rr1.wav");
    CHECK (makeExportFileName (ExportTarget::tonverk, "X", 61, 100, 2, 0, 1) == "X-002-061-c#3.wav");
}

TEST_CASE ("Export: Aufbereitung (Crossfade, Tail, Loop Only, Mono, Rate)")
{
    TempFolder temp;
    auto source = writeSource (temp.folder, "s.wav", 60, 100);
    source.loop = true;
    source.loopStart = 1000;
    source.loopEnd = 3000;
    source.crossfade = 300;
    source.crossfadeMode = CrossfadeMode::equalPower;

    juce::AudioBuffer<float> expected;
    double rate = 0.0;
    REQUIRE (readWavFile (source.file, expected, rate).isEmpty());
    applyLoopCrossfade (expected, 1000, 3000, 300, CrossfadeMode::equalPower, rate);

    ExportSettings settings;
    PreparedAudio prepared;

    SECTION ("Original: ganzes Sample mit gerendertem Crossfade")
    {
        REQUIRE (prepareExportAudio (source, settings, prepared).isEmpty());
        CHECK (prepared.audio.getNumSamples() == 4800);
        CHECK (prepared.loop == juce::Range<int> (1000, 3000));

        for (int i = 2600; i < 3000; ++i)
            CHECK (prepared.audio.getSample (0, i) == Approx (expected.getSample (0, i)).margin (1.0e-6));
    }

    SECTION ("Bei Loop-Ende + 32 abschneiden")
    {
        settings.tail = TailMode::cutAfterLoop;
        REQUIRE (prepareExportAudio (source, settings, prepared).isEmpty());
        CHECK (prepared.audio.getNumSamples() == 3032);
    }

    SECTION ("Nur der Loop")
    {
        settings.loopOnly = true;
        REQUIRE (prepareExportAudio (source, settings, prepared).isEmpty());
        CHECK (prepared.audio.getNumSamples() == 2000);
        CHECK (prepared.loop == juce::Range<int> (0, 2000));
        CHECK (prepared.audio.getSample (0, 1999) == Approx (expected.getSample (0, 2999)).margin (1.0e-6));
    }

    SECTION ("Mono")
    {
        settings.mono = true;
        REQUIRE (prepareExportAudio (source, settings, prepared).isEmpty());
        CHECK (prepared.audio.getNumChannels() == 1);
        CHECK (prepared.audio.getSample (0, 100) == Approx ((expected.getSample (0, 100) + expected.getSample (1, 100)) * 0.5f).margin (1.0e-6));
    }

    SECTION ("Sample-Rate: Loop-Länge einmal gerundet")
    {
        settings.sampleRate = 44100.0;
        REQUIRE (prepareExportAudio (source, settings, prepared).isEmpty());
        CHECK (juce::exactlyEqual (prepared.sampleRate, 44100.0));
        CHECK (prepared.audio.getNumSamples() == Approx (4410).margin (2));
        REQUIRE (prepared.loop.has_value());
        CHECK (prepared.loop->getLength() == 1838);    // 2000 · 44100 / 48000 = 1837,5
        CHECK (prepared.loop->getStart() == 919);
    }

    SECTION ("Fehlende Datei")
    {
        source.file = temp.folder.getChildFile ("fehlt.wav");
        CHECK (prepareExportAudio (source, settings, prepared).isNotEmpty());
    }
}

TEST_CASE ("Export: SFZ (Golden File)")
{
    TempFolder temp;
    const auto input = temp.folder.getChildFile ("in");
    input.createDirectory();

    ExportSettings settings;
    settings.target = ExportTarget::sfz;
    settings.attackMs = 5.0f;
    settings.releaseMs = 250.0f;

    const auto result = runExport (makeInstrument (input), settings, "Soft Pad", temp.folder.getChildFile ("out"));
    REQUIRE (result.error.isEmpty());
    CHECK (result.numWritten == 5);
    CHECK (result.output.getFileName() == "Soft Pad");

    const auto sfz = result.output.getChildFile ("Soft Pad.sfz");
    REQUIRE (sfz.existsAsFile());
    checkGolden (sfz.loadFileAsString(), "SoftPad.sfz");

    const auto looped = result.output.getChildFile ("samples/Soft_Pad_060_C3_v127_rr1.wav");
    REQUIRE (looped.existsAsFile());
    auto reader = openWav (looped);
    REQUIRE (reader != nullptr);
    CHECK (reader->metadataValues["Loop0Start"] == "1000");
    CHECK (reader->metadataValues["Loop0End"] == "2999");
    CHECK (reader->metadataValues["LowNote"] == "55");

    // Ein zweiter Export legt einen neuen Ordner an
    const auto second = runExport (makeInstrument (input), settings, "Soft Pad", temp.folder.getChildFile ("out"));
    CHECK (second.output.getFileName() == "Soft Pad 2");
}

TEST_CASE ("Export: Elektron Tonverk (Golden File)")
{
    TempFolder temp;
    const auto input = temp.folder.getChildFile ("in");
    input.createDirectory();

    ExportSettings settings;
    settings.target = ExportTarget::tonverk;
    settings.bitDepth = 32;

    // 44,1 kHz wird auf 48 kHz umgerechnet, 32 Bit Float auf 24 Bit
    const auto result = runExport (makeInstrument (input, 44100.0), settings, "Pad", temp.folder);
    REQUIRE (result.error.isEmpty());
    CHECK (result.warnings.contains ("Tonverk: 24 bit instead of 32 bit float"));

    const auto elmulti = result.output.getChildFile ("Pad.elmulti");
    REQUIRE (elmulti.existsAsFile());
    checkGolden (elmulti.loadFileAsString(), "Pad.elmulti");

    auto looped = openWav (result.output.getChildFile ("Pad-001-060-c3.wav"));
    REQUIRE (looped != nullptr);
    CHECK (juce::exactlyEqual (looped->sampleRate, 48000.0));
    CHECK (looped->bitsPerSample == 24);
    CHECK (looped->metadataValues["Loop0Start"] == "1088");
    CHECK (looped->metadataValues.getValue ("LowNote", {}).isEmpty());

    auto plain = openWav (result.output.getChildFile ("Pad-000-048-c2.wav"));
    REQUIRE (plain != nullptr);
    CHECK (plain->metadataValues.getValue ("MidiUnityNote", {}).isEmpty());
}

TEST_CASE ("Export: Normalisieren mit gemeinsamer Verstärkung")
{
    TempFolder temp;
    std::vector<SamplerZoneSource> sources { writeSource (temp.folder, "laut.wav", 60, 127, 48000.0, 0.5f),
                                             writeSource (temp.folder, "leise.wav", 60, 64, 48000.0, 0.1f) };

    ExportSettings settings;
    settings.target = ExportTarget::wav;
    settings.normalize = true;
    settings.peakDb = -1.0f;
    settings.bitDepth = 32;
    settings.mono = true;

    const auto result = runExport (sources, settings, "N", temp.folder.getChildFile ("out"));
    REQUIRE (result.error.isEmpty());

    juce::AudioBuffer<float> loud, quiet;
    double rate = 0.0;
    REQUIRE (readWavFile (result.output.getChildFile ("N_060_C3_v127.wav"), loud, rate).isEmpty());
    REQUIRE (readWavFile (result.output.getChildFile ("N_060_C3_v064.wav"), quiet, rate).isEmpty());

    const auto loudPeak = loud.getMagnitude (0, loud.getNumSamples());
    CHECK (juce::Decibels::gainToDecibels (loudPeak) == Approx (-1.0).margin (0.01));
    CHECK (quiet.getMagnitude (0, quiet.getNumSamples()) / loudPeak == Approx (0.2).margin (0.001));
}

TEST_CASE ("Export: Auswahl, Zip und Abbruch")
{
    TempFolder temp;
    const auto package = temp.folder.getChildFile ("Zip.asession");

    Session session;
    SamplingSettings sampling;
    REQUIRE (session.beginRecording (package, "Zip", {}, {}, sampling).isEmpty());

    for (const auto velocity : { 64, 127 })
    {
        const SampleRequest request { 60, velocity };
        const auto file = session.getSamplesFolder().getChildFile (makeSampleFileName ("Zip", request));
        REQUIRE (writeWavFile (file, makeTone (2400, 48000.0, 220.0f, 0.3f), 48000.0, 24, 60).isEmpty());
        session.addRecordedSample ({ file, request, 48000.0, 2400, 2 });
    }

    session.getSample (0).setExported (false);
    CHECK_FALSE (session.getSample (0).isExported());
    CHECK (session.getUndoManager().canUndo());

    const auto sources = getExportSources (session);
    REQUIRE (sources.size() == 1);
    CHECK (sources[0].velocity == 127);

    ExportSettings settings;
    settings.target = ExportTarget::sfz;
    settings.zip = true;

    const auto result = runExport (sources, settings, "Zip", temp.folder.getChildFile ("out"));
    REQUIRE (result.error.isEmpty());
    CHECK (result.output.getFileName() == "Zip.zip");

    juce::ZipFile zip (result.output);
    CHECK (zip.getNumEntries() == 2);
    CHECK (zip.getEntry ("Zip/Zip.sfz") != nullptr);
    CHECK (zip.getEntry ("Zip/samples/Zip_060_C3_v127.wav") != nullptr);
    CHECK_FALSE (temp.folder.getChildFile ("out/Zip").exists());

    SECTION ("Abbruch löscht die Ausgabe")
    {
        settings.zip = false;
        ExportCallbacks callbacks;
        callbacks.shouldCancel = [] { return true; };

        const auto cancelled = runExport (sources, settings, "Abbruch", temp.folder.getChildFile ("out"), callbacks);
        CHECK (cancelled.cancelled);
        CHECK_FALSE (temp.folder.getChildFile ("out/Abbruch").exists());
    }

    SECTION ("Keine Samples")
    {
        CHECK (runExport ({}, settings, "Leer", temp.folder).error.isNotEmpty());
    }
}

TEST_CASE ("Export: Einstellungen als ValueTree und in der Session")
{
    ExportSettings settings;
    settings.target = ExportTarget::tonverk;
    settings.name = "Mein Pad";
    settings.sampleRate = 44100.0;
    settings.bitDepth = 16;
    settings.normalize = true;
    settings.peakDb = -3.0f;
    settings.mono = true;
    settings.tail = TailMode::cutAfterLoop;
    settings.loopOnly = true;
    settings.loopOnRelease = false;
    settings.oneShotWithoutLoop = false;
    settings.attackMs = 10.0f;
    settings.releaseMs = 500.0f;
    settings.velocitySensitivity = 0.8f;
    settings.zip = true;

    const auto restored = ExportSettings::fromValueTree (settings.toValueTree());
    CHECK (restored.target == ExportTarget::tonverk);
    CHECK (restored.name == "Mein Pad");
    CHECK (juce::exactlyEqual (restored.sampleRate, 44100.0));
    CHECK (restored.bitDepth == 16);
    CHECK (restored.normalize);
    CHECK (juce::exactlyEqual (restored.peakDb, -3.0f));
    CHECK (restored.mono);
    CHECK (restored.tail == TailMode::cutAfterLoop);
    CHECK (restored.loopOnly);
    CHECK_FALSE (restored.loopOnRelease);
    CHECK_FALSE (restored.oneShotWithoutLoop);
    CHECK (juce::exactlyEqual (restored.attackMs, 10.0f));
    CHECK (juce::exactlyEqual (restored.releaseMs, 500.0f));
    CHECK (restored.velocitySensitivity == Approx (0.8f));
    CHECK (restored.zip);

    // Ungültige Werte fallen auf Standardwerte zurück
    juce::ValueTree broken (ExportSettings::getValueTreeType());
    broken.setProperty ("bitDepth", 12, nullptr);
    broken.setProperty ("target", "sf2", nullptr);
    CHECK (ExportSettings::fromValueTree (broken).bitDepth == 24);
    CHECK (ExportSettings::fromValueTree (broken).target == ExportTarget::tonverk);

    Session session;
    session.setExportSettings (settings);
    CHECK (session.getExportSettings().name == "Mein Pad");
    CHECK_FALSE (session.getUndoManager().canUndo());
}
