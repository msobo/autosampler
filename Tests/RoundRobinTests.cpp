#include "TestSynth.h"
#include "engine/AudioAnalysis.h"
#include "engine/Exporter.h"
#include "engine/SampleWriter.h"
#include "engine/SamplingJob.h"
#include "engine/Session.h"
#include "engine/ZoneSmoothing.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace autosampler;
using autosampler::test::TestSynth;
using Catch::Approx;

namespace
{
    struct TempFolder
    {
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }

        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("AutosamplerRoundRobinTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    };

    SamplingSettings makeSettings (const juce::String& notes, const juce::String& velocities)
    {
        SamplingSettings settings;
        settings.useCustomNotes = true;
        settings.customNotes = notes;
        settings.velocities = velocities;
        settings.holdSeconds = 0.1;
        settings.maxTailSeconds = 1.0;
        settings.renderMode = RenderMode::offline;
        return settings;
    }

    /** Führt einen Lauf mit den Einstellungen aus; requests aus buildSampleList(). */
    SamplingResult run (TestSynth& synth, const SamplingSettings& settings, const juce::File& folder,
                        juce::StringArray* logLines = nullptr)
    {
        juce::String error;

        SamplingContext context { synth };
        context.settings = settings;
        context.requests = buildSampleList (settings, error);
        context.outputFolder = folder;
        context.filePrefix = "RR";
        synth.getStateInformation (context.pluginState);
        REQUIRE (error.isEmpty());

        SamplingCallbacks callbacks;
        callbacks.onLog = [logLines] (const juce::String& line, LogLevel)
        {
            if (logLines != nullptr)
                logLines->add (line);
        };

        return runSampling (context, callbacks);
    }

    juce::AudioBuffer<float> readAudio (const juce::File& file)
    {
        juce::AudioBuffer<float> audio;
        double rate = 0.0;
        REQUIRE (readWavFile (file, audio, rate).isEmpty());
        return audio;
    }

    float peakOf (const juce::File& file)
    {
        const auto audio = readAudio (file);
        return audio.getMagnitude (0, audio.getNumSamples());
    }

    juce::AudioBuffer<float> makeTone (int numFrames, float level, int numChannels = 2)
    {
        juce::AudioBuffer<float> audio (numChannels, numFrames);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numFrames; ++i)
                audio.setSample (ch, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 48000.0));

        return audio;
    }

    SamplerZoneSource writeSource (const juce::File& folder, const juce::String& fileName, int note, int velocity,
                                   float level, int roundRobin = 0)
    {
        SamplerZoneSource source;
        source.file = folder.getChildFile (fileName);
        source.note = note;
        source.velocity = velocity;
        source.roundRobin = roundRobin;
        REQUIRE (writeWavFile (source.file, makeTone (9600, level), 48000.0, 24, note).isEmpty());
        return source;
    }
}

//==============================================================================
TEST_CASE ("Round Robin: Sample-Liste je Modus")
{
    auto settings = makeSettings ("C3, C4", "64, 127");

    SECTION ("Ohne Round Robin")
    {
        juce::String error;
        const auto requests = buildSampleList (settings, error);
        CHECK (requests.size() == 4);
        CHECK (requests[0].roundRobinCount == 1);
        CHECK (requests[0].variation == -1);
    }

    SECTION ("Real RR: Noten × Velocities × Varianten, Varianten hintereinander")
    {
        settings.roundRobinSource = RoundRobinSource::real;
        settings.roundRobinSteps = 3;
        settings.roundRobinMode = RoundRobinMode::linked;   // gilt nur für MIDI CC

        juce::String error;
        const auto requests = buildSampleList (settings, error);
        REQUIRE (requests.size() == 12);
        CHECK (requests[0] == SampleRequest { 60, 64, 0, 3, -1 });
        CHECK (requests[1] == SampleRequest { 60, 64, 1, 3, -1 });
        CHECK (requests[2] == SampleRequest { 60, 64, 2, 3, -1 });
        CHECK (requests[3] == SampleRequest { 60, 127, 0, 3, -1 });
        CHECK (requests[11] == SampleRequest { 72, 127, 2, 3, -1 });
    }

    SECTION ("MIDI CC Independent: Werte gleichmäßig von bis")
    {
        settings.roundRobinSource = RoundRobinSource::midiCC;
        settings.roundRobinSteps = 3;
        settings.variationFrom = 32;
        settings.variationTo = 45;

        juce::String error;
        const auto requests = buildSampleList (settings, error);
        REQUIRE (requests.size() == 12);
        CHECK (requests[0].variation == 32);
        CHECK (requests[1].variation == 39);
        CHECK (requests[2].variation == 45);
        CHECK (requests[3].variation == 32);
        CHECK (getVariationValues (45, 32, 2) == juce::Array<int> { 45, 32 });
        CHECK (getVariationValues (10, 20, 1) == juce::Array<int> { 10 });
    }

    SECTION ("MIDI CC Linked: Variante n gehört zu Velocity-Layer n")
    {
        settings.roundRobinSource = RoundRobinSource::midiCC;
        settings.roundRobinMode = RoundRobinMode::linked;
        settings.roundRobinSteps = 7;   // wird ignoriert
        settings.velocities = "90, 95, 100, 110";
        settings.variationFrom = 30;
        settings.variationTo = 60;

        juce::String error;
        const auto requests = buildSampleList (settings, error);
        REQUIRE (requests.size() == 8);
        CHECK (getRoundRobinCount (settings) == 1);

        for (int i = 0; i < 4; ++i)
        {
            CHECK (requests[i].roundRobinCount == 1);
            CHECK (requests[i].variation == 30 + 10 * i);
        }

        CHECK (makeSampleFileName ("Pad", requests[1]) == "Pad_060_C3_v095.wav");
    }

    SECTION ("Fehler")
    {
        settings.roundRobinSource = RoundRobinSource::midiCC;
        settings.roundRobinMode = RoundRobinMode::linked;
        settings.velocities = "127";

        juce::String error;
        CHECK (buildSampleList (settings, error).isEmpty());
        CHECK (error.isNotEmpty());

        settings.roundRobinMode = RoundRobinMode::independent;
        settings.variationTarget = VariationTarget::parameter;
        error = {};
        CHECK (buildSampleList (settings, error).isEmpty());
        CHECK (error.contains ("parameter"));
    }
}

TEST_CASE ("Round Robin: Dateinamen und Einstellungen als ValueTree")
{
    CHECK (makeSampleFileName ("Pad", { 60, 100, 1, 3, -1 }) == "Pad_060_C3_v100_rr2.wav");
    CHECK (makeSampleFileName ("Pad", { 60, 100, 0, 1, 40 }) == "Pad_060_C3_v100.wav");

    SamplingSettings s;
    s.roundRobinSource = RoundRobinSource::midiCC;
    s.roundRobinMode = RoundRobinMode::linked;
    s.roundRobinSteps = 5;
    s.variationTarget = VariationTarget::parameter;
    s.variationController = 1;
    s.variationParameterId = "cutoff";
    s.variationParameterName = "Cutoff";
    s.variationFrom = 10;
    s.variationTo = 20;
    s.gainDb = -3.5f;
    s.pan = 0.25f;

    const auto r = SamplingSettings::fromValueTree (s.toValueTree());
    CHECK (r.roundRobinSource == RoundRobinSource::midiCC);
    CHECK (r.roundRobinMode == RoundRobinMode::linked);
    CHECK (r.roundRobinSteps == 5);
    CHECK (r.variationTarget == VariationTarget::parameter);
    CHECK (r.variationController == 1);
    CHECK (r.variationParameterId == "cutoff");
    CHECK (r.variationParameterName == "Cutoff");
    CHECK (r.variationFrom == 10);
    CHECK (r.variationTo == 20);
    CHECK (r.gainDb == Approx (-3.5f));
    CHECK (r.pan == Approx (0.25f));

    // Alte Einstellungen ohne Round Robin
    const auto old = SamplingSettings::fromValueTree (juce::ValueTree (SamplingSettings::getValueTreeType()));
    CHECK (old.roundRobinSource == RoundRobinSource::off);
    CHECK (old.gainDb == 0.0f);

    CHECK (parseRoundRobinSource ("cc") == RoundRobinSource::midiCC);
    CHECK (parseRoundRobinSource ("Real") == RoundRobinSource::real);
    CHECK_FALSE (parseRoundRobinSource ("xyz").has_value());
}

//==============================================================================
TEST_CASE ("Round Robin: Real RR nimmt Varianten ohne State und reset dazwischen auf")
{
    TempFolder temp;
    TestSynth synth;
    synth.roundRobin = true;

    auto settings = makeSettings ("C3", "127");
    settings.roundRobinSource = RoundRobinSource::real;
    settings.roundRobinSteps = 3;

    juce::StringArray log;
    const auto result = run (synth, settings, temp.folder, &log);
    INFO (log.joinIntoString ("\n"));

    CHECK (result.error.isEmpty());
    CHECK (result.numWritten == 3);
    CHECK (result.numIdenticalGroups == 0);
    CHECK (synth.numStateRestores == 1);

    CHECK (peakOf (temp.folder.getChildFile ("RR_060_C3_v127_rr1.wav")) == Approx (0.5f).margin (0.005f));
    CHECK (peakOf (temp.folder.getChildFile ("RR_060_C3_v127_rr2.wav")) == Approx (0.45f).margin (0.005f));
    CHECK (peakOf (temp.folder.getChildFile ("RR_060_C3_v127_rr3.wav")) == Approx (0.4f).margin (0.005f));
}

TEST_CASE ("Round Robin: Real RR erkennt Instrumente ohne eigenes Round Robin")
{
    TempFolder temp;
    TestSynth synth;

    auto settings = makeSettings ("C3, D3", "127");
    settings.roundRobinSource = RoundRobinSource::real;
    settings.roundRobinSteps = 2;

    const auto result = run (synth, settings, temp.folder);
    CHECK (result.numWritten == 4);
    CHECK (result.numIdenticalGroups == 2);
}

TEST_CASE ("Round Robin: MIDI CC vor dem Note-On")
{
    TempFolder temp;
    TestSynth synth;

    auto settings = makeSettings ("C3", "127");
    settings.roundRobinSource = RoundRobinSource::midiCC;
    settings.roundRobinSteps = 2;
    settings.variationController = 7;
    settings.variationFrom = 64;
    settings.variationTo = 127;

    const auto result = run (synth, settings, temp.folder);
    CHECK (result.numWritten == 2);
    CHECK (peakOf (temp.folder.getChildFile ("RR_060_C3_v127_rr1.wav")) == Approx (0.5f * 64.0f / 127.0f).margin (0.005f));
    CHECK (peakOf (temp.folder.getChildFile ("RR_060_C3_v127_rr2.wav")) == Approx (0.5f).margin (0.005f));
}

TEST_CASE ("Round Robin: Linked bindet Werte an Velocity-Layer, ohne rr in den Namen")
{
    TempFolder temp;
    TestSynth synth;

    auto settings = makeSettings ("C3", "100, 127");
    settings.roundRobinSource = RoundRobinSource::midiCC;
    settings.roundRobinMode = RoundRobinMode::linked;
    settings.variationController = 7;
    settings.variationFrom = 127;
    settings.variationTo = 64;

    const auto result = run (synth, settings, temp.folder);
    CHECK (result.numWritten == 2);
    CHECK (peakOf (temp.folder.getChildFile ("RR_060_C3_v100.wav")) == Approx (0.5f * 100.0f / 127.0f).margin (0.005f));
    CHECK (peakOf (temp.folder.getChildFile ("RR_060_C3_v127.wav")) == Approx (0.5f * 64.0f / 127.0f).margin (0.005f));
}

TEST_CASE ("Round Robin: Plugin-Parameter wird variiert und danach zurückgesetzt")
{
    TempFolder temp;
    TestSynth synth;
    synth.levelParameter->setValueNotifyingHost (0.8f);

    REQUIRE (findParameter (synth, "level") == synth.levelParameter);
    CHECK (getParameterIdentifier (*synth.levelParameter) == "level");
    CHECK (findParameter (synth, "gibt-es-nicht") == nullptr);

    auto settings = makeSettings ("C3", "127");
    settings.roundRobinSource = RoundRobinSource::midiCC;
    settings.roundRobinSteps = 2;
    settings.variationTarget = VariationTarget::parameter;
    settings.variationParameterId = "level";
    settings.variationFrom = 127;
    settings.variationTo = 64;

    const auto result = run (synth, settings, temp.folder);
    CHECK (result.error.isEmpty());
    CHECK (result.numWritten == 2);
    CHECK (peakOf (temp.folder.getChildFile ("RR_060_C3_v127_rr1.wav")) == Approx (0.5f).margin (0.005f));
    CHECK (peakOf (temp.folder.getChildFile ("RR_060_C3_v127_rr2.wav")) == Approx (0.5f * 64.0f / 127.0f).margin (0.005f));
    CHECK (synth.levelParameter->get() == Approx (0.8f));

    settings.variationParameterId = "gibt-es-nicht";
    CHECK (run (synth, settings, temp.folder).error.isNotEmpty());
}

TEST_CASE ("Round Robin: Gain und Pan wirken auf die Aufnahme")
{
    TempFolder temp;
    TestSynth synth;

    auto settings = makeSettings ("C3", "127");
    settings.gainDb = -6.0206f;
    settings.pan = 1.0f;

    const auto result = run (synth, settings, temp.folder);
    REQUIRE (result.numWritten == 1);
    CHECK (result.numClipped == 0);

    const auto audio = readAudio (temp.folder.getChildFile ("RR_060_C3_v127.wav"));
    CHECK (audio.getMagnitude (0, 0, audio.getNumSamples()) == Approx (0.0f).margin (1.0e-6f));
    CHECK (audio.getMagnitude (1, 0, audio.getNumSamples()) == Approx (0.25f).margin (0.005f));

    // +12 dB übersteuert bei 24 Bit
    settings.gainDb = 12.0f;
    settings.pan = 0.0f;
    CHECK (run (synth, settings, temp.folder).numClipped == 1);
}

TEST_CASE ("Analyse: Gain/Pan, Pegel und Unterschied")
{
    SECTION ("Mono wird beim Panorama stereo (Constant Power)")
    {
        auto audio = makeTone (4800, 0.5f, 1);
        applyGainAndPan (audio, 0.0f, 0.0f);
        CHECK (audio.getNumChannels() == 1);

        applyGainAndPan (audio, 0.0f, -0.5f);
        REQUIRE (audio.getNumChannels() == 2);
        const auto left = audio.getMagnitude (0, 0, audio.getNumSamples());
        const auto right = audio.getMagnitude (1, 0, audio.getNumSamples());
        CHECK (left * left + right * right == Approx (0.25f).margin (0.001f));
        CHECK (left > right);
    }

    SECTION ("Stereo über die Balance")
    {
        auto audio = makeTone (4800, 0.5f);
        applyGainAndPan (audio, 6.0206f, -0.5f);
        CHECK (audio.getMagnitude (0, 0, audio.getNumSamples()) == Approx (1.0f).margin (0.002f));
        CHECK (audio.getMagnitude (1, 0, audio.getNumSamples()) == Approx (0.5f).margin (0.002f));
    }

    SECTION ("Pegel und Unterschied")
    {
        const auto tone = makeTone (48000, 0.5f);
        CHECK (measureLevelDb (tone, 48000.0) == Approx (-9.03f).margin (0.05f));
        CHECK (measureLevelDb (juce::AudioBuffer<float> (2, 100), 48000.0) == -100.0f);

        auto quieter = tone;
        quieter.applyGain (0.5f);
        CHECK (measureDifferenceDb (tone, tone) == -100.0f);
        CHECK (measureDifferenceDb (tone, quieter) == Approx (-6.02f).margin (0.01f));
    }
}

//==============================================================================
TEST_CASE ("Zone Smoothing: glättet Ausreißer, lässt Verläufe stehen")
{
    SECTION ("Linearer Verlauf bleibt unverändert")
    {
        const auto corrections = computeZoneSmoothing ({ { 48, 127, -10.0f }, { 52, 127, -11.0f },
                                                         { 55, 127, -12.0f }, { 60, 127, -13.0f } }, 1.0f);

        for (const auto c : corrections)
            CHECK (c == Approx (0.0f).margin (1.0e-4f));
    }

    SECTION ("Ausreißer mit Stärke und Grenze")
    {
        const std::vector<ZoneLevel> levels { { 48, 127, -10.0f }, { 52, 127, -10.0f }, { 55, 127, -4.0f },
                                              { 60, 127, -10.0f }, { 64, 127, -10.0f } };

        auto corrections = computeZoneSmoothing (levels, 1.0f);
        CHECK (corrections[2] == Approx (-3.0f));
        CHECK (corrections[1] == Approx (1.5f));

        corrections = computeZoneSmoothing (levels, 0.5f);
        CHECK (corrections[2] == Approx (-1.5f));

        CHECK (computeZoneSmoothing (levels, 0.0f) == std::vector<float> (5, 0.0f));

        auto loud = levels;
        loud[2].levelDb = 40.0f;
        CHECK (computeZoneSmoothing (loud, 1.0f)[2] == Approx (-maxZoneSmoothingDb));
    }

    SECTION ("Varianten teilen die Korrektur, Layer getrennt, Stille und kurze Layer bleiben")
    {
        const auto corrections = computeZoneSmoothing ({ { 48, 127, -10.0f }, { 55, 127, -3.0f }, { 55, 127, -5.0f },
                                                         { 60, 127, -10.0f }, { 48, 64, -20.0f }, { 55, 64, -30.0f },
                                                         { 60, 64, -100.0f } }, 1.0f);

        CHECK (corrections[1] == Approx (corrections[2]));
        CHECK (corrections[1] < 0.0f);
        CHECK (corrections[4] == 0.0f);     // Layer 0 hat nur zwei hörbare Noten
        CHECK (corrections[5] == 0.0f);
        CHECK (corrections[6] == 0.0f);     // still
    }
}

TEST_CASE ("Zone Smoothing und Round-Robin-Reihenfolge in Sampler und Export")
{
    TempFolder temp;

    std::vector<SamplerZoneSource> sources;
    sources.push_back (writeSource (temp.folder, "c2.wav", 48, 127, 0.25f));
    sources.push_back (writeSource (temp.folder, "c3b.wav", 60, 127, 0.4f, 1));
    sources.push_back (writeSource (temp.folder, "c3a.wav", 60, 127, 0.5f, 0));
    sources.push_back (writeSource (temp.folder, "c4.wav", 72, 127, 0.25f));
    sources[2].id = 1;

    // Erwartete Korrektur: mittlere Leistung der beiden C3-Varianten gegen die Nachbarn
    const auto levelOf = [] (float amplitude) { return 20.0f * std::log10 (amplitude / std::sqrt (2.0f)); };
    const auto middle = 10.0f * std::log10 ((std::pow (10.0f, levelOf (0.5f) / 10.0f) + std::pow (10.0f, levelOf (0.4f) / 10.0f)) / 2.0f);
    const auto expectedDb = juce::jlimit (-6.0f, 6.0f, (levelOf (0.25f) + 2.0f * middle + levelOf (0.25f)) / 4.0f - middle);

    SECTION ("Sampler")
    {
        SamplerAudioCache cache;
        juce::StringArray errors;
        const auto instrument = buildSamplerInstrument (sources, cache, errors, 1.0f);
        REQUIRE (errors.isEmpty());

        const auto group = instrument->findGroup (60, 127);
        REQUIRE (group >= 0);
        const auto& members = instrument->getGroup (group);
        REQUIRE (members.size() == 2);

        // Variante 0 zuerst, obwohl sie in der Session später steht
        const auto& first = instrument->getZones()[(size_t) members[0]];
        CHECK (first.id == 1);
        CHECK (juce::Decibels::gainToDecibels (first.gain) == Approx (expectedDb).margin (0.05f));

        const auto plain = buildSamplerInstrument (sources, cache, errors);
        CHECK (plain->getZones()[0].gain == 1.0f);
    }

    SECTION ("Export")
    {
        const auto plan = planExport (sources, ExportTarget::wav, "Pad");
        REQUIRE (plan.size() == 4);
        CHECK (plan[1].source.file.getFileName() == "c3a.wav");
        CHECK (plan[1].fileName == "Pad_060_C3_v127_rr1.wav");
        CHECK (plan[2].fileName == "Pad_060_C3_v127_rr2.wav");

        ExportSettings settings;
        settings.target = ExportTarget::wav;
        settings.zoneSmoothing = 1.0f;

        const auto out = temp.folder.getChildFile ("out");
        const auto result = runExport (sources, settings, "Pad", out);
        REQUIRE (result.error.isEmpty());

        const auto peak = peakOf (result.output.getChildFile ("Pad_060_C3_v127_rr1.wav"));
        CHECK (juce::Decibels::gainToDecibels (peak / 0.5f) == Approx (expectedDb).margin (0.05f));
        CHECK (peakOf (result.output.getChildFile ("Pad_060_C3_v127_rr2.wav")) / peak == Approx (0.8f).margin (0.002f));
    }
}

TEST_CASE ("Round Robin: Session speichert Variante und Wert")
{
    TempFolder temp;
    Session session;

    juce::PluginDescription desc;
    desc.name = "TestSynth";
    REQUIRE (session.beginRecording (temp.folder.getChildFile ("RR.asession"), "RR", desc, {}, {}).isEmpty());

    const SampleRequest plain { 60, 127 };
    const SampleRequest variant { 60, 127, 1, 3, 40 };

    for (const auto& request : { plain, variant })
    {
        const auto file = session.getSamplesFolder().getChildFile (makeSampleFileName ("RR", request));
        REQUIRE (writeWavFile (file, makeTone (480, 0.5f), 48000.0, 24, 60).isEmpty());
        session.addRecordedSample ({ file, request, 48000.0, 480, 2 });
    }

    auto first = session.getSample (0);
    auto second = session.getSample (1);
    CHECK_FALSE (first.getTree().hasProperty (sessionIds::roundRobin));
    CHECK (first.getRoundRobin() == 0);
    CHECK (first.getVariation() == -1);
    CHECK (second.getRoundRobin() == 1);
    CHECK (second.getVariation() == 40);
    CHECK (second.getFileName() == "RR_060_C3_v127_rr2.wav");
}
