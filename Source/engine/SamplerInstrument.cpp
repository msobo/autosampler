#include "SamplerInstrument.h"
#include "AudioAnalysis.h"
#include "SampleWriter.h"
#include "ZoneSmoothing.h"

#include <numeric>

namespace autosampler
{

std::vector<ZoneRange> computeZoneRanges (const std::vector<std::pair<int, int>>& pairs)
{
    std::vector<int> notes;

    for (const auto& [note, velocity] : pairs)
        notes.push_back (juce::jlimit (0, 127, note));

    std::sort (notes.begin(), notes.end());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());

    std::vector<ZoneRange> ranges;

    for (const auto& [rawNote, rawVelocity] : pairs)
    {
        const auto note = juce::jlimit (0, 127, rawNote);
        const auto velocity = juce::jlimit (1, 127, rawVelocity);
        const auto index = (size_t) std::distance (notes.begin(), std::lower_bound (notes.begin(), notes.end(), note));

        ZoneRange range;
        range.lowKey = index == 0 ? 0 : (notes[index - 1] + note) / 2 + 1;
        range.highKey = index + 1 == notes.size() ? 127 : (note + notes[index + 1]) / 2;

        // Velocities derselben Note
        std::vector<int> velocities;

        for (const auto& [otherNote, otherVelocity] : pairs)
            if (juce::jlimit (0, 127, otherNote) == note)
                velocities.push_back (juce::jlimit (1, 127, otherVelocity));

        std::sort (velocities.begin(), velocities.end());
        velocities.erase (std::unique (velocities.begin(), velocities.end()), velocities.end());

        const auto v = (size_t) std::distance (velocities.begin(), std::lower_bound (velocities.begin(), velocities.end(), velocity));
        range.lowVelocity = v == 0 ? 1 : velocities[v - 1] + 1;
        range.highVelocity = v + 1 == velocities.size() ? 127 : velocity;

        ranges.push_back (range);
    }

    return ranges;
}

juce::int64 makeSampleId (const juce::String& fileName)
{
    return fileName.hashCode64();
}

//==============================================================================
SamplerInstrument::SamplerInstrument (std::vector<SamplerZone> zonesToUse)
    : zones (std::move (zonesToUse))
{
    for (auto& row : lookup)
        row.fill (-1);

    for (size_t z = 0; z < zones.size(); ++z)
    {
        const auto& range = zones[z].range;
        auto group = std::find_if (groups.begin(), groups.end(), [&] (const std::vector<int>& g)
        {
            return zones[(size_t) g.front()].range == range;
        });

        if (group == groups.end())
        {
            groups.push_back ({ (int) z });
            group = std::prev (groups.end());
        }
        else
        {
            group->push_back ((int) z);
        }

        const auto groupIndex = (juce::int16) std::distance (groups.begin(), group);

        for (int key = range.lowKey; key <= range.highKey; ++key)
            for (int velocity = range.lowVelocity; velocity <= range.highVelocity; ++velocity)
                if (lookup[(size_t) key][(size_t) velocity] < 0)
                    lookup[(size_t) key][(size_t) velocity] = groupIndex;
    }
}

int SamplerInstrument::findGroup (int key, int velocity) const noexcept
{
    if (! juce::isPositiveAndBelow (key, 128) || ! juce::isPositiveAndBelow (velocity, 128))
        return -1;

    return lookup[(size_t) key][(size_t) juce::jmax (1, velocity)];
}

int SamplerInstrument::findZone (juce::int64 id) const noexcept
{
    for (size_t i = 0; i < zones.size(); ++i)
        if (zones[i].id == id)
            return (int) i;

    return -1;
}

//==============================================================================
SamplerAudioCache::Loaded SamplerAudioCache::getRaw (const juce::File& file)
{
    const auto path = file.getFullPathName();

    {
        const std::scoped_lock lock (mutex);

        if (const auto it = raw.find (path); it != raw.end())
            return it->second;
    }

    // Laden ohne Lock, damit andere Zugriffe nicht warten
    Loaded loaded;
    auto buffer = std::make_shared<juce::AudioBuffer<float>>();
    loaded.error = readWavFile (file, *buffer, loaded.sampleRate);

    if (loaded.error.isEmpty())
    {
        loaded.levelDb = measureLevelDb (*buffer, loaded.sampleRate);
        loaded.audio = std::move (buffer);
    }

    const std::scoped_lock lock (mutex);
    raw[path] = loaded;
    return loaded;
}

SamplerAudioCache::LoopKey SamplerAudioCache::makeKey (const SamplerZoneSource& source)
{
    return { source.file.getFullPathName(), source.loopStart, source.loopEnd, source.crossfade, source.crossfadeMode };
}

SamplerAudioCache::Loaded SamplerAudioCache::getPlayable (const SamplerZoneSource& source)
{
    auto loaded = getRaw (source.file);

    if (! source.loop || loaded.audio == nullptr)
        return loaded;

    const auto key = makeKey (source);

    {
        const std::scoped_lock lock (mutex);

        if (const auto it = looped.find (key); it != looped.end())
            return it->second;
    }

    const auto& original = *loaded.audio;
    const auto end = juce::jlimit (0, original.getNumSamples(), source.loopEnd);
    const auto start = juce::jlimit (0, end, source.loopStart);

    if (end - start < 2)
        return loaded;

    // Crossfade auf einer Kopie rendern; nach E wird nie gelesen (der Loop läuft
    // auch im Release weiter), deshalb endet die Kopie bei E
    auto buffer = std::make_shared<juce::AudioBuffer<float>> (original);
    applyLoopCrossfade (*buffer, start, end, source.crossfade, source.crossfadeMode, loaded.sampleRate);
    buffer->setSize (buffer->getNumChannels(), end, true, false, true);

    Loaded result;
    result.audio = std::move (buffer);
    result.sampleRate = loaded.sampleRate;
    result.loopRendered = true;
    result.levelDb = loaded.levelDb;

    const std::scoped_lock lock (mutex);
    looped[key] = result;
    return result;
}

void SamplerAudioCache::retainOnly (const std::vector<SamplerZoneSource>& sources)
{
    std::set<juce::String> paths;
    std::set<LoopKey> keys;

    for (const auto& source : sources)
    {
        paths.insert (source.file.getFullPathName());

        if (source.loop)
            keys.insert (makeKey (source));
    }

    const std::scoped_lock lock (mutex);
    std::erase_if (raw, [&] (const auto& entry) { return paths.count (entry.first) == 0; });
    std::erase_if (looped, [&] (const auto& entry) { return keys.count (entry.first) == 0; });
}

void SamplerAudioCache::clear()
{
    const std::scoped_lock lock (mutex);
    raw.clear();
    looped.clear();
}

//==============================================================================
std::shared_ptr<const SamplerInstrument> buildSamplerInstrument (const std::vector<SamplerZoneSource>& sources,
                                                                 SamplerAudioCache& cache, juce::StringArray& errors,
                                                                 float zoneSmoothing)
{
    std::vector<SamplerZone> zones;
    std::vector<std::pair<int, int>> pairs;
    std::vector<ZoneLevel> levels;

    // Round Robin: Varianten in ihrer Reihenfolge, sonst Reihenfolge der Session
    std::vector<size_t> order (sources.size());
    std::iota (order.begin(), order.end(), size_t { 0 });
    std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b)
    {
        return sources[a].roundRobin < sources[b].roundRobin;
    });

    for (const auto index : order)
    {
        const auto& source = sources[index];
        const auto loaded = cache.getPlayable (source);

        if (loaded.audio == nullptr || loaded.audio->getNumSamples() == 0 || loaded.sampleRate <= 0.0)
        {
            errors.add (source.file.getFileName() + ": " + (loaded.error.isNotEmpty() ? loaded.error : juce::String ("leer")));
            continue;
        }

        SamplerZone zone;
        zone.audio = loaded.audio;
        zone.sampleRate = loaded.sampleRate;
        zone.rootNote = juce::jlimit (0, 127, source.note);
        zone.velocity = juce::jlimit (1, 127, source.velocity);
        zone.id = source.id;

        const auto numFrames = zone.audio->getNumSamples();
        zone.loop = loaded.loopRendered && numFrames >= 2;
        zone.loopEnd = zone.loop ? numFrames : 0;
        zone.loopStart = zone.loop ? juce::jlimit (0, numFrames - 2, source.loopStart) : 0;

        pairs.emplace_back (zone.rootNote, zone.velocity);
        levels.push_back ({ zone.rootNote, zone.velocity, loaded.levelDb });
        zones.push_back (std::move (zone));
    }

    const auto ranges = computeZoneRanges (pairs);
    const auto corrections = computeZoneSmoothing (levels, zoneSmoothing);

    for (size_t i = 0; i < zones.size(); ++i)
    {
        zones[i].range = ranges[i];
        zones[i].gain = juce::Decibels::decibelsToGain (corrections[i]);
    }

    return std::make_shared<const SamplerInstrument> (std::move (zones));
}

} // namespace autosampler
