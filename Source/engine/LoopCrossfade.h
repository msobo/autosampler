#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace autosampler
{

/** Form des Loop-Crossfades (Blend 1–4). */
enum class CrossfadeMode
{
    linear,     ///< Blend 1: linear
    equalPower, ///< Blend 2: gleiche Leistung (Sinus/Kosinus)
    sCurve,     ///< Blend 3: S-Kurve (Smoothstep)
    spectral    ///< Blend 4: pro Frequenzband, tiefe Bänder länger, Pegel nach Korrelation
};

juce::String toString (CrossfadeMode);
CrossfadeMode parseCrossfadeMode (const juce::String&, CrossfadeMode fallback = CrossfadeMode::equalPower);
juce::String getDisplayName (CrossfadeMode);

/** Gewichte bei t in [0, 1]: Ausblenden des Loop-Endes, Einblenden des Materials vor S. */
struct CrossfadeGains
{
    float fadeOut = 1.0f, fadeIn = 0.0f;
};

CrossfadeGains getCrossfadeGains (CrossfadeMode, float t) noexcept;

/** Längster möglicher Crossfade: das Material vor S muss existieren
    und der Crossfade passt in den Loop. */
int getMaxCrossfade (int loopStart, int loopEnd) noexcept;

/** Rendert den Loop-Crossfade in den Puffer: Die letzten length Frames vor E
    werden in die length Frames vor S überblendet, sodass der Sprung E → S
    nahtlos ist. Verändert nur [E - length, E); length wird begrenzt.
    Der spektrale Modus teilt in drei Bänder (Grenzen 300 Hz und 3 kHz,
    nullphasig und exakt rekonstruierend) mit 100 %, 60 % und 30 % der Länge;
    pro Band wird eine S-Kurve nach der Korrelation beider Seiten normiert
    (korreliert: Summe 1, unkorreliert: gleiche Leistung). */
void applyLoopCrossfade (juce::AudioBuffer<float>&, int loopStart, int loopEnd, int length,
                         CrossfadeMode, double sampleRate);

} // namespace autosampler
