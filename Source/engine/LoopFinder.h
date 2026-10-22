#pragma once

#include "LoopCrossfade.h"

#include <juce_data_structures/juce_data_structures.h>

namespace autosampler
{

/** Loop-Algorithmen, benannt nach dem Einsatzzweck (siehe LoopAlgorithmen.md). */
enum class LoopAlgorithm
{
    pad,        ///< Pads, sich entwickelnde Klänge: spektraler Fingerprint, langer Crossfade
    universal,  ///< ohne klaren Grundton, Rückfallebene: Nulldurchgänge + Kreuzkorrelation
    tonal       ///< statische Töne: Grundton (YIN), Loop als Vielfaches der Periode
};

juce::String toString (LoopAlgorithm);
std::optional<LoopAlgorithm> parseLoopAlgorithm (const juce::String&);
juce::String getDisplayName (LoopAlgorithm);

/** Einstellungen der Loop-Suche (pro Session gespeichert). */
struct LoopSearchSettings
{
    LoopAlgorithm algorithm = LoopAlgorithm::pad;
    CrossfadeMode crossfadeMode = CrossfadeMode::spectral;
    double crossfadeSeconds = 0.05;     ///< Universal; Tonal höchstens zwei Perioden; Pad mindestens so lang
    float padAmount = 0.5f;             ///< 0–1: Pad-Crossfade als Anteil der halben Loop-Länge
    double minLoopSeconds = 0.1;

    juce::ValueTree toValueTree() const;
    static LoopSearchSettings fromValueTree (const juce::ValueTree&);
    static juce::Identifier getValueTreeType();
};

/** Zusatzwissen über das Sample, das die Suche eingrenzt. */
struct LoopHints
{
    int note = -1;              ///< MIDI-Note der Aufnahme (Tonal: Suchbereich des Grundtons)
    int sustainEndFrame = -1;   ///< Ende der Haltezeit (Note-Off) im Sample; -1 = unbekannt
};

struct LoopResult
{
    bool found = false;
    LoopAlgorithm algorithm = LoopAlgorithm::pad;
    int start = 0, end = 0;     ///< Frames, end exklusiv
    int crossfade = 0;          ///< Frames vor E
    float score = 0.0f;         ///< 0–100, siehe scoreLoop()
    juce::String message;       ///< Hinweis oder Grund, warum nichts gefunden wurde
};

/** Sucht einen Loop im tragenden Teil des Samples (nach dem Anschlag, vor dem Note-Off).
    Rechnet nur auf den übergebenen Daten und ist damit auf jedem Thread nutzbar. */
LoopResult findLoop (const juce::AudioBuffer<float>&, double sampleRate,
                     const LoopSearchSettings&, const LoopHints& = {});

/** Qualität eines Loops, 0–100: Übergang E → S nach dem Crossfade
    (gewichtete Kreuzkorrelation der Frames vor E gegen die Frames vor S im Original),
    spektrale Ähnlichkeit von Loop-Anfang und -Ende und deren Pegelverhältnis. */
float scoreLoop (const juce::AudioBuffer<float>&, double sampleRate,
                 int loopStart, int loopEnd, int crossfade, CrossfadeMode);

/** Grundperiode in Frames (YIN mit parabolischer Interpolation, also mit
    Sub-Sample-Genauigkeit); 0, wenn kein Grundton gefunden wird. */
double detectPitchPeriod (const float* data, int numSamples, double sampleRate,
                          double minFrequency = 25.0, double maxFrequency = 4000.0);

} // namespace autosampler
