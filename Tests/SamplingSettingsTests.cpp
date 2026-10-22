#include "engine/SampleWriter.h"
#include "engine/SamplingSettings.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace autosampler;

TEST_CASE ("Notennamen mit C3 = 60")
{
    CHECK (getNoteName (60) == "C3");
    CHECK (getNoteName (0) == "C-2");
    CHECK (getNoteName (127) == "G8");

    CHECK (parseNoteName ("C3") == 60);
    CHECK (parseNoteName ("c3") == 60);
    CHECK (parseNoteName ("C-2") == 0);
    CHECK (parseNoteName ("G8") == 127);
    CHECK (parseNoteName ("A#2") == 58);
    CHECK (parseNoteName ("Bb2") == 58);
    CHECK (parseNoteName ("60") == 60);
    CHECK (parseNoteName (" E4 ") == 76);

    CHECK_FALSE (parseNoteName ("G#8").has_value());
    CHECK_FALSE (parseNoteName ("H3").has_value());
    CHECK_FALSE (parseNoteName ("C").has_value());
    CHECK_FALSE (parseNoteName ("128").has_value());
    CHECK_FALSE (parseNoteName ("C3x").has_value());

    for (int note = 0; note < 128; ++note)
        CHECK (parseNoteName (getNoteName (note)) == note);
}

TEST_CASE ("Notenlisten")
{
    juce::String error;
    CHECK (parseNoteList ("E2, C2 60;C2", error) == juce::Array<int> { 48, 52, 60 });
    CHECK (error.isEmpty());

    parseNoteList ("C2, X5", error);
    CHECK (error.contains ("X5"));
}

TEST_CASE ("Velocity-Listen")
{
    juce::String error;
    CHECK (parseVelocityList ("110, 30 60,90,60", error) == juce::Array<int> { 30, 60, 90, 110 });
    CHECK (error.isEmpty());

    parseVelocityList ("0", error);
    CHECK (error.isNotEmpty());

    error.clear();
    parseVelocityList ("128", error);
    CHECK (error.isNotEmpty());

    error.clear();
    parseVelocityList ("abc", error);
    CHECK (error.isNotEmpty());
}

TEST_CASE ("Sample-Liste aus Raster und Custom Notes")
{
    SamplingSettings s;
    s.lowNote = 36;
    s.highNote = 48;
    s.noteStep = 4;
    s.velocities = "100, 50";

    juce::String error;
    const auto requests = buildSampleList (s, error);
    REQUIRE (error.isEmpty());
    REQUIRE (requests.size() == 8);
    CHECK (requests[0] == SampleRequest { 36, 50 });
    CHECK (requests[1] == SampleRequest { 36, 100 });
    CHECK (requests[7] == SampleRequest { 48, 100 });

    s.useCustomNotes = true;
    s.customNotes = "C3, G3";
    CHECK (buildSampleList (s, error).size() == 4);

    s.useCustomNotes = false;
    s.lowNote = 50;
    CHECK (buildSampleList (s, error).isEmpty());
    CHECK (error.isNotEmpty());
}

TEST_CASE ("Einstellungen über ValueTree")
{
    SamplingSettings s;
    s.lowNote = 21;
    s.highNote = 108;
    s.useCustomNotes = true;
    s.customNotes = "C1, C2";
    s.velocities = "1, 127";
    s.renderMode = RenderMode::realtime;
    s.bitDepth = 32;
    s.holdSeconds = 3.5;
    s.outputFolder = juce::File ("/tmp/foo");
    s.name = "Pad";

    const auto restored = SamplingSettings::fromValueTree (s.toValueTree());
    CHECK (restored.lowNote == 21);
    CHECK (restored.highNote == 108);
    CHECK (restored.useCustomNotes);
    CHECK (restored.customNotes == "C1, C2");
    CHECK (restored.velocities == "1, 127");
    CHECK (restored.renderMode == RenderMode::realtime);
    CHECK (restored.bitDepth == 32);
    CHECK (restored.holdSeconds == Catch::Approx (3.5));
    CHECK (restored.outputFolder == juce::File ("/tmp/foo"));
    CHECK (restored.name == "Pad");
}

TEST_CASE ("Dateinamen")
{
    CHECK (makeSampleFileName ("My Synth", { 60, 100 }) == "My_Synth_060_C3_v100.wav");
    CHECK (makeSampleFileName ("", { 61, 7 }) == "061_C#3_v007.wav");
    CHECK (makeSampleFileName ("a/b:c", { 0, 127 }).endsWith ("_000_C-2_v127.wav"));
}
