#include "engine/PerformTools.h"

#include <catch2/catch_test_macros.hpp>

using namespace autosampler;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 512;

    struct Event
    {
        int time = 0;
        bool on = false;
        int note = 0, velocity = 0;
    };

    std::vector<Event> collect (const juce::MidiBuffer& buffer, int offset = 0)
    {
        std::vector<Event> events;

        for (const auto metadata : buffer)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOnOrOff())
                events.push_back ({ offset + metadata.samplePosition, message.isNoteOn(), message.getNoteNumber(), message.getVelocity() });
        }

        return events;
    }

    std::vector<int> noteOns (const std::vector<Event>& events)
    {
        std::vector<int> notes;

        for (const auto& e : events)
            if (e.on)
                notes.push_back (e.note);

        return notes;
    }

    /** Lässt den PatternPlayer numSamples laufen; input: (absolute Zeit, Nachricht). */
    std::vector<Event> run (PatternPlayer& player, int numSamples, const std::vector<std::pair<int, juce::MidiMessage>>& input,
                            int startTime = 0)
    {
        std::vector<Event> events;

        for (int start = 0; start < numSamples; start += blockSize)
        {
            const auto length = juce::jmin (blockSize, numSamples - start);
            juce::MidiBuffer in, out;

            for (const auto& [time, message] : input)
                if (time - startTime >= start && time - startTime < start + length)
                    in.addEvent (message, time - startTime - start);

            player.process (in, out, length);
            const auto blockEvents = collect (out, startTime + start);
            events.insert (events.end(), blockEvents.begin(), blockEvents.end());
        }

        return events;
    }

    PerformSettings arpSettings (ArpMode mode, int octaves = 1)
    {
        PerformSettings s;
        s.pattern = PatternMode::arp;
        s.arpMode = mode;
        s.arpOctaves = octaves;
        s.bpm = 120.0;
        s.rate = StepRate::sixteenth;     // 0,125 s = 6000 Frames
        s.gate = 0.5f;
        return s;
    }

    juce::MidiMessage on (int note, int velocity = 100)  { return juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity); }
    juce::MidiMessage off (int note)                     { return juce::MidiMessage::noteOff (1, note); }
}

TEST_CASE ("Akkord-Intervalle und Umkehrungen", "[perform]")
{
    const auto major = getChordIntervals (ChordType::major);
    REQUIRE (major.count == 3);
    CHECK (major.values[1] == 4);
    CHECK (major.values[2] == 7);

    const auto firstInversion = getChordIntervals (ChordType::major, 1);
    CHECK (firstInversion.values[0] == 4);
    CHECK (firstInversion.values[1] == 7);
    CHECK (firstInversion.values[2] == 12);

    // Mehr Umkehrungen als Töne werden begrenzt
    CHECK (getChordIntervals (ChordType::octave, 3).count == 2);
    CHECK (getChordIntervals (ChordType::off).count == 1);
}

TEST_CASE ("Chord-Player: Akkord, passende Note-Offs, gemeinsame Töne", "[perform]")
{
    ChordProcessor chord;
    chord.setChord (ChordType::major, 0);

    juce::MidiBuffer in, out;
    in.addEvent (on (60), 0);
    chord.process (in, out);
    CHECK (noteOns (collect (out)) == std::vector<int> { 60, 64, 67 });

    // Akkord wechseln, während die Taste gehalten wird: Note-Off beendet trotzdem C-Dur
    chord.setChord (ChordType::minor, 0);
    in.clear(); out.clear();
    in.addEvent (off (60), 10);
    chord.process (in, out);
    auto events = collect (out);
    REQUIRE (events.size() == 3);
    CHECK ((! events[0].on && events[0].note == 60 && events[1].note == 64 && events[2].note == 67));

    // C-Dur und E-Dur teilen das E: nur ein Note-On, Note-Off erst mit der letzten Taste
    chord.setChord (ChordType::major, 0);
    in.clear(); out.clear();
    in.addEvent (on (60), 0);
    in.addEvent (on (64), 1);       // 64, 68, 71
    in.addEvent (off (60), 2);
    chord.process (in, out);
    events = collect (out);
    CHECK (noteOns (events) == std::vector<int> { 60, 64, 67, 68, 71 });

    const auto offs = std::count_if (events.begin(), events.end(), [] (const Event& e) { return ! e.on; });
    CHECK (offs == 2);     // 60 und 67, nicht 64

    in.clear(); out.clear();
    chord.reset (out, 0);
    CHECK (collect (out).size() == 3);
}

TEST_CASE ("Arpeggiator: Reihenfolge und Takt", "[perform]")
{
    PatternPlayer player;
    player.prepare (rate);
    juce::MidiBuffer scratch;

    SECTION ("Auf, eine Oktave, Gate 50 %")
    {
        player.setSettings (arpSettings (ArpMode::up), scratch);
        const auto events = run (player, 24000, { { 0, on (67) }, { 0, on (60) }, { 0, on (64) } });
        const auto ons = noteOns (events);
        CHECK (ons == std::vector<int> { 60, 64, 67, 60 });

        CHECK (events[0].time == 0);
        CHECK ((! events[1].on && events[1].note == 60 && events[1].time == 3000));
        CHECK ((events[2].on && events[2].time == 6000));
    }

    SECTION ("Auf/Ab über zwei Oktaven ohne doppelte Endtöne")
    {
        player.setSettings (arpSettings (ArpMode::upDown, 2), scratch);
        const auto ons = noteOns (run (player, 6000 * 8, { { 0, on (60) }, { 0, on (64) } }));
        CHECK (ons == std::vector<int> { 60, 64, 72, 76, 72, 64, 60, 64 });
    }

    SECTION ("Ab und Gespielt")
    {
        player.setSettings (arpSettings (ArpMode::down), scratch);
        CHECK (noteOns (run (player, 6000 * 3, { { 0, on (60) }, { 0, on (67) }, { 0, on (64) } })) == std::vector<int> { 67, 64, 60 });

        player.reset (scratch, 0);
        player.setSettings (arpSettings (ArpMode::asPlayed), scratch);
        CHECK (noteOns (run (player, 6000 * 3, { { 0, on (67) }, { 0, on (60) }, { 0, on (64) } })) == std::vector<int> { 67, 60, 64 });
    }

    SECTION ("Loslassen hält an, Latch nicht")
    {
        player.setSettings (arpSettings (ArpMode::up), scratch);
        auto events = run (player, 30000, { { 0, on (60) }, { 7000, off (60) } });
        CHECK (noteOns (events).size() == 2);   // 0 und 6000

        auto latched = arpSettings (ArpMode::up);
        latched.latch = true;
        player.reset (scratch, 0);
        player.setSettings (latched, scratch);
        events = run (player, 30000, { { 0, on (60) }, { 7000, off (60) } });
        CHECK (noteOns (events).size() == 5);

        // Neue Taste nach dem Loslassen ersetzt die gehaltenen Töne
        events = run (player, 12000, { { 30000, on (72) } }, 30000);
        CHECK (noteOns (events) == std::vector<int> { 72, 72 });
    }
}

TEST_CASE ("Step-Sequencer: Schritte transponiert um die gedrückte Taste", "[perform]")
{
    PatternPlayer player;
    player.prepare (rate);
    juce::MidiBuffer scratch;

    PerformSettings s;
    s.pattern = PatternMode::sequencer;
    s.bpm = 120.0;
    s.rate = StepRate::eighth;       // 12000 Frames
    s.sequencerLength = 4;
    s.steps[0] = { true, 0, 100 };
    s.steps[1] = { true, 7, 80 };
    s.steps[2] = { false, 0, 100 };
    s.steps[3] = { true, 12, 127 };
    player.setSettings (s, scratch);

    const auto events = run (player, 50000, { { 0, on (62, 50) } });
    CHECK (noteOns (events) == std::vector<int> { 62, 69, 74, 62 });
    CHECK (events[0].velocity == 100);       // Velocity des Schritts, nicht der Taste
    CHECK (events[2].velocity == 80);

    // Wechsel des Modus beendet laufende Noten
    juce::MidiBuffer out;
    s.pattern = PatternMode::off;
    player.setSettings (s, out);
    const auto flushed = collect (out);
    CHECK_FALSE (flushed.empty());
    CHECK (std::none_of (flushed.begin(), flushed.end(), [] (const Event& e) { return e.on; }));
}

TEST_CASE ("Pattern aus: Noten gehen direkt durch", "[perform]")
{
    PatternPlayer player;
    player.prepare (rate);

    const auto events = run (player, 2000, { { 100, on (60) }, { 900, off (60) },
                                             { 1000, juce::MidiMessage::controllerEvent (1, 1, 64) } });
    REQUIRE (events.size() == 2);
    CHECK ((events[0].on && events[0].time == 100));
    CHECK ((! events[1].on && events[1].time == 900));
}
