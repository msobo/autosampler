#pragma once

#include "ExportSettings.h"
#include "SamplerInstrument.h"

namespace autosampler
{

class Session;

/** Ein Sample im Export: Quelle, Bereich, Round Robin und Dateiname. */
struct ExportSample
{
    SamplerZoneSource source;
    ZoneRange range;
    int velocityLayer = 0;          ///< Index der Velocity innerhalb der Note (0 = leiseste)
    int roundRobinIndex = 0;        ///< Position innerhalb gleicher Note und Velocity
    int roundRobinCount = 1;
    float gainDb = 0.0f;            ///< Zone Smoothing (runExport)
    juce::String fileName;          ///< relativ zum Sample-Ordner des Ziels
};

/** Audio eines Samples nach der Aufbereitung. */
struct PreparedAudio
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 48000.0;
    std::optional<juce::Range<int>> loop;   ///< Frames, Ende exklusiv
    float sourceLevelDb = -100.0f;          ///< measureLevelDb() vor der Aufbereitung (Zone Smoothing)
};

/** Ein geschriebenes Sample, wie es in der Mapping-Datei steht. */
struct ExportedRegion
{
    ExportSample sample;
    std::optional<juce::Range<int>> loop;   ///< Frames im geschriebenen Sample, Ende exklusiv
    int numFrames = 0;
};

struct ExportResult
{
    juce::File output;              ///< Ordner oder .zip
    int numWritten = 0;
    juce::StringArray warnings;
    juce::String error;
    bool cancelled = false;
};

struct ExportCallbacks
{
    std::function<void (double progress)> onProgress;     ///< 0–1, vom Export-Thread
    std::function<bool()> shouldCancel;
};

//==============================================================================
/** Quellen aller Samples der Session, die exportiert werden sollen. */
std::vector<SamplerZoneSource> getExportSources (const Session&);

/** Wendet die Grenzen des Ziels an (Tonverk: 48 kHz, höchstens 24 Bit) und
    nennt die Änderungen in warnings. */
ExportSettings resolveExportSettings (const ExportSettings&, juce::StringArray& warnings);

/** Sortiert nach Note, Velocity, Round-Robin-Variante und Reihenfolge der Session, berechnet Bereiche
    wie der Sampler (computeZoneRanges), Round Robin und Dateinamen. */
std::vector<ExportSample> planExport (const std::vector<SamplerZoneSource>&, ExportTarget,
                                      const juce::String& instrumentName);

/** Dateiname nach Konvention des Ziels, z. B. "Pad_060_C3_v127.wav" (WAV, SFZ)
    oder "Pad-001-060-c3.wav" (Tonverk, Velocity-Layer und Note). */
juce::String makeExportFileName (ExportTarget, const juce::String& instrumentName, int note, int velocity,
                                 int velocityLayer, int roundRobinIndex, int roundRobinCount);

/** Liest ein Sample und bereitet es auf: Loop-Crossfade rendern, Tail bzw.
    Loop Only, Mono, Sample-Rate. Ohne Normalisierung. Liefert eine Fehlermeldung oder "". */
juce::String prepareExportAudio (const SamplerZoneSource&, const ExportSettings&, PreparedAudio&);

/** SFZ-Text (Golden-File-Tests). Pfade relativ zu default_path=samples/. */
juce::String makeSfz (const std::vector<ExportedRegion>&, const ExportSettings&, const juce::String& instrumentName);

/** Elektron-Mapping (.elmulti, TOML-artig) für den Tonverk. */
juce::String makeElmulti (const std::vector<ExportedRegion>&, const ExportSettings&, const juce::String& instrumentName);

/** Exportiert die Quellen nach destinationParent/<Name> (bzw. <Name>.zip).
    Synchron, auf jedem Thread nutzbar. Bei Abbruch oder Fehler wird die
    unvollständige Ausgabe gelöscht. */
ExportResult runExport (const std::vector<SamplerZoneSource>&, const ExportSettings&,
                        const juce::String& instrumentName, const juce::File& destinationParent,
                        const ExportCallbacks& = {});

} // namespace autosampler
