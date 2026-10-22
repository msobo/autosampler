#pragma once

#include <juce_core/juce_core.h>

namespace autosampler
{

/** Pegel eines Samples für das Zone Smoothing. */
struct ZoneLevel
{
    int note = 60;
    int velocity = 127;
    float levelDb = -100.0f;    ///< measureLevelDb()
};

/** Größte Korrektur des Zone Smoothing in dB (in beide Richtungen). */
constexpr float maxZoneSmoothingDb = 6.0f;

/** Zone Smoothing: gleicht Pegelsprünge zwischen benachbarten Noten an.

    Pro Velocity-Layer (Rang der Velocity innerhalb der Note) wird der mittlere
    Pegel jeder Note (über ihre Round-Robin-Varianten) über die Nachbarnoten
    geglättet (Gewichte 1-2-1, an den Rändern die Ausgleichsgerade durch drei
    Noten, ein linearer Verlauf bleibt also unverändert). Jede Note bekommt
    amount · (geglättet − gemessen), höchstens ±maxZoneSmoothingDb; Varianten
    einer Note dieselbe Korrektur, ihre Unterschiede bleiben erhalten.
    Layer mit weniger als drei Noten und stille Samples bleiben bei 0 dB.

    Liefert die Korrektur in dB pro Eintrag (gleiche Reihenfolge). */
std::vector<float> computeZoneSmoothing (const std::vector<ZoneLevel>&, float amount);

} // namespace autosampler
