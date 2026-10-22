#include "SampleWriter.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace autosampler
{

juce::String makeSampleFileName (const juce::String& prefix, const SampleRequest& request)
{
    const auto base = juce::File::createLegalFileName (prefix.trim()).replaceCharacter (' ', '_');

    return (base.isNotEmpty() ? base + "_" : juce::String())
         + juce::String (request.note).paddedLeft ('0', 3) + "_"
         + getNoteName (request.note) + "_v"
         + juce::String (request.velocity).paddedLeft ('0', 3)
         + (request.roundRobinCount > 1 ? "_rr" + juce::String (request.roundRobin + 1) : juce::String())
         + ".wav";
}

juce::File createUniqueFolder (const juce::File& parent, const juce::String& name, const juce::String& extension)
{
    auto legalName = juce::File::createLegalFileName (name.trim());

    if (legalName.isEmpty())
        legalName = "Samples";

    auto folder = parent.getChildFile (legalName + extension);

    for (int suffix = 2; folder.exists(); ++suffix)
        folder = parent.getChildFile (legalName + " " + juce::String (suffix) + extension);

    if (const auto result = folder.createDirectory(); result.failed())
        return {};

    return folder;
}

juce::String writeWavFile (const juce::File& file, const juce::AudioBuffer<float>& audio,
                           double sampleRate, int bitDepth, int rootNote)
{
    WavMetadata metadata;
    metadata.rootNote = rootNote;
    return writeWavFile (file, audio, sampleRate, bitDepth, metadata);
}

juce::String writeWavFile (const juce::File& file, const juce::AudioBuffer<float>& audio,
                           double sampleRate, int bitDepth, const WavMetadata& metadata)
{
    const auto isFloat = bitDepth >= 32;

    // FileOutputStream hängt an bestehende Dateien an
    file.deleteFile();
    auto fileStream = std::make_unique<juce::FileOutputStream> (file);

    if (fileStream->failedToOpen())
        return "Cannot write file: " + file.getFullPathName();

    std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);

    // Schlüssel wie in juce_WavAudioFormat.cpp (SMPLChunk, InstChunk);
    // ohne Einträge schreibt JUCE weder smpl noch inst
    std::unordered_map<juce::String, juce::String> values;
    const auto hasLoop = metadata.loop.has_value() && metadata.loop->getLength() > 0;

    if (hasLoop || metadata.sampleChunkWithoutLoop)
    {
        values["MidiUnityNote"] = juce::String (juce::jlimit (0, 127, metadata.rootNote));
        values["SamplePeriod"] = juce::String ((juce::int64) std::llround (1.0e9 / sampleRate));
        values["NumSampleLoops"] = hasLoop ? "1" : "0";

        if (hasLoop)
        {
            values["Loop0Identifier"] = "0";
            values["Loop0Type"] = "0";      // vorwärts
            values["Loop0Start"] = juce::String (metadata.loop->getStart());
            values["Loop0End"] = juce::String (metadata.loop->getEnd() - 1);
        }
    }

    if (const auto& zone = metadata.zone)
    {
        values["MidiUnityNote"] = juce::String (juce::jlimit (0, 127, metadata.rootNote));
        values["LowNote"] = juce::String (zone->lowKey);
        values["HighNote"] = juce::String (zone->highKey);
        values["LowVelocity"] = juce::String (zone->lowVelocity);
        values["HighVelocity"] = juce::String (zone->highVelocity);
    }

    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (audio.getNumChannels())
                             .withBitsPerSample (isFloat ? 32 : bitDepth)
                             .withSampleFormat (isFloat ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                        : juce::AudioFormatWriterOptions::SampleFormat::integral)
                             .withMetadataValues (values);

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, options);

    if (writer == nullptr)
        return "Could not create WAV writer (" + juce::String (bitDepth) + " bit)";

    if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
        return "Error writing " + file.getFileName();

    return {};
}

juce::String readWavFile (const juce::File& file, juce::AudioBuffer<float>& audio, double& sampleRate)
{
    // createReaderFor() verträgt keinen leeren Stream (fehlende Datei)
    auto stream = file.createInputStream();

    if (stream == nullptr)
        return "File missing: " + file.getFullPathName();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream.release(), true));

    if (reader == nullptr)
        return "Cannot read file: " + file.getFullPathName();

    if (reader->lengthInSamples > std::numeric_limits<int>::max())
        return file.getFileName() + " is too long";

    audio.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);

    if (! reader->read (&audio, 0, audio.getNumSamples(), 0, true, true))
        return "Error reading " + file.getFileName();

    sampleRate = reader->sampleRate;
    return {};
}

} // namespace autosampler
