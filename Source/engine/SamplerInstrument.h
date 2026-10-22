#pragma once

#include "LoopCrossfade.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <map>
#include <mutex>
#include <set>

namespace autosampler
{

/** Tasten- und Velocity-Bereich einer Zone (inklusive Grenzen). */
struct ZoneRange
{
    int lowKey = 0, highKey = 127;
    int lowVelocity = 1, highVelocity = 127;

    bool contains (int key, int velocity) const noexcept
    {
        return key >= lowKey && key <= highKey && velocity >= lowVelocity && velocity <= highVelocity;
    }

    bool operator== (const ZoneRange& other) const noexcept
    {
        return lowKey == other.lowKey && highKey == other.highKey
            && lowVelocity == other.lowVelocity && highVelocity == other.highVelocity;
    }
};

/** Verteilt Samples (Note, Velocity) auf Tasten und Velocities:
    Jede Note deckt die Tasten bis zur Mitte zwischen ihr und den Nachbarnoten ab
    (die tiefste bis 0, die höchste bis 127). Pro Note deckt jede Velocity den
    Bereich über der nächstkleineren ab, die höchste bis 127.
    Gleiche Note und Velocity ergeben gleiche Bereiche (Round Robin). */
std::vector<ZoneRange> computeZoneRanges (const std::vector<std::pair<int, int>>& noteVelocityPairs);

/** Eine spielbare Zone: Audio mit fertig gerendertem Loop-Crossfade. */
struct SamplerZone
{
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    double sampleRate = 48000.0;
    int rootNote = 60;
    int velocity = 127;         ///< Velocity der Aufnahme
    ZoneRange range;
    bool loop = false;
    int loopStart = 0, loopEnd = 0;     ///< Frames, loopEnd exklusiv; Audio endet bei loopEnd
    float gain = 1.0f;                  ///< Zone Smoothing
    juce::int64 id = 0;                 ///< siehe makeSampleId()
};

/** Kennung eines Samples für die Hörprüfung, aus dem Dateinamen der Session. */
juce::int64 makeSampleId (const juce::String& fileName);

/** Unveränderliches Instrument; wird im Hintergrund gebaut und dann an den
    Audio-Thread übergeben. Gruppen fassen Zonen mit gleichen Bereichen zusammen
    (Round Robin), die Tabelle bildet Taste × Velocity auf eine Gruppe ab. */
class SamplerInstrument
{
public:
    explicit SamplerInstrument (std::vector<SamplerZone>);

    const std::vector<SamplerZone>& getZones() const noexcept               { return zones; }
    int getNumGroups() const noexcept                                       { return (int) groups.size(); }
    const std::vector<int>& getGroup (int index) const noexcept             { return groups[(size_t) index]; }

    /** Gruppe für Taste und Velocity (1–127), -1 wenn keine passt. */
    int findGroup (int key, int velocity) const noexcept;

    /** Zone mit dieser Kennung, -1 wenn es keine gibt. */
    int findZone (juce::int64 id) const noexcept;

private:
    std::vector<SamplerZone> zones;
    std::vector<std::vector<int>> groups;
    std::array<std::array<juce::int16, 128>, 128> lookup {};    // [Taste][Velocity]
};

//==============================================================================
/** Was der Bau einer Zone über ein Sample der Session wissen muss. */
struct SamplerZoneSource
{
    juce::File file;
    juce::int64 id = 0;
    int note = 60;
    int velocity = 127;
    int roundRobin = 0;         ///< Reihenfolge innerhalb gleicher Note und Velocity
    bool loop = false;
    int loopStart = 0, loopEnd = 0, crossfade = 0;
    CrossfadeMode crossfadeMode = CrossfadeMode::spectral;

    bool operator== (const SamplerZoneSource&) const = default;
};

/** Cache der Rohdaten und der gerenderten Loops über mehrere Bauvorgänge, damit
    eine Änderung an einem Loop nicht alle Dateien neu lädt. Thread-sicher. */
class SamplerAudioCache
{
public:
    struct Loaded
    {
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        double sampleRate = 0.0;
        bool loopRendered = false;      ///< Kopie bis E mit Crossfade
        float levelDb = -100.0f;        ///< measureLevelDb() der Rohdaten
        juce::String error;
    };

    /** Rohdaten einer Datei (lädt bei Bedarf). */
    Loaded getRaw (const juce::File&);

    /** Audio für die Wiedergabe: ohne Loop die Rohdaten, mit Loop eine Kopie bis E
        mit gerendertem Crossfade. */
    Loaded getPlayable (const SamplerZoneSource&);

    /** Entfernt alles, was nicht zu diesen Quellen gehört. */
    void retainOnly (const std::vector<SamplerZoneSource>&);

    void clear();

private:
    struct LoopKey
    {
        juce::String path;
        int start = 0, end = 0, crossfade = 0;
        CrossfadeMode mode = CrossfadeMode::spectral;

        auto tie() const { return std::tie (path, start, end, crossfade, mode); }
        bool operator< (const LoopKey& other) const { return tie() < other.tie(); }
    };

    static LoopKey makeKey (const SamplerZoneSource&);

    std::mutex mutex;
    std::map<juce::String, Loaded> raw;
    std::map<LoopKey, Loaded> looped;
};

/** Baut ein Instrument aus den Quellen. Nicht ladbare Dateien werden
    übersprungen und in errors gezählt. Innerhalb gleicher Note und Velocity
    folgen die Zonen der Round-Robin-Reihenfolge. zoneSmoothing (0–1) gleicht
    die Pegel benachbarter Noten an (computeZoneSmoothing()). */
std::shared_ptr<const SamplerInstrument> buildSamplerInstrument (const std::vector<SamplerZoneSource>&,
                                                                 SamplerAudioCache&, juce::StringArray& errors,
                                                                 float zoneSmoothing = 0.0f);

} // namespace autosampler
