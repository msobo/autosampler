#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace autosampler
{

/** Exportziele. */
enum class ExportTarget
{
    wav,        ///< WAV-Dateien mit smpl- und inst-Chunk
    sfz,        ///< SFZ-Datei mit Samples in samples/
    tonverk     ///< Elektron Tonverk: .elmulti mit WAVs im selben Ordner
};

/** Was nach dem Loop-Ende mit dem Sample passiert. */
enum class TailMode
{
    original,       ///< Ganzes Sample (Release-Fahne bleibt)
    cutAfterLoop    ///< Bei Loop-Ende + 32 Frames abschneiden
};

juce::String toString (ExportTarget);
std::optional<ExportTarget> parseExportTarget (const juce::String&);
juce::String getDisplayName (ExportTarget);

juce::String toString (TailMode);
std::optional<TailMode> parseTailMode (const juce::String&);
juce::String getDisplayName (TailMode);

/** Einstellungen des Exports (pro Session gespeichert). */
struct ExportSettings
{
    static constexpr int tailAfterLoop = 32;

    ExportTarget target = ExportTarget::tonverk;
    juce::String name;                  ///< Leer: Name der Session

    double sampleRate = 0.0;            ///< 0: wie aufgenommen (Tonverk: immer 48 kHz)
    int bitDepth = 24;                  ///< 16, 24 oder 32 (Float; Tonverk: höchstens 24)

    bool normalize = false;             ///< Gemeinsame Verstärkung für alle Samples
    float peakDb = -1.0f;               ///< Zielpegel der lautesten Spitze, -6 … -1 dBFS
    bool mono = false;

    TailMode tail = TailMode::original;
    bool loopOnly = false;              ///< Nur [S, E) exportieren (mit Crossfade)
    bool loopOnRelease = true;          ///< Loop läuft im Release weiter (sonst bis Note-Off)
    bool oneShotWithoutLoop = true;     ///< Ohne Loop: Note-Off ignorieren (SFZ one_shot)

    // Hüllkurve und Velocity (SFZ), aus dem Play-Tab übernehmbar
    float attackMs = 2.0f;
    float releaseMs = 300.0f;
    float velocitySensitivity = 0.5f;   ///< 0–1

    float zoneSmoothing = 0.0f;         ///< 0–1, gleicht Pegel benachbarter Noten an (auch im Sampler)

    bool zip = false;                   ///< Ergebnis als .zip statt als Ordner

    juce::ValueTree toValueTree() const;
    static ExportSettings fromValueTree (const juce::ValueTree&);
    static juce::Identifier getValueTreeType();
};

} // namespace autosampler
