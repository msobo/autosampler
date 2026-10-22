#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace autosampler
{

/** Bringt ein Instrument in ein spielbares Layout: Eingänge aus, Haupt-
    ausgang Stereo (sonst Mono, sonst alle Busse an).

    Nötig, weil manche Plugins (z. B. Dexed) ihren Ausgang standardmäßig
    deaktivieren. Ohne diese Korrektur liefert AudioProcessorPlayer bei einem
    Gerät mit Eingängen 0 Ausgangskanäle, und das Plugin bleibt stumm.
    Gibt die Anzahl der Ausgangskanäle zurück.
*/
inline int prepareInstrumentBuses (juce::AudioProcessor& processor)
{
    if (processor.getBusCount (false) == 0)
        return 0;

    for (const auto& mainOut : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono() })
    {
        auto layout = processor.getBusesLayout();

        for (auto& in : layout.inputBuses)
            in = juce::AudioChannelSet::disabled();

        layout.outputBuses.getReference (0) = mainOut;

        if (processor.setBusesLayout (layout))
            return processor.getTotalNumOutputChannels();
    }

    processor.enableAllBuses();
    return processor.getTotalNumOutputChannels();
}

} // namespace autosampler
