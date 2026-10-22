#pragma once

#include "SamplerInstrument.h"
#include "SamplingSettings.h"

namespace autosampler
{

/** Dateiname einer Aufnahme, z. B. "TRITON_060_C3_v100.wav", mit Round Robin
    "TRITON_060_C3_v100_rr2.wav". */
juce::String makeSampleFileName (const juce::String& prefix, const SampleRequest&);

/** Legt parent/name+extension an; existiert der Ordner schon, wird " 2", " 3" …
    vor der Endung angehängt. */
juce::File createUniqueFolder (const juce::File& parent, const juce::String& name,
                               const juce::String& extension = {});

/** Metadaten einer exportierten WAV-Datei. */
struct WavMetadata
{
    int rootNote = 60;
    std::optional<juce::Range<int>> loop;   ///< Frames, Ende exklusiv (im smpl-Chunk inklusiv)
    std::optional<ZoneRange> zone;          ///< Tasten- und Velocity-Bereich (inst-Chunk)
    bool sampleChunkWithoutLoop = true;     ///< false: smpl-Chunk nur mit Loop (Tonverk)
};

/** Schreibt eine WAV-Datei mit smpl-Chunk (MIDI Unity Note).
    bitDepth 16 oder 24 schreibt PCM, 32 schreibt Float.
    Liefert eine Fehlermeldung oder einen leeren String. */
juce::String writeWavFile (const juce::File&, const juce::AudioBuffer<float>&,
                           double sampleRate, int bitDepth, int rootNote);

/** Wie oben, mit Loop im smpl-Chunk und optional inst-Chunk. */
juce::String writeWavFile (const juce::File&, const juce::AudioBuffer<float>&,
                           double sampleRate, int bitDepth, const WavMetadata&);

/** Liest eine WAV-Datei komplett ein. Liefert eine Fehlermeldung oder "". */
juce::String readWavFile (const juce::File&, juce::AudioBuffer<float>&, double& sampleRate);

} // namespace autosampler
