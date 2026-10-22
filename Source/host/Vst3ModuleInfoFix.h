#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace autosampler
{

/** Korrigiert VST3-Beschreibungen, die JUCE aus einer veralteten moduleinfo.json
    übernommen hat.

    JUCE liest beim Scan nur die moduleinfo.json, beim Laden aber die echte
    Factory im Binary. Stimmen die Class-IDs nicht überein (z. B. Korg TRITON
    1.4.3), lässt sich das Plugin nicht laden ("Unable to load VST-3 plug-in
    file"). Diese Funktion liest die Klassen aus dem Binary und setzt die IDs
    neu, die JUCE beim Laden erwartet.

    Lädt das Plugin-Binary, darf daher nur im Scan-Subprozess laufen.
    Gibt die Anzahl korrigierter Beschreibungen zurück.
*/
int fixStaleVst3ModuleInfo (juce::OwnedArray<juce::PluginDescription>& descriptions,
                            const juce::String& bundlePath);

} // namespace autosampler
