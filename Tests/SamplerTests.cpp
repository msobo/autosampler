#include "engine/LivePlaySettings.h"
#include "engine/SampleWriter.h"
#include "engine/SamplerEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace autosampler;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;

    /** Sinus mit fester Amplitude, Länge in Sekunden. */
    std::shared_ptr<const juce::AudioBuffer<float>> makeSine (double frequency, double seconds, float amplitude = 0.5f)
    {
        auto buffer = std::make_shared<juce::AudioBuffer<float>> (2, (int) (seconds * rate));

        for (int i = 0; i < buffer->getNumSamples(); ++i)
        {
            const auto value = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate);
            buffer->setSample (0, i, value);
            buffer->setSample (1, i, value);
        }

        return buffer;
    }

    SamplerZone makeZone (std::shared_ptr<const juce::AudioBuffer<float>> audio, int note, int velocity, juce::int64 id = 0)
    {
        SamplerZone zone;
        zone.audio = std::move (audio);
        zone.sampleRate = rate;
        zone.rootNote = note;
        zone.velocity = velocity;
        zone.id = id;
        return zone;
    }

    std::shared_ptr<const SamplerInstrument> makeInstrument (std::vector<SamplerZone> zones)
    {
        std::vector<std::pair<int, int>> pairs;

        for (const auto& zone : zones)
            pairs.emplace_back (zone.rootNote, zone.velocity);

        const auto ranges = computeZoneRanges (pairs);

        for (size_t i = 0; i < zones.size(); ++i)
            zones[i].range = ranges[i];

        return std::make_shared<const SamplerInstrument> (std::move (zones));
    }

    /** Rendert seconds Sekunden; events: (Zeit in s, Nachricht). */
    juce::AudioBuffer<float> render (SamplerEngine& engine, double seconds,
                                     std::vector<std::pair<double, juce::MidiMessage>> events)
    {
        const auto numFrames = (int) (seconds * rate);
        juce::AudioBuffer<float> output (2, numFrames);
        output.clear();

        for (int start = 0; start < numFrames; start += blockSize)
        {
            const auto length = juce::jmin (blockSize, numFrames - start);
            juce::MidiBuffer midi;

            for (const auto& [time, message] : events)
                if (const auto frame = (int) (time * rate); frame >= start && frame < start + length)
                    midi.addEvent (message, frame - start);

            engine.render (output, midi, start, length);
        }

        return output;
    }

    float rmsBetween (const juce::AudioBuffer<float>& buffer, double from, double to)
    {
        const auto start = (int) (from * rate);
        return buffer.getRMSLevel (0, start, (int) (to * rate) - start);
    }

    /** Frequenz aus steigenden Nulldurchgängen. */
    double measureFrequency (const juce::AudioBuffer<float>& buffer, double from, double to)
    {
        const auto* data = buffer.getReadPointer (0);
        int first = -1, last = -1, crossings = 0;

        for (int i = (int) (from * rate) + 1; i < (int) (to * rate); ++i)
            if (data[i - 1] < 0.0f && data[i] >= 0.0f)
            {
                if (first < 0)
                    first = i;

                last = i;
                ++crossings;
            }

        return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0.0;
    }

    float maxStep (const juce::AudioBuffer<float>& buffer, double from, double to)
    {
        const auto* data = buffer.getReadPointer (0);
        float result = 0.0f;

        for (int i = (int) (from * rate) + 1; i < (int) (to * rate); ++i)
            result = juce::jmax (result, std::abs (data[i] - data[i - 1]));

        return result;
    }
}

TEST_CASE ("Zonen: Tasten bis zur Mitte, Velocities bis zur nächsten Stufe", "[sampler]")
{
    const auto ranges = computeZoneRanges ({ { 48, 64 }, { 48, 127 }, { 60, 127 }, { 72, 100 } });
    REQUIRE (ranges.size() == 4);

    CHECK (ranges[0].lowKey == 0);
    CHECK (ranges[0].highKey == 54);
    CHECK (ranges[0].lowVelocity == 1);
    CHECK (ranges[0].highVelocity == 64);
    CHECK (ranges[1].lowVelocity == 65);
    CHECK (ranges[1].highVelocity == 127);

    CHECK (ranges[2].lowKey == 55);
    CHECK (ranges[2].highKey == 66);
    CHECK (ranges[2].lowVelocity == 1);
    CHECK (ranges[2].highVelocity == 127);

    // Höchste Velocity einer Note deckt bis 127 ab, auch wenn sie darunter aufgenommen wurde
    CHECK (ranges[3].lowKey == 67);
    CHECK (ranges[3].highKey == 127);
    CHECK (ranges[3].highVelocity == 127);
}

TEST_CASE ("Instrument: Gruppen, Lookup und Round Robin", "[sampler]")
{
    const auto sine = makeSine (440.0, 0.2);
    const auto instrument = makeInstrument ({ makeZone (sine, 60, 100, 1), makeZone (sine, 60, 100, 2), makeZone (sine, 72, 100, 3) });

    CHECK (instrument->getNumGroups() == 2);
    CHECK (instrument->findGroup (60, 10) == instrument->findGroup (65, 127));
    CHECK (instrument->findGroup (70, 64) != instrument->findGroup (60, 64));
    CHECK (instrument->getGroup (instrument->findGroup (60, 100)).size() == 2);
    CHECK (instrument->findZone (3) == 2);
    CHECK (instrument->findZone (99) == -1);

    // Round Robin: zwei Noten nacheinander nutzen verschiedene Zonen (unterschiedliche Pegel)
    const auto quiet = makeSine (440.0, 0.2, 0.1f);
    const auto loud = makeSine (440.0, 0.2, 0.8f);
    SamplerEngine engine;
    engine.prepare (rate);
    SamplerSettings settings;
    settings.velocitySensitivity = 0.0f;
    engine.setSettings (settings);
    engine.setInstrument (makeInstrument ({ makeZone (quiet, 60, 100), makeZone (loud, 60, 100) }));

    const auto output = render (engine, 1.0, { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) },
                                               { 0.5, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) } });
    const auto first = output.getMagnitude (0, 0, (int) (0.15 * rate));
    const auto second = output.getMagnitude (0, (int) (0.5 * rate), (int) (0.15 * rate));
    CHECK (first == Catch::Approx (0.1f).margin (0.01f));
    CHECK (second == Catch::Approx (0.8f).margin (0.02f));
}

TEST_CASE ("Sampler: Tonhöhe folgt der Taste", "[sampler]")
{
    SamplerEngine engine;
    engine.prepare (rate);
    engine.setInstrument (makeInstrument ({ makeZone (makeSine (220.0, 2.0), 57, 127) }));

    const auto output = render (engine, 0.5, { { 0.0, juce::MidiMessage::noteOn (1, 69, (juce::uint8) 127) } });
    CHECK (measureFrequency (output, 0.05, 0.45) == Catch::Approx (440.0).epsilon (0.002));

    // Andere Geräte-Rate: Abspielrate wird umgerechnet
    SamplerEngine engine44;
    engine44.prepare (44100.0);
    engine44.setInstrument (makeInstrument ({ makeZone (makeSine (220.0, 2.0), 57, 127) }));
    juce::AudioBuffer<float> output44 (2, 22050);
    output44.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 127), 0);
    engine44.render (output44, midi, 0, 22050);

    const auto* data = output44.getReadPointer (0);
    int crossings = 0;

    for (int i = 1; i < 22050; ++i)
        if (data[i - 1] < 0.0f && data[i] >= 0.0f)
            ++crossings;

    CHECK (crossings == Catch::Approx (110).margin (1));
}

TEST_CASE ("Sampler: Loop läuft beim Halten und im Release nahtlos weiter", "[sampler]")
{
    // 375 Hz = 128 Frames pro Periode; Loop über 100 Perioden, Audio endet bei E
    auto audio = std::make_shared<juce::AudioBuffer<float>> (*makeSine (375.0, 0.6));
    audio->setSize (2, 25600, true);

    auto zone = makeZone (audio, 60, 127);
    zone.loop = true;
    zone.loopStart = 12800;
    zone.loopEnd = 25600;

    SamplerEngine engine;
    engine.prepare (rate);
    SamplerSettings settings;
    settings.releaseMs = 200.0f;
    engine.setSettings (settings);
    engine.setInstrument (makeInstrument ({ zone }));

    const auto output = render (engine, 2.5, { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127) },
                                               { 2.0, juce::MidiMessage::noteOff (1, 60) } });

    // Weit hinter der Länge des Samples (0,53 s) klingt der Ton gleichmäßig weiter
    CHECK (rmsBetween (output, 1.5, 1.9) == Catch::Approx (rmsBetween (output, 0.1, 0.4)).epsilon (0.01));
    CHECK (measureFrequency (output, 1.0, 1.9) == Catch::Approx (375.0).epsilon (0.002));

    // Kein Sprung am Übergang E -> S (größter Schritt wie im Sinus selbst)
    const auto sineStep = (float) (0.5 * juce::MathConstants<double>::twoPi * 375.0 / rate);
    CHECK (maxStep (output, 0.4, 1.9) <= sineStep * 1.05f);

    // Release: nach 200 ms still
    CHECK (rmsBetween (output, 2.3, 2.5) < 1.0e-4f);
    CHECK (engine.getNumActiveVoices() == 0);
}

TEST_CASE ("Sampler: One Shot ignoriert Note-Off, sonst Release", "[sampler]")
{
    const auto instrument = makeInstrument ({ makeZone (makeSine (300.0, 0.5), 60, 127) });

    SamplerEngine oneShot;
    oneShot.prepare (rate);
    oneShot.setInstrument (instrument);
    const auto played = render (oneShot, 0.8, { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127) },
                                                { 0.05, juce::MidiMessage::noteOff (1, 60) } });
    CHECK (rmsBetween (played, 0.4, 0.48) > 0.3f);
    CHECK (rmsBetween (played, 0.55, 0.8) == 0.0f);
    CHECK (oneShot.getNumActiveVoices() == 0);

    SamplerEngine withRelease;
    withRelease.prepare (rate);
    SamplerSettings settings;
    settings.oneShotWithoutLoop = false;
    settings.releaseMs = 50.0f;
    withRelease.setSettings (settings);
    withRelease.setInstrument (instrument);
    const auto released = render (withRelease, 0.8, { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127) },
                                                       { 0.05, juce::MidiMessage::noteOff (1, 60) } });
    CHECK (rmsBetween (released, 0.0, 0.05) > 0.3f);
    CHECK (rmsBetween (released, 0.2, 0.5) < 1.0e-4f);
}

TEST_CASE ("Sampler: Velocity-Layer, Sustain-Pedal und Stimmenklau", "[sampler]")
{
    SamplerEngine engine;
    engine.prepare (rate);
    SamplerSettings settings;
    settings.velocitySensitivity = 0.0f;
    settings.oneShotWithoutLoop = false;
    settings.releaseMs = 10.0f;
    settings.maxVoices = 2;
    engine.setSettings (settings);
    engine.setInstrument (makeInstrument ({ makeZone (makeSine (200.0, 3.0, 0.2f), 60, 64),
                                            makeZone (makeSine (200.0, 3.0, 0.6f), 60, 127) }));

    SECTION ("Leise Note spielt den leisen Layer")
    {
        const auto soft = render (engine, 0.2, { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 40) } });
        CHECK (soft.getMagnitude (0, 0, soft.getNumSamples()) == Catch::Approx (0.2f).margin (0.01f));

        engine.allNotesOff (true);
        const auto hard = render (engine, 0.2, { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) } });
        CHECK (hard.getMagnitude (0, 0, hard.getNumSamples()) == Catch::Approx (0.6f).margin (0.01f));
    }

    SECTION ("Sustain-Pedal hält die Note bis zum Loslassen")
    {
        const auto output = render (engine, 1.0, { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 127) },
                                                   { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) },
                                                   { 0.1, juce::MidiMessage::noteOff (1, 60) },
                                                   { 0.5, juce::MidiMessage::controllerEvent (1, 64, 0) } });
        CHECK (rmsBetween (output, 0.3, 0.45) > 0.3f);
        CHECK (rmsBetween (output, 0.6, 1.0) < 1.0e-4f);
    }

    SECTION ("Über der Polyphonie wird die älteste Stimme ausgeblendet")
    {
        render (engine, 0.1, { { 0.0, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) },
                               { 0.01, juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100) },
                               { 0.02, juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100) } });
        CHECK (engine.getNumActiveVoices() == 2);
    }
}

TEST_CASE ("Sampler: Hörprüfung spielt ein bestimmtes Sample auf seiner Note", "[sampler]")
{
    SamplerEngine engine;
    engine.prepare (rate);
    engine.setInstrument (makeInstrument ({ makeZone (makeSine (250.0, 1.0), 60, 127, 11), makeZone (makeSine (500.0, 1.0), 72, 127, 22) }));

    engine.startAudition (22, 127);
    const auto output = render (engine, 0.3, {});
    CHECK (measureFrequency (output, 0.05, 0.25) == Catch::Approx (500.0).epsilon (0.002));
    CHECK (engine.getAuditionId() == 22);
    CHECK (engine.getAuditionFrame() == Catch::Approx (0.3 * rate).margin (blockSize));

    engine.stopAudition();
    render (engine, 0.5, {});
    CHECK (engine.getAuditionFrame() == -1);
}

TEST_CASE ("Sampler: Instrument aus WAV-Dateien mit gerendertem Loop", "[sampler]")
{
    const auto folder = juce::File::createTempFile ("autosampler-sampler");
    folder.createDirectory();

    const auto sine = makeSine (375.0, 1.0);
    const auto file = folder.getChildFile ("Test_060_C3_v127.wav");
    REQUIRE (writeWavFile (file, *sine, rate, 24, 60).isEmpty());

    SamplerZoneSource looped;
    looped.file = file;
    looped.id = makeSampleId (file.getFileName());
    looped.loop = true;
    looped.loopStart = 12800;
    looped.loopEnd = 25600;
    looped.crossfade = 1000;
    looped.crossfadeMode = CrossfadeMode::equalPower;

    auto missing = looped;
    missing.file = folder.getChildFile ("fehlt.wav");
    missing.note = 72;

    SamplerAudioCache cache;
    juce::StringArray errors;
    const auto instrument = buildSamplerInstrument ({ looped, missing }, cache, errors);

    REQUIRE (instrument->getZones().size() == 1);
    CHECK (errors.size() == 1);

    const auto& zone = instrument->getZones().front();
    CHECK (zone.loop);
    CHECK (zone.loopStart == 12800);
    CHECK (zone.loopEnd == 25600);
    CHECK (zone.audio->getNumSamples() == 25600);
    CHECK (zone.range.lowKey == 0);
    CHECK (zone.range.highKey == 127);

    // Zweiter Bau mit gleichen Quellen nutzt den Cache (selbes Audio-Objekt)
    juce::StringArray moreErrors;
    const auto again = buildSamplerInstrument ({ looped }, cache, moreErrors);
    CHECK (again->getZones().front().audio == zone.audio);

    folder.deleteRecursively();
}

TEST_CASE ("Live-Play-Einstellungen als ValueTree", "[sampler]")
{
    LivePlaySettings settings;
    settings.source = PlaySource::sampler;
    settings.sampler.releaseMs = 750.0f;
    settings.sampler.oneShotWithoutLoop = false;
    settings.perform.chord = ChordType::minor7;
    settings.perform.inversion = 2;
    settings.perform.pattern = PatternMode::sequencer;
    settings.perform.bpm = 96.0;
    settings.perform.rate = StepRate::eighthTriplet;
    settings.perform.steps[3] = { false, -5, 64 };
    settings.reverb.enabled = true;
    settings.reverb.mix = 0.4f;

    const auto restored = LivePlaySettings::fromValueTree (settings.toValueTree());
    CHECK (restored.source == PlaySource::sampler);
    CHECK (restored.sampler.releaseMs == 750.0f);
    CHECK_FALSE (restored.sampler.oneShotWithoutLoop);
    CHECK (restored.perform.chord == ChordType::minor7);
    CHECK (restored.perform.inversion == 2);
    CHECK (restored.perform.pattern == PatternMode::sequencer);
    CHECK (restored.perform.bpm == 96.0);
    CHECK (restored.perform.rate == StepRate::eighthTriplet);
    CHECK_FALSE (restored.perform.steps[3].active);
    CHECK (restored.perform.steps[3].offset == -5);
    CHECK (restored.perform.steps[3].velocity == 64);
    CHECK (restored.perform.steps[4].active);
    CHECK (restored.reverb.enabled);
    CHECK (restored.reverb.mix == Catch::Approx (0.4f));

    // Fremder Baum: Standardwerte
    CHECK (LivePlaySettings::fromValueTree (juce::ValueTree ("Other")).source == PlaySource::plugin);
}
