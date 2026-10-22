#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <optional>

namespace autosampler
{

enum class RenderMode
{
    automatic,  ///< Offline, bei Stille automatisch Echtzeit
    offline,    ///< Immer offline (schneller als Echtzeit)
    realtime    ///< Immer in Echtzeit über das Audio-Gerät
};

/** Woher die Round-Robin-Varianten kommen. */
enum class RoundRobinSource
{
    off,        ///< Eine Aufnahme pro Note und Velocity
    real,       ///< Das Instrument variiert jeden Anschlag selbst
    midiCC      ///< Ein Parameter (MIDI CC oder Plugin-Parameter) wird über Steps variiert
};

/** Wie Varianten und Velocities zusammenkommen. */
enum class RoundRobinMode
{
    independent,    ///< Jede Velocity × jede Variante
    linked          ///< Variante n gehört zu Velocity-Layer n (für Sampler ohne Round Robin)
};

/** Was bei RoundRobinSource::midiCC variiert wird. */
enum class VariationTarget
{
    midiCC,     ///< Controller-Nachricht direkt vor dem Note-On
    parameter   ///< Plugin-Parameter, gesetzt vor dem Ausklingen
};

/** Eine einzelne Aufnahme: Note, Velocity und Round-Robin-Variante. */
struct SampleRequest
{
    int note = 60;
    int velocity = 127;
    int roundRobin = 0;         ///< Variante 0 … roundRobinCount - 1
    int roundRobinCount = 1;    ///< Varianten pro Note und Velocity (Linked: 1)
    int variation = -1;         ///< Wert 0–127 für CC/Parameter, -1 = keine Variation

    bool operator== (const SampleRequest& other) const noexcept
    {
        return note == other.note && velocity == other.velocity && roundRobin == other.roundRobin
            && roundRobinCount == other.roundRobinCount && variation == other.variation;
    }
};

/** Eine geschriebene Aufnahme, wie sie ein Sampling-Lauf meldet. */
struct RecordedSample
{
    juce::File file;
    SampleRequest request;
    double sampleRate = 48000.0;
    int numFrames = 0;
    int numChannels = 2;
};

/** Alle Einstellungen eines Sampling-Laufs. */
struct SamplingSettings
{
    // Notenbereich
    int lowNote = 36;
    int highNote = 96;
    int noteStep = 3;
    bool useCustomNotes = false;
    juce::String customNotes;       ///< z. B. "C2, E2, G2, 60"

    juce::String velocities = "127";    ///< z. B. "30, 60, 90, 110"
    int midiChannel = 1;

    // Round Robin
    static constexpr int maxRoundRobinSteps = 16;

    RoundRobinSource roundRobinSource = RoundRobinSource::off;
    RoundRobinMode roundRobinMode = RoundRobinMode::independent;    ///< nur bei MIDI CC
    int roundRobinSteps = 3;            ///< 2–16 (Independent); Linked: Anzahl der Velocities
    VariationTarget variationTarget = VariationTarget::midiCC;
    int variationController = 74;       ///< CC-Nummer 0–127
    juce::String variationParameterId;  ///< Kennung des Plugin-Parameters
    juce::String variationParameterName;
    int variationFrom = 32;             ///< 0–127 (Parameter: 0–100 %)
    int variationTo = 45;

    // Pegel der Aufnahme
    float gainDb = 0.0f;                ///< -24 … +12 dB
    float pan = 0.0f;                   ///< -1 (links) … +1 (rechts)

    // Zeiten
    double holdSeconds = 2.0;
    double maxTailSeconds = 10.0;

    // Render
    RenderMode renderMode = RenderMode::automatic;
    double sampleRate = 48000.0;
    int bitDepth = 24;                  ///< 16, 24 oder 32 (Float)
    bool restoreStatePerSample = true;

    // Nachbearbeitung
    bool trimStart = true;
    float startThresholdDb = -60.0f;
    float tailThresholdDb = -70.0f;
    double preRollMs = 2.0;
    double fadeOutMs = 20.0;

    // Nach dem Lauf
    bool autoFindLoops = false;         ///< Loops für alle Samples suchen

    // Ziel
    juce::File outputFolder;
    juce::String name;                  ///< Leer: Name des Plugins

    juce::ValueTree toValueTree() const;
    static SamplingSettings fromValueTree (const juce::ValueTree&);
    static juce::Identifier getValueTreeType();
};

/** Notenname mit C3 = 60 (Konvention der meisten Sampler und DAWs). */
juce::String getNoteName (int note);

/** Liest "C3", "F#-1", "Db4" oder eine MIDI-Nummer "60". */
std::optional<int> parseNoteName (const juce::String& text);

/** Liest eine komma- oder leerzeichengetrennte Notenliste.
    Ergebnis sortiert und ohne Duplikate; bei Fehlern ist error gesetzt. */
juce::Array<int> parseNoteList (const juce::String& text, juce::String& error);

/** Liest eine Velocity-Liste (1-127), sortiert und ohne Duplikate. */
juce::Array<int> parseVelocityList (const juce::String& text, juce::String& error);

/** Alle Noten des Laufs (Raster oder Custom Notes). */
juce::Array<int> getNotes (const SamplingSettings&, juce::String& error);

/** Anzahl der Round-Robin-Varianten pro Note und Velocity (1 ohne Round Robin oder bei Linked). */
int getRoundRobinCount (const SamplingSettings&);

/** Werte der Variation für count Steps, gleichmäßig von from bis to (inklusive). */
juce::Array<int> getVariationValues (int from, int to, int count);

/** Noten × Velocities × Varianten, nach Note, Velocity und Variante sortiert.
    Linked: eine Aufnahme pro Velocity, Velocity-Layer n bekommt den n-ten Variationswert. */
juce::Array<SampleRequest> buildSampleList (const SamplingSettings&, juce::String& error);

juce::String toString (RoundRobinSource);
juce::String toString (RoundRobinMode);
std::optional<RoundRobinSource> parseRoundRobinSource (const juce::String&);
std::optional<RoundRobinMode> parseRoundRobinMode (const juce::String&);

} // namespace autosampler
