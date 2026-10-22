#include "LivePlaySettings.h"
#include "Text.h"

namespace autosampler
{

namespace ids
{
    static const juce::Identifier livePlay            { "LivePlay" };
    static const juce::Identifier source              { "source" };
    static const juce::Identifier attackMs            { "attackMs" };
    static const juce::Identifier releaseMs           { "releaseMs" };
    static const juce::Identifier velocitySensitivity { "velocitySensitivity" };
    static const juce::Identifier gainDb              { "gainDb" };
    static const juce::Identifier oneShotWithoutLoop  { "oneShotWithoutLoop" };
    static const juce::Identifier pitchBendRange      { "pitchBendRange" };
    static const juce::Identifier maxVoices           { "maxVoices" };
    static const juce::Identifier chord               { "chord" };
    static const juce::Identifier inversion           { "inversion" };
    static const juce::Identifier pattern             { "pattern" };
    static const juce::Identifier bpm                 { "bpm" };
    static const juce::Identifier rate                { "rate" };
    static const juce::Identifier gate                { "gate" };
    static const juce::Identifier latch               { "latch" };
    static const juce::Identifier arpMode             { "arpMode" };
    static const juce::Identifier arpOctaves          { "arpOctaves" };
    static const juce::Identifier sequencerLength     { "sequencerLength" };
    static const juce::Identifier steps               { "steps" };
    static const juce::Identifier reverbEnabled       { "reverbEnabled" };
    static const juce::Identifier reverbMix           { "reverbMix" };
    static const juce::Identifier reverbSize          { "reverbSize" };
    static const juce::Identifier reverbDamping       { "reverbDamping" };
}

namespace
{
    template <typename Enum>
    Enum readEnum (const juce::ValueTree& tree, const juce::Identifier& id, Enum fallback, Enum last)
    {
        return (Enum) juce::jlimit (0, (int) last, (int) tree.getProperty (id, (int) fallback));
    }

    /** Schritte als "a:offset:velocity" durch Leerzeichen getrennt, z. B. "1:0:100 0:7:90". */
    juce::String stepsToString (const PerformSettings& p)
    {
        juce::StringArray parts;

        for (const auto& step : p.steps)
            parts.add (juce::String (step.active ? 1 : 0) + ":" + juce::String (step.offset) + ":" + juce::String (step.velocity));

        return parts.joinIntoString (" ");
    }

    void stepsFromString (const juce::String& text, PerformSettings& p)
    {
        const auto parts = juce::StringArray::fromTokens (text, " ", {});

        for (int i = 0; i < juce::jmin (parts.size(), PerformSettings::maxSteps); ++i)
        {
            const auto values = juce::StringArray::fromTokens (parts[i], ":", {});

            if (values.size() != 3)
                continue;

            auto& step = p.steps[(size_t) i];
            step.active = values[0].getIntValue() != 0;
            step.offset = juce::jlimit (-24, 24, values[1].getIntValue());
            step.velocity = juce::jlimit (1, 127, values[2].getIntValue());
        }
    }
}

juce::Identifier LivePlaySettings::getValueTreeType()
{
    return ids::livePlay;
}

juce::ValueTree LivePlaySettings::toValueTree() const
{
    juce::ValueTree tree (ids::livePlay);
    tree.setProperty (ids::source, (int) source, nullptr);

    tree.setProperty (ids::attackMs, sampler.attackMs, nullptr);
    tree.setProperty (ids::releaseMs, sampler.releaseMs, nullptr);
    tree.setProperty (ids::velocitySensitivity, sampler.velocitySensitivity, nullptr);
    tree.setProperty (ids::gainDb, sampler.gainDb, nullptr);
    tree.setProperty (ids::oneShotWithoutLoop, sampler.oneShotWithoutLoop, nullptr);
    tree.setProperty (ids::pitchBendRange, sampler.pitchBendRange, nullptr);
    tree.setProperty (ids::maxVoices, sampler.maxVoices, nullptr);

    tree.setProperty (ids::chord, (int) perform.chord, nullptr);
    tree.setProperty (ids::inversion, perform.inversion, nullptr);
    tree.setProperty (ids::pattern, (int) perform.pattern, nullptr);
    tree.setProperty (ids::bpm, perform.bpm, nullptr);
    tree.setProperty (ids::rate, (int) perform.rate, nullptr);
    tree.setProperty (ids::gate, perform.gate, nullptr);
    tree.setProperty (ids::latch, perform.latch, nullptr);
    tree.setProperty (ids::arpMode, (int) perform.arpMode, nullptr);
    tree.setProperty (ids::arpOctaves, perform.arpOctaves, nullptr);
    tree.setProperty (ids::sequencerLength, perform.sequencerLength, nullptr);
    tree.setProperty (ids::steps, stepsToString (perform), nullptr);

    tree.setProperty (ids::reverbEnabled, reverb.enabled, nullptr);
    tree.setProperty (ids::reverbMix, reverb.mix, nullptr);
    tree.setProperty (ids::reverbSize, reverb.size, nullptr);
    tree.setProperty (ids::reverbDamping, reverb.damping, nullptr);
    return tree;
}

LivePlaySettings LivePlaySettings::fromValueTree (const juce::ValueTree& tree)
{
    LivePlaySettings s;

    if (! tree.hasType (ids::livePlay))
        return s;

    s.source = readEnum (tree, ids::source, s.source, PlaySource::sampler);

    auto& sm = s.sampler;
    sm.attackMs            = juce::jlimit (0.0f, 5000.0f, (float) tree.getProperty (ids::attackMs, sm.attackMs));
    sm.releaseMs           = juce::jlimit (1.0f, 10000.0f, (float) tree.getProperty (ids::releaseMs, sm.releaseMs));
    sm.velocitySensitivity = juce::jlimit (0.0f, 1.0f, (float) tree.getProperty (ids::velocitySensitivity, sm.velocitySensitivity));
    sm.gainDb              = juce::jlimit (-48.0f, 12.0f, (float) tree.getProperty (ids::gainDb, sm.gainDb));
    sm.oneShotWithoutLoop  = tree.getProperty (ids::oneShotWithoutLoop, sm.oneShotWithoutLoop);
    sm.pitchBendRange      = juce::jlimit (0, 24, (int) tree.getProperty (ids::pitchBendRange, sm.pitchBendRange));
    sm.maxVoices           = juce::jlimit (1, 128, (int) tree.getProperty (ids::maxVoices, sm.maxVoices));

    auto& p = s.perform;
    p.chord           = readEnum (tree, ids::chord, p.chord, ChordType::octave);
    p.inversion       = juce::jlimit (0, 3, (int) tree.getProperty (ids::inversion, p.inversion));
    p.pattern         = readEnum (tree, ids::pattern, p.pattern, PatternMode::sequencer);
    p.bpm             = juce::jlimit (20.0, 300.0, (double) tree.getProperty (ids::bpm, p.bpm));
    p.rate            = readEnum (tree, ids::rate, p.rate, StepRate::thirtySecond);
    p.gate            = juce::jlimit (0.05f, 1.0f, (float) tree.getProperty (ids::gate, p.gate));
    p.latch           = tree.getProperty (ids::latch, p.latch);
    p.arpMode         = readEnum (tree, ids::arpMode, p.arpMode, ArpMode::asPlayed);
    p.arpOctaves      = juce::jlimit (1, 4, (int) tree.getProperty (ids::arpOctaves, p.arpOctaves));
    p.sequencerLength = juce::jlimit (1, PerformSettings::maxSteps, (int) tree.getProperty (ids::sequencerLength, p.sequencerLength));
    stepsFromString (tree[ids::steps].toString(), p);

    auto& r = s.reverb;
    r.enabled = tree.getProperty (ids::reverbEnabled, r.enabled);
    r.mix     = juce::jlimit (0.0f, 1.0f, (float) tree.getProperty (ids::reverbMix, r.mix));
    r.size    = juce::jlimit (0.0f, 1.0f, (float) tree.getProperty (ids::reverbSize, r.size));
    r.damping = juce::jlimit (0.0f, 1.0f, (float) tree.getProperty (ids::reverbDamping, r.damping));
    return s;
}

//==============================================================================
ChordIntervals getChordIntervals (ChordType type, int inversion) noexcept
{
    const auto make = [] (std::initializer_list<int> list)
    {
        ChordIntervals result;

        for (const auto value : list)
            result.values[(size_t) result.count++] = value;

        return result;
    };

    ChordIntervals chord;

    switch (type)
    {
        case ChordType::off:        return make ({ 0 });
        case ChordType::major:      chord = make ({ 0, 4, 7 }); break;
        case ChordType::minor:      chord = make ({ 0, 3, 7 }); break;
        case ChordType::dominant7:  chord = make ({ 0, 4, 7, 10 }); break;
        case ChordType::major7:     chord = make ({ 0, 4, 7, 11 }); break;
        case ChordType::minor7:     chord = make ({ 0, 3, 7, 10 }); break;
        case ChordType::sus2:       chord = make ({ 0, 2, 7 }); break;
        case ChordType::sus4:       chord = make ({ 0, 5, 7 }); break;
        case ChordType::diminished: chord = make ({ 0, 3, 6 }); break;
        case ChordType::augmented:  chord = make ({ 0, 4, 8 }); break;
        case ChordType::power:      chord = make ({ 0, 7, 12 }); break;
        case ChordType::octave:     chord = make ({ 0, 12 }); break;
    }

    // Umkehrung: der tiefste Ton wandert jeweils eine Oktave nach oben
    for (int i = 0; i < juce::jlimit (0, chord.count - 1, inversion); ++i)
    {
        const auto lowest = chord.values[0];
        std::rotate (chord.values.begin(), chord.values.begin() + 1, chord.values.begin() + chord.count);
        chord.values[(size_t) chord.count - 1] = lowest + 12;
    }

    return chord;
}

double getBeatsPerStep (StepRate rate) noexcept
{
    switch (rate)
    {
        case StepRate::quarter:          return 1.0;
        case StepRate::eighth:           return 0.5;
        case StepRate::eighthTriplet:    return 1.0 / 3.0;
        case StepRate::sixteenth:        return 0.25;
        case StepRate::sixteenthTriplet: return 1.0 / 6.0;
        case StepRate::thirtySecond:     return 0.125;
    }

    return 0.25;
}

juce::String getDisplayName (ChordType type)
{
    switch (type)
    {
        case ChordType::off:        return "Off";
        case ChordType::major:      return "Major";
        case ChordType::minor:      return "Minor";
        case ChordType::dominant7:  return "7";
        case ChordType::major7:     return "Maj7";
        case ChordType::minor7:     return "m7";
        case ChordType::sus2:       return "Sus2";
        case ChordType::sus4:       return "Sus4";
        case ChordType::diminished: return "Diminished";
        case ChordType::augmented:  return "Augmented";
        case ChordType::power:      return "Power (1-5-8)";
        case ChordType::octave:     return "Octave";
    }

    return {};
}

juce::String getDisplayName (PatternMode mode)
{
    switch (mode)
    {
        case PatternMode::off:       return "Off";
        case PatternMode::arp:       return "Arpeggiator";
        case PatternMode::sequencer: return "Step-Sequencer";
    }

    return {};
}

juce::String getDisplayName (ArpMode mode)
{
    switch (mode)
    {
        case ArpMode::up:       return "Up";
        case ArpMode::down:     return "Down";
        case ArpMode::upDown:   return "Up/Down";
        case ArpMode::random:   return "Random";
        case ArpMode::asPlayed: return "As played";
    }

    return {};
}

juce::String getDisplayName (StepRate rate)
{
    switch (rate)
    {
        case StepRate::quarter:          return "1/4";
        case StepRate::eighth:           return "1/8";
        case StepRate::eighthTriplet:    return "1/8 T";
        case StepRate::sixteenth:        return "1/16";
        case StepRate::sixteenthTriplet: return "1/16 T";
        case StepRate::thirtySecond:     return "1/32";
    }

    return {};
}

} // namespace autosampler
