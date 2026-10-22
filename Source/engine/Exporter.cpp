#include "Exporter.h"
#include "AudioAnalysis.h"
#include "SampleWriter.h"
#include "SamplerLoader.h"
#include "Session.h"
#include "Text.h"
#include "ZoneSmoothing.h"

#include <numeric>

namespace autosampler
{

namespace
{
    constexpr double tonverkSampleRate = 48000.0;

    /** Kommazahl ohne überflüssige Nullen, z. B. 0.002 oder 0.3. */
    juce::String formatDecimal (double value)
    {
        auto text = juce::String (value, 6);

        if (text.containsChar ('.'))
            text = text.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");

        return text == "-0" ? juce::String ("0") : text;
    }

    /** Wie die Tonverk-Werksdateien: ganze Zahlen mit einer Nachkommastelle (60.0),
        sonst höchstens 8 signifikante Stellen (0.78740157). */
    juce::String formatElektronNumber (double value)
    {
        if (juce::exactlyEqual (value, std::rint (value)) && std::abs (value) < 1.0e15)
            return juce::String (value, 1);

        char buffer[64];
        std::snprintf (buffer, sizeof (buffer), "%.8g", value);
        juce::String text (buffer);

        if (text.containsChar ('.') && ! text.containsAnyOf ("eE"))
        {
            text = text.trimCharactersAtEnd ("0");

            if (text.endsWithChar ('.'))
                text << "0";
        }

        return text;
    }

    /** TOML-String: einfache Anführungszeichen, sonst doppelte mit Escapes. */
    juce::String quoteElektron (const juce::String& text)
    {
        if (! text.containsChar ('\''))
            return "'" + text + "'";

        return "\"" + text.replace ("\\", "\\\\").replace ("\"", "\\\"") + "\"";
    }

    juce::String makeLegalName (const juce::String& name)
    {
        const auto legal = juce::File::createLegalFileName (name.trim());
        return legal.isNotEmpty() ? legal : juce::String ("Instrument");
    }

    juce::File makeUniqueFile (const juce::File& parent, const juce::String& name, const juce::String& extension)
    {
        auto file = parent.getChildFile (name + extension);

        for (int suffix = 2; file.exists(); ++suffix)
            file = parent.getChildFile (name + " " + juce::String (suffix) + extension);

        return file;
    }

    juce::String writeZip (const juce::File& folder, const juce::File& zipFile)
    {
        juce::ZipFile::Builder builder;

        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
            builder.addFile (entry.getFile(), 9,
                             folder.getFileName() + "/" + entry.getFile().getRelativePathFrom (folder).replaceCharacter ('\\', '/'));

        zipFile.deleteFile();
        juce::FileOutputStream stream (zipFile);

        if (stream.failedToOpen() || ! builder.writeToStream (stream, nullptr))
            return "Cannot write zip: " + zipFile.getFullPathName();

        return {};
    }
}

//==============================================================================
std::vector<SamplerZoneSource> getExportSources (const Session& session)
{
    std::vector<SamplerZoneSource> sources;

    for (const auto& tree : session.getSamplesTree())
        if (SessionSample (tree, nullptr).isExported())
            if (auto source = makeSamplerSource (session, tree); source.file != juce::File())
                sources.push_back (std::move (source));

    return sources;
}

ExportSettings resolveExportSettings (const ExportSettings& input, juce::StringArray& warnings)
{
    auto settings = input;
    settings.peakDb = juce::jlimit (-6.0f, -1.0f, settings.peakDb);

    if (settings.bitDepth != 16 && settings.bitDepth != 24 && settings.bitDepth != 32)
        settings.bitDepth = 24;

    if (settings.target == ExportTarget::tonverk)
    {
        if (! juce::exactlyEqual (settings.sampleRate, tonverkSampleRate))
        {
            if (settings.sampleRate > 0.0)
                warnings.add ("Tonverk: sample rate set to 48 kHz");

            settings.sampleRate = tonverkSampleRate;
        }

        if (settings.bitDepth > 24)
        {
            warnings.add ("Tonverk: 24 bit instead of 32 bit float");
            settings.bitDepth = 24;
        }
    }

    return settings;
}

juce::String makeExportFileName (ExportTarget target, const juce::String& instrumentName, int note, int velocity,
                                 int velocityLayer, int roundRobinIndex, int roundRobinCount)
{
    note = juce::jlimit (0, 127, note);

    if (target == ExportTarget::tonverk)
    {
        // Konvention der Elektron-Werksdateien: Name-VVV-NNN-note[-rrN], C3 = 60
        auto name = makeLegalName (instrumentName) + "-" + juce::String (velocityLayer).paddedLeft ('0', 3)
                  + "-" + juce::String (note).paddedLeft ('0', 3) + "-" + getNoteName (note).toLowerCase();

        if (roundRobinIndex > 0)
            name << "-rr" << roundRobinIndex;

        return name + ".wav";
    }

    auto name = makeSampleFileName (instrumentName, { note, velocity }).upToLastOccurrenceOf (".wav", false, true);

    if (roundRobinCount > 1)
        name << "_rr" << (roundRobinIndex + 1);

    return name + ".wav";
}

std::vector<ExportSample> planExport (const std::vector<SamplerZoneSource>& sources, ExportTarget target,
                                      const juce::String& instrumentName)
{
    std::vector<size_t> order (sources.size());
    std::iota (order.begin(), order.end(), size_t { 0 });
    std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b)
    {
        return std::tie (sources[a].note, sources[a].velocity, sources[a].roundRobin)
             < std::tie (sources[b].note, sources[b].velocity, sources[b].roundRobin);
    });

    std::vector<ExportSample> plan;
    std::vector<std::pair<int, int>> pairs;

    for (const auto index : order)
    {
        ExportSample sample;
        sample.source = sources[index];
        sample.source.note = juce::jlimit (0, 127, sample.source.note);
        sample.source.velocity = juce::jlimit (1, 127, sample.source.velocity);
        pairs.emplace_back (sample.source.note, sample.source.velocity);
        plan.push_back (std::move (sample));
    }

    const auto ranges = computeZoneRanges (pairs);

    for (size_t i = 0; i < plan.size(); ++i)
    {
        auto& sample = plan[i];
        sample.range = ranges[i];

        std::set<int> velocities;

        for (const auto& [note, velocity] : pairs)
            if (note == sample.source.note)
                velocities.insert (velocity);

        sample.velocityLayer = (int) std::distance (velocities.begin(), velocities.find (sample.source.velocity));
        sample.roundRobinCount = (int) std::count (pairs.begin(), pairs.end(), pairs[i]);
        sample.roundRobinIndex = (int) std::count (pairs.begin(), pairs.begin() + (std::ptrdiff_t) i, pairs[i]);
        sample.fileName = makeExportFileName (target, instrumentName, sample.source.note, sample.source.velocity,
                                              sample.velocityLayer, sample.roundRobinIndex, sample.roundRobinCount);
    }

    return plan;
}

juce::String prepareExportAudio (const SamplerZoneSource& source, const ExportSettings& settings, PreparedAudio& prepared)
{
    auto& audio = prepared.audio;
    double rate = 0.0;

    if (auto error = readWavFile (source.file, audio, rate); error.isNotEmpty())
        return error;

    if (audio.getNumSamples() == 0 || rate <= 0.0)
        return source.file.getFileName() + " is empty";

    prepared.sourceLevelDb = measureLevelDb (audio, rate);

    prepared.loop.reset();

    if (source.loop)
    {
        const auto end = juce::jlimit (0, audio.getNumSamples(), source.loopEnd);
        const auto start = juce::jlimit (0, end, source.loopStart);

        if (end - start >= 2)
        {
            // Dieselbe Funktion wie Sampler und Score: der Export klingt wie das Vorhören
            applyLoopCrossfade (audio, start, end, source.crossfade, source.crossfadeMode, rate);
            prepared.loop = juce::Range<int> (start, end);
        }
    }

    if (const auto loop = prepared.loop)
    {
        if (settings.loopOnly)
        {
            juce::AudioBuffer<float> loopOnly (audio.getNumChannels(), loop->getLength());

            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                loopOnly.copyFrom (ch, 0, audio, ch, loop->getStart(), loop->getLength());

            audio = std::move (loopOnly);
            prepared.loop = juce::Range<int> (0, loop->getLength());
        }
        else if (settings.tail == TailMode::cutAfterLoop)
        {
            const auto length = juce::jmin (audio.getNumSamples(), loop->getEnd() + ExportSettings::tailAfterLoop);
            audio.setSize (audio.getNumChannels(), length, true, false, true);
        }
    }

    if (settings.mono && audio.getNumChannels() > 1)
    {
        juce::AudioBuffer<float> mono (1, audio.getNumSamples());
        mono.clear();

        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            mono.addFrom (0, 0, audio, ch, 0, audio.getNumSamples(), 1.0f / (float) audio.getNumChannels());

        audio = std::move (mono);
    }

    prepared.sampleRate = rate;

    if (settings.sampleRate > 0.0 && std::abs (settings.sampleRate - rate) > 0.01)
    {
        const auto ratio = settings.sampleRate / rate;
        audio = resample (audio, rate, settings.sampleRate);
        prepared.sampleRate = settings.sampleRate;

        // Loop-Länge nur einmal runden, damit sich die Tonhöhe kurzer Loops nicht verschiebt
        if (const auto loop = prepared.loop)
        {
            const auto length = juce::jmax (2, (int) std::lround (loop->getLength() * ratio));
            const auto end = juce::jmin (audio.getNumSamples(), (int) std::lround (loop->getStart() * ratio) + length);
            const auto start = juce::jmax (0, end - length);
            prepared.loop = juce::Range<int> (start, end);
        }
    }

    return {};
}

//==============================================================================
juce::String makeSfz (const std::vector<ExportedRegion>& regions, const ExportSettings& settings,
                      const juce::String& instrumentName)
{
    juce::String text;
    text << "// " << instrumentName << ", exported with Autosampler\n"
         << "// " << (int) regions.size() << (regions.size() == 1 ? " sample" : " samples") << "\n\n"
         << "<control>\n"
         << "default_path=samples/\n\n"
         << "<global>\n"
         << "ampeg_attack=" << formatDecimal (settings.attackMs * 0.001) << "\n"
         << "ampeg_release=" << formatDecimal (settings.releaseMs * 0.001) << "\n"
         << "amp_veltrack=" << formatDecimal (juce::jlimit (0.0f, 1.0f, settings.velocitySensitivity) * 100.0) << "\n\n"
         << "<group>\n";

    for (const auto& region : regions)
    {
        const auto& sample = region.sample;
        text << "<region> sample=" << sample.fileName
             << " lokey=" << sample.range.lowKey << " hikey=" << sample.range.highKey
             << " lovel=" << sample.range.lowVelocity << " hivel=" << sample.range.highVelocity
             << " pitch_keycenter=" << sample.source.note;

        if (sample.roundRobinCount > 1)
            text << " seq_length=" << sample.roundRobinCount << " seq_position=" << (sample.roundRobinIndex + 1);

        if (const auto loop = region.loop)
        {
            // loop_end ist in SFZ inklusiv
            text << " loop_mode=" << (settings.loopOnRelease ? "loop_continuous" : "loop_sustain")
                 << " loop_start=" << loop->getStart() << " loop_end=" << (loop->getEnd() - 1);
        }
        else
        {
            text << " loop_mode=" << (settings.oneShotWithoutLoop ? "one_shot" : "no_loop");
        }

        text << "\n";
    }

    return text;
}

juce::String makeElmulti (const std::vector<ExportedRegion>& regions, const ExportSettings& settings,
                          const juce::String& instrumentName)
{
    juce::String text;
    text << "# ELEKTRON MULTI-SAMPLE MAPPING FORMAT\n"
         << "version = 0\n"
         << "name = " << quoteElektron (instrumentName) << "\n";

    // regions sind nach Note und Velocity sortiert (planExport)
    int currentNote = -1, currentVelocity = -1;

    for (const auto& region : regions)
    {
        const auto& sample = region.sample;

        if (sample.source.note != currentNote)
        {
            currentNote = sample.source.note;
            currentVelocity = -1;
            text << "\n[[key-zones]]\n"
                 << "pitch = " << currentNote << "\n"
                 << "key-center = " << formatElektronNumber (currentNote) << "\n";
        }

        if (sample.source.velocity != currentVelocity)
        {
            // Velocity der Aufnahme (0–1); 0.0 lehnt der Tonverk ab, velocity ist immer >= 1
            currentVelocity = sample.source.velocity;
            text << "\n[[key-zones.velocity-layers]]\n"
                 << "velocity = " << formatElektronNumber (currentVelocity / 127.0) << "\n"
                 << "strategy = 'Forward'\n";
        }

        text << "\n[[key-zones.velocity-layers.sample-slots]]\n"
             << "sample = " << quoteElektron (sample.fileName) << "\n";

        if (const auto loop = region.loop)
        {
            // loop-end inklusiv wie im smpl-Chunk; Crossfade ist schon im Audio
            text << "loop-mode = 'Forward'\n"
                 << "loop-start = " << loop->getStart() << "\n"
                 << "loop-end = " << (loop->getEnd() - 1) << "\n";

            if (settings.loopOnRelease)
                text << "keep-looping-on-release = true\n";
        }
        else
        {
            text << "loop-mode = 'Off'\n";
        }
    }

    return text;
}

//==============================================================================
ExportResult runExport (const std::vector<SamplerZoneSource>& sources, const ExportSettings& input,
                        const juce::String& instrumentName, const juce::File& destinationParent,
                        const ExportCallbacks& callbacks)
{
    ExportResult result;
    const auto settings = resolveExportSettings (input, result.warnings);
    const auto name = makeLegalName (instrumentName);

    if (sources.empty())
    {
        result.error = "No samples to export";
        return result;
    }

    if (! destinationParent.isDirectory() && destinationParent.createDirectory().failed())
    {
        result.error = "Cannot create destination folder: " + destinationParent.getFullPathName();
        return result;
    }

    // Zip: erst in einen temporären Ordner, dann packen
    juce::File tempFolder, root;

    if (settings.zip)
    {
        tempFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("AutosamplerExport-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        root = tempFolder.getChildFile (name);

        if (root.createDirectory().failed())
            root = juce::File();
    }
    else
    {
        root = createUniqueFolder (destinationParent, name);
    }

    if (root == juce::File())
    {
        result.error = "Cannot create output folder";
        return result;
    }

    const auto cleanUp = [&]
    {
        (tempFolder != juce::File() ? tempFolder : root).deleteRecursively();
    };

    const auto isCancelled = [&]
    {
        return callbacks.shouldCancel != nullptr && callbacks.shouldCancel();
    };

    const auto sampleFolder = settings.target == ExportTarget::sfz ? root.getChildFile ("samples") : root;
    sampleFolder.createDirectory();

    auto plan = planExport (sources, settings.target, name);
    const auto analyse = settings.normalize || settings.zoneSmoothing > 0.0f;
    const auto numSteps = (double) plan.size() * (analyse ? 2.0 : 1.0);
    int step = 0;

    const auto reportProgress = [&]
    {
        if (callbacks.onProgress != nullptr)
            callbacks.onProgress (++step / numSteps);
    };

    // Erster Durchgang: Pegel für das Zone Smoothing und Spitzen für die Normalisierung
    auto gain = 1.0f;

    if (analyse)
    {
        std::vector<ZoneLevel> levels;
        std::vector<float> peaks;

        for (const auto& sample : plan)
        {
            if (isCancelled())
            {
                cleanUp();
                result.cancelled = true;
                return result;
            }

            PreparedAudio prepared;
            const auto ok = prepareExportAudio (sample.source, settings, prepared).isEmpty();
            levels.push_back ({ sample.source.note, sample.source.velocity, ok ? prepared.sourceLevelDb : -100.0f });
            peaks.push_back (ok ? prepared.audio.getMagnitude (0, prepared.audio.getNumSamples()) : 0.0f);
            reportProgress();
        }

        // Wie im Sampler (buildSamplerInstrument): Pegel der Rohdaten
        const auto corrections = computeZoneSmoothing (levels, settings.zoneSmoothing);
        auto maxPeak = 0.0f;

        for (size_t i = 0; i < plan.size(); ++i)
        {
            plan[i].gainDb = corrections[i];
            maxPeak = juce::jmax (maxPeak, peaks[i] * juce::Decibels::decibelsToGain (corrections[i]));
        }

        // Normalisierung: eine gemeinsame Verstärkung, damit die Layer zueinander passen
        if (settings.normalize && maxPeak > 0.0f)
            gain = juce::Decibels::decibelsToGain (settings.peakDb) / maxPeak;
    }

    std::vector<ExportedRegion> regions;
    auto numClipped = 0;

    for (const auto& sample : plan)
    {
        if (isCancelled())
        {
            cleanUp();
            result.cancelled = true;
            return result;
        }

        PreparedAudio prepared;

        if (const auto error = prepareExportAudio (sample.source, settings, prepared); error.isNotEmpty())
        {
            result.warnings.add (error);
            reportProgress();
            continue;
        }

        auto& audio = prepared.audio;
        audio.applyGain (gain * juce::Decibels::decibelsToGain (sample.gainDb));

        if (settings.bitDepth < 32 && audio.getMagnitude (0, audio.getNumSamples()) > 1.0f)
            ++numClipped;

        WavMetadata metadata;
        metadata.rootNote = sample.source.note;
        metadata.loop = prepared.loop;

        // Der Tonverk-Parser ist streng: keine inst-Chunks, smpl nur mit Loop
        if (settings.target == ExportTarget::tonverk)
            metadata.sampleChunkWithoutLoop = false;
        else
            metadata.zone = sample.range;

        if (const auto error = writeWavFile (sampleFolder.getChildFile (sample.fileName), audio, prepared.sampleRate,
                                             settings.bitDepth, metadata);
            error.isNotEmpty())
        {
            cleanUp();
            result.error = error;
            return result;
        }

        regions.push_back ({ sample, prepared.loop, audio.getNumSamples() });
        ++result.numWritten;
        reportProgress();
    }

    if (numClipped > 0)
        result.warnings.add (juce::String (numClipped) + (numClipped == 1 ? " sample clipped" : " samples clipped")
                             + " (enable normalizing)"_u);

    if (regions.empty())
    {
        cleanUp();
        result.error = "No sample could be exported";
        return result;
    }

    // Mapping-Datei
    juce::String mappingError;

    if (settings.target == ExportTarget::sfz)
        mappingError = root.getChildFile (name + ".sfz").replaceWithText (makeSfz (regions, settings, name), false, false, "\n")
                           ? juce::String() : juce::String ("Cannot write SFZ file");
    else if (settings.target == ExportTarget::tonverk)
        mappingError = root.getChildFile (name + ".elmulti").replaceWithText (makeElmulti (regions, settings, name), false, false, "\n")
                           ? juce::String() : juce::String ("Cannot write elmulti file");

    if (mappingError.isNotEmpty())
    {
        cleanUp();
        result.error = mappingError;
        return result;
    }

    if (settings.zip)
    {
        const auto zipFile = makeUniqueFile (destinationParent, name, ".zip");

        if (const auto error = writeZip (root, zipFile); error.isNotEmpty())
        {
            zipFile.deleteFile();
            cleanUp();
            result.error = error;
            return result;
        }

        cleanUp();
        result.output = zipFile;
    }
    else
    {
        result.output = root;
    }

    return result;
}

} // namespace autosampler
