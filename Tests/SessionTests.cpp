#include "TestSynth.h"
#include "Text.h"
#include "engine/SampleWriter.h"
#include "engine/SamplingJob.h"
#include "engine/Session.h"

#include <catch2/catch_test_macros.hpp>

using namespace autosampler;
using autosampler::test::TestSynth;

namespace
{
    struct TempFolder
    {
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }

        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("AutosamplerSessionTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    };

    juce::PluginDescription makePluginDescription()
    {
        juce::PluginDescription desc;
        desc.name = "TestSynth";
        desc.manufacturerName = "Autosampler";
        desc.pluginFormatName = "VST3";
        desc.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/TestSynth.vst3";
        desc.uniqueId = 1234;
        desc.isInstrument = true;
        return desc;
    }

    const juce::MemoryBlock pluginState ("plugin-state", 12);

    /** Beginnt eine Session im Paketordner und schreibt zwei echte WAV-Dateien. */
    void recordTwoSamples (Session& session, const juce::File& package)
    {
        SamplingSettings settings;
        settings.velocities = "64, 127";
        settings.holdSeconds = 1.5;

        REQUIRE (session.beginRecording (package, "Test", makePluginDescription(), pluginState, settings).isEmpty());

        for (const auto velocity : { 64, 127 })
        {
            juce::AudioBuffer<float> audio (2, 4800);
            audio.clear();
            audio.setSample (0, 10, 0.5f);

            const SampleRequest request { 60, velocity };
            const auto file = session.getSamplesFolder().getChildFile (makeSampleFileName ("Test", request));
            REQUIRE (writeWavFile (file, audio, 48000.0, 24, 60).isEmpty());
            session.addRecordedSample ({ file, request, 48000.0, audio.getNumSamples(), audio.getNumChannels() });
        }
    }
}

TEST_CASE ("Session: ValueTree <-> JSON")
{
    juce::ValueTree tree ("Root");
    tree.setProperty ("text", "Grüße"_u, nullptr);
    tree.setProperty ("int", 42, nullptr);
    tree.setProperty ("double", 0.25, nullptr);
    tree.setProperty ("flag", true, nullptr);

    juce::ValueTree child ("Child");
    child.setProperty ("x", 1, nullptr);
    child.appendChild (juce::ValueTree ("GrandChild"), nullptr);
    tree.appendChild (child, nullptr);

    const auto text = juce::JSON::toString (Session::toJson (tree));
    const auto restored = Session::fromJson (juce::JSON::parse (text));

    CHECK (restored.isEquivalentTo (tree));
    CHECK (restored["text"].toString() == "Grüße"_u);
    CHECK ((bool) restored["flag"]);
    CHECK (! Session::fromJson (juce::JSON::parse ("{\"type\": \"1 ungültig\"}"_u)).isValid());
}

TEST_CASE ("Session: Paketendung")
{
    const auto folder = juce::File ("/tmp/My.Synth");
    CHECK (Session::withPackageExtension (folder).getFileName() == "My.Synth.asession");
    CHECK (Session::withPackageExtension (juce::File ("/tmp/A.asession")).getFileName() == "A.asession");
}

TEST_CASE ("Session: Aufnahme, Speichern und Laden")
{
    TempFolder temp;
    const auto package = temp.folder.getChildFile ("Test.asession");

    Session session;
    CHECK (session.isEmpty());
    CHECK_FALSE (session.getSamplingSettings().has_value());
    recordTwoSamples (session, package);

    CHECK (session.getNumSamples() == 2);
    CHECK (session.hasUnsavedChanges());
    REQUIRE (session.save().isEmpty());
    CHECK_FALSE (session.hasUnsavedChanges());

    CHECK (package.getChildFile ("session.json").existsAsFile());
    CHECK (package.getChildFile ("plugin-state.bin").getSize() == 12);
    CHECK (package.getChildFile ("samples/Test_060_C3_v064.wav").existsAsFile());

    // Bearbeiten und speichern
    session.getSample (1).setLoopEnabled (true);
    session.getSample (1).setLoop (1000, 3000);
    session.getSample (0).setLocked (true);
    REQUIRE (session.save().isEmpty());

    Session loaded;
    REQUIRE (loaded.load (package).isEmpty());
    CHECK_FALSE (loaded.hasUnsavedChanges());
    CHECK (loaded.getName() == "Test");
    CHECK (loaded.getPackageFolder() == package);
    CHECK (loaded.getPluginState() == pluginState);
    REQUIRE (loaded.getSamplingSettings().has_value());
    CHECK (loaded.getSamplingSettings()->velocities == "64, 127");
    CHECK (juce::exactlyEqual (loaded.getSamplingSettings()->holdSeconds, 1.5));

    const auto plugin = loaded.getPluginDescription();
    REQUIRE (plugin.has_value());
    CHECK (plugin->isDuplicateOf (makePluginDescription()));

    REQUIRE (loaded.getNumSamples() == 2);
    auto first = loaded.getSample (0);
    auto second = loaded.getSample (1);
    CHECK (first.getNote() == 60);
    CHECK (first.getVelocity() == 64);
    CHECK (first.getNumFrames() == 4800);
    CHECK (first.isLocked());
    CHECK_FALSE (first.isLoopEnabled());
    CHECK (second.isLoopEnabled());
    CHECK (second.getLoopStart() == 1000);
    CHECK (second.getLoopEnd() == 3000);
    CHECK (loaded.getSampleFile (second.getTree()).existsAsFile());

    // Laden über die session.json selbst
    Session viaFile;
    CHECK (viaFile.load (package.getChildFile ("session.json")).isEmpty());
    CHECK (viaFile.getNumSamples() == 2);
}

TEST_CASE ("Session: Laden schlägt fehlerhaft sauber fehl")
{
    TempFolder temp;
    Session session;
    session.setName ("Bleibt");

    CHECK (session.load (temp.folder).isNotEmpty());
    CHECK (session.getName() == "Bleibt");

    temp.folder.getChildFile ("session.json").replaceWithText ("{ kaputt");
    CHECK (session.load (temp.folder).isNotEmpty());

    temp.folder.getChildFile ("session.json").replaceWithText (R"({ "format": "Autosampler Session", "version": 99, "session": {} })");
    CHECK (session.load (temp.folder).contains ("newer"));
    CHECK (session.getName() == "Bleibt");
}

TEST_CASE ("Session: Speichern unter kopiert die Samples")
{
    TempFolder temp;
    Session session;
    recordTwoSamples (session, temp.folder.getChildFile ("Original.asession"));
    REQUIRE (session.save().isEmpty());

    const auto copy = temp.folder.getChildFile ("Kopie");
    REQUIRE (session.saveAs (copy).isEmpty());

    const auto package = temp.folder.getChildFile ("Kopie.asession");
    CHECK (session.getPackageFolder() == package);
    CHECK (session.getName() == "Kopie");
    CHECK_FALSE (session.hasUnsavedChanges());
    CHECK (package.getChildFile ("samples/Test_060_C3_v127.wav").existsAsFile());
    CHECK (package.getChildFile ("plugin-state.bin").existsAsFile());
    CHECK (session.getSampleFile (session.getSample (0).getTree()).isAChildOf (package));

    // Ziel existiert schon
    CHECK (session.saveAs (temp.folder.getChildFile ("Original.asession")).isNotEmpty());
}

TEST_CASE ("Session: Loop-Punkte werden begrenzt, Lock verhindert Änderungen")
{
    TempFolder temp;
    Session session;
    recordTwoSamples (session, temp.folder.getChildFile ("Test.asession"));

    auto sample = session.getSample (0);
    CHECK_FALSE (sample.isLoopEnabled());
    CHECK (sample.getLoopStart() == 2400);     // Standard: 50 % bis 90 %
    CHECK (sample.getLoopEnd() == 4320);

    sample.setLoopEnabled (true);
    sample.setLoopStart (-100);
    CHECK (sample.getLoopStart() == 0);

    sample.setLoopEnd (100000);
    CHECK (sample.getLoopEnd() == 4800);

    sample.setLoopStart (4799);
    CHECK (sample.getLoopStart() == 4800 - SessionSample::minLoopLength);
    CHECK (sample.getLoopEnd() == 4800);

    sample.setLoopEnd (0);
    CHECK (sample.getLoopEnd() == sample.getLoopStart() + SessionSample::minLoopLength);

    sample.setLocked (true);
    sample.setLoop (100, 200);
    sample.setLoopEnabled (false);
    CHECK (sample.getLoopStart() == 4800 - SessionSample::minLoopLength);
    CHECK (sample.isLoopEnabled());
}

TEST_CASE ("Session: Undo und Redo")
{
    TempFolder temp;
    Session session;
    recordTwoSamples (session, temp.folder.getChildFile ("Test.asession"));

    auto& undoManager = session.getUndoManager();
    CHECK_FALSE (undoManager.canUndo());   // Aufnahmen sind nicht rückgängig zu machen

    auto sample = session.getSample (0);

    undoManager.beginNewTransaction ("Loop an");
    sample.setLoopEnabled (true);

    // Ein Ziehvorgang mit mehreren Schritten ist eine Transaktion
    undoManager.beginNewTransaction ("Loop-Start verschieben");
    sample.setLoopStart (1000);
    sample.setLoopStart (1100);
    sample.setLoopStart (1200);
    CHECK (sample.getLoopStart() == 1200);

    REQUIRE (undoManager.undo());
    CHECK (sample.getLoopStart() == 2400);
    CHECK (sample.isLoopEnabled());

    REQUIRE (undoManager.undo());
    CHECK_FALSE (sample.isLoopEnabled());
    CHECK_FALSE (undoManager.canUndo());

    REQUIRE (undoManager.redo());
    REQUIRE (undoManager.redo());
    CHECK (sample.getLoopStart() == 1200);

    // Laden und Leeren löschen die Historie
    session.clear();
    CHECK_FALSE (undoManager.canUndo());
    CHECK (session.getNumSamples() == 0);
    CHECK (session.getPackageFolder() == juce::File());
}

TEST_CASE ("Session: Sampling-Lauf meldet geschriebene Samples")
{
    TempFolder temp;
    TestSynth synth;

    SamplingSettings settings;
    settings.useCustomNotes = true;
    settings.customNotes = "C3, A3";
    settings.velocities = "100";
    settings.holdSeconds = 0.2;
    settings.maxTailSeconds = 2.0;
    settings.renderMode = RenderMode::offline;

    Session session;
    const auto package = temp.folder.getChildFile ("Run.asession");
    REQUIRE (session.beginRecording (package, "Run", makePluginDescription(), {}, settings).isEmpty());

    juce::String error;
    SamplingContext context { synth };
    context.settings = settings;
    context.requests = buildSampleList (settings, error);
    context.outputFolder = session.getSamplesFolder();
    context.filePrefix = "Run";

    SamplingCallbacks callbacks;
    callbacks.onSampleWritten = [&] (const RecordedSample& sample) { session.addRecordedSample (sample); };

    const auto result = runSampling (context, callbacks);
    REQUIRE (result.error.isEmpty());
    REQUIRE (result.numWritten == 2);
    REQUIRE (session.save().isEmpty());

    Session loaded;
    REQUIRE (loaded.load (package).isEmpty());
    REQUIRE (loaded.getNumSamples() == 2);

    for (int i = 0; i < 2; ++i)
    {
        const auto sample = loaded.getSample (i);
        const auto file = loaded.getSampleFile (sample.getTree());
        CHECK (file.existsAsFile());
        CHECK (sample.getNumFrames() > 0);
        CHECK (sample.getVelocity() == 100);
        CHECK (sample.getFileName() == file.getFileName());   // relativ zum Paket gespeichert
    }

    CHECK (loaded.getSample (0).getNote() == 60);
    CHECK (loaded.getSample (1).getNote() == 69);
    CHECK_FALSE (package.getChildFile ("plugin-state.bin").exists());
}
