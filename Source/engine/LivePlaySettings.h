#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>

namespace autosampler
{

/** Was das Keyboard und MIDI-In spielen: das Plugin oder die gesampelten Samples (A/B). */
enum class PlaySource
{
    plugin,
    sampler
};

/** Wiedergabe der Samples. */
struct SamplerSettings
{
    float attackMs = 2.0f;
    float releaseMs = 300.0f;
    float velocitySensitivity = 0.5f;   ///< 0–1: Lautstärke folgt der Velocity innerhalb eines Layers
    float gainDb = 0.0f;
    bool oneShotWithoutLoop = true;     ///< Ohne Loop: ganzes Sample spielen, Note-Off ignorieren
    int pitchBendRange = 2;             ///< Halbtöne
    int maxVoices = 64;
};

enum class ChordType
{
    off, major, minor, dominant7, major7, minor7, sus2, sus4, diminished, augmented, power, octave
};

enum class PatternMode
{
    off,        ///< Noten direkt durchreichen
    arp,
    sequencer
};

enum class ArpMode
{
    up, down, upDown, random, asPlayed
};

/** Notenwerte für Arp und Sequencer. */
enum class StepRate
{
    quarter, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtySecond
};

struct SequencerStep
{
    bool active = true;
    int offset = 0;         ///< Halbtöne relativ zur gespielten Taste (C3 = keine Transposition)
    int velocity = 100;
};

/** Chord-Player, Arpeggiator und Step-Sequencer. */
struct PerformSettings
{
    static constexpr int maxSteps = 16;

    ChordType chord = ChordType::off;
    int inversion = 0;                  ///< 0–3

    PatternMode pattern = PatternMode::off;
    double bpm = 120.0;
    StepRate rate = StepRate::sixteenth;
    float gate = 0.5f;                  ///< Anteil der Schrittlänge, 0,05–1
    bool latch = false;

    ArpMode arpMode = ArpMode::up;
    int arpOctaves = 1;                 ///< 1–4

    int sequencerLength = 16;
    std::array<SequencerStep, maxSteps> steps {};
};

/** Hall nur zum Abhören, nie im Export. */
struct ReverbSettings
{
    bool enabled = false;
    float mix = 0.25f;
    float size = 0.6f;
    float damping = 0.5f;
};

/** Alle Einstellungen der Live-Wiedergabe. Kopierbar ohne Allokation,
    damit sie per SpinLock an den Audio-Thread gehen kann. */
struct LivePlaySettings
{
    PlaySource source = PlaySource::plugin;
    SamplerSettings sampler;
    PerformSettings perform;
    ReverbSettings reverb;

    juce::ValueTree toValueTree() const;
    static LivePlaySettings fromValueTree (const juce::ValueTree&);
    static juce::Identifier getValueTreeType();
};

/** Intervalle eines Akkords relativ zum Grundton; bei off nur der Grundton. */
struct ChordIntervals
{
    std::array<int, 8> values {};
    int count = 0;
};

/** Alloziert nicht (auch auf dem Audio-Thread nutzbar). */
ChordIntervals getChordIntervals (ChordType, int inversion = 0) noexcept;

/** Länge eines Schritts in Vierteln (Beats). */
double getBeatsPerStep (StepRate) noexcept;

juce::String getDisplayName (ChordType);
juce::String getDisplayName (PatternMode);
juce::String getDisplayName (ArpMode);
juce::String getDisplayName (StepRate);

} // namespace autosampler
