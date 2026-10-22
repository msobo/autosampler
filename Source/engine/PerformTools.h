#pragma once

#include "LivePlaySettings.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace autosampler
{

/** Chord-Player: erweitert jede Note zu einem Akkord. Merkt sich pro Taste,
    welche Noten sie ausgelöst hat, damit Note-Offs auch nach einem Wechsel des
    Akkords passen. Überlappende Akkordtöne werden gezählt (nur ein Note-On und
    ein Note-Off pro Ton). Audio-Thread, alloziert nicht. */
class ChordProcessor
{
public:
    void setChord (ChordType, int inversion);

    void process (const juce::MidiBuffer& input, juce::MidiBuffer& output);

    /** Note-Offs für alle klingenden Töne an samplePosition. */
    void reset (juce::MidiBuffer& output, int samplePosition);

private:
    static constexpr int maxChordNotes = 8;

    struct Mapping
    {
        std::array<juce::int8, maxChordNotes> notes {};
        int count = 0;
    };

    void noteOn (int channel, int note, juce::uint8 velocity, juce::MidiBuffer&, int position);
    void noteOff (int channel, int note, juce::MidiBuffer&, int position);

    std::array<int, maxChordNotes> intervals { 0 };
    int numIntervals = 1;
    std::array<Mapping, 16 * 128> mappings {};          // [Kanal][Taste]
    std::array<juce::uint8, 16 * 128> soundingCount {};
};

//==============================================================================
/** Arpeggiator und Step-Sequencer mit internem Takt (BPM aus den Einstellungen).

    - Aus: Noten gehen direkt durch, gehaltene Tasten werden trotzdem mitgezählt.
    - Arp: spielt die gehaltenen Töne (über 1–4 Oktaven) nacheinander.
    - Sequencer: spielt die Schritte, transponiert um die zuletzt gedrückte Taste
      relativ zu C3 (Offset 0 = die Taste selbst).
    - Latch: Töne bleiben nach dem Loslassen stehen, bis eine neue Taste gedrückt wird.

    Startet mit der ersten Taste sofort auf Schritt 1. Andere MIDI-Daten gehen durch.
    Audio-Thread, alloziert nicht. */
class PatternPlayer
{
public:
    void prepare (double sampleRate);

    /** Bei einem Wechsel des Modus werden klingende Noten in output beendet. */
    void setSettings (const PerformSettings&, juce::MidiBuffer& output);

    void process (const juce::MidiBuffer& input, juce::MidiBuffer& output, int numSamples);

    /** Beendet alle Noten und vergisst gehaltene Töne (Panic). */
    void reset (juce::MidiBuffer& output, int samplePosition);

    /** Zuletzt gespielter Schritt (Sequencer) bzw. Ton (Arp), -1 wenn still. */
    int getCurrentStep() const noexcept                 { return currentStep.load(); }

private:
    struct HeldNote
    {
        int note = 0, channel = 1, velocity = 100;
        juce::uint32 order = 0;
        bool down = false;
    };

    struct PendingOff
    {
        int note = 0, channel = 1;
        double time = 0.0;
    };

    void handleNote (const juce::MidiMessage&, juce::MidiBuffer& output, int position);
    void advance (juce::MidiBuffer& output, int from, int to);
    void playStep (juce::MidiBuffer& output, int position);
    void flushPendingOffs (juce::MidiBuffer& output, int position);
    void removeHeld (int index);
    void stop (juce::MidiBuffer& output, int position);
    double getStepLength() const noexcept;

    double sampleRate = 48000.0;
    PerformSettings settings;
    juce::Random random;

    std::array<HeldNote, 128> held {};
    int numHeld = 0;
    juce::uint32 orderCounter = 0;

    std::array<PendingOff, 128> pending {};
    int numPending = 0;

    bool running = false;
    double nextStepTime = 0.0;      // relativ zum Blockanfang
    int stepCounter = 0;
    std::atomic<int> currentStep { -1 };
};

} // namespace autosampler
