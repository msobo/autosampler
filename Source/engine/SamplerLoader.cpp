#include "SamplerLoader.h"
#include "Text.h"

namespace autosampler
{

/** Wie bei LoopBatch: owner nur auf dem Message-Thread, generation verwirft veraltete Bauaufträge. */
struct SamplerLoader::Relay
{
    SamplerLoader* owner = nullptr;
    std::atomic<int> generation { 0 };
};

namespace
{
    constexpr int rebuildDelayMs = 150;

    bool affectsPlayback (const juce::Identifier& id)
    {
        return id == sessionIds::file || id == sessionIds::note || id == sessionIds::velocity
            || id == sessionIds::loopEnabled || id == sessionIds::loopStart || id == sessionIds::loopEnd
            || id == sessionIds::crossfade || id == sessionIds::crossfadeMode || id == sessionIds::numFrames
            || id == sessionIds::roundRobin;
    }
}

SamplerLoader::SamplerLoader (Session& sessionToUse)
    : session (sessionToUse),
      sessionState (session.getState()),
      pool (juce::ThreadPoolOptions{}.withThreadName ("Sampler laden").withNumberOfThreads (1)),
      relay (std::make_shared<Relay>()),
      cache (std::make_shared<SamplerAudioCache>())
{
    relay->owner = this;
    sessionState.addListener (this);
    scheduleBuild();
}

SamplerLoader::~SamplerLoader()
{
    relay->owner = nullptr;
    ++relay->generation;
    sessionState.removeListener (this);
    pool.removeAllJobs (true, 10000);
}

void SamplerLoader::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id)
{
    if ((tree.hasType (sessionIds::sample) && affectsPlayback (id)) || tree.hasType (ExportSettings::getValueTreeType()))
        scheduleBuild();
}

void SamplerLoader::scheduleBuild()
{
    // Während eines Ziehvorgangs immer wieder neu starten
    startTimer (rebuildDelayMs);
}

void SamplerLoader::reload()
{
    force = true;
    timerCallback();
}

SamplerZoneSource makeSamplerSource (const Session& session, const juce::ValueTree& tree)
{
    const auto sample = SessionSample (tree, nullptr);

    SamplerZoneSource source;
    source.file = session.getSampleFile (tree);
    source.id = makeSampleId (sample.getFileName());
    source.note = sample.getNote();
    source.velocity = sample.getVelocity();
    source.roundRobin = sample.getRoundRobin();
    source.loop = sample.isLoopEnabled();

    if (source.loop)
    {
        source.loopStart = sample.getLoopStart();
        source.loopEnd = sample.getLoopEnd();
        source.crossfade = sample.getCrossfade();
        source.crossfadeMode = sample.getCrossfadeMode();
    }

    return source;
}

std::vector<SamplerZoneSource> getSamplerSources (const Session& session)
{
    std::vector<SamplerZoneSource> sources;

    for (const auto& tree : session.getSamplesTree())
        if (auto source = makeSamplerSource (session, tree); source.file != juce::File())
            sources.push_back (std::move (source));

    return sources;
}

void SamplerLoader::timerCallback()
{
    stopTimer();

    auto sources = getSamplerSources (session);
    const auto smoothing = session.getExportSettings().zoneSmoothing;

    if (hasBuilt && ! force && sources == lastSources && juce::exactlyEqual (smoothing, lastZoneSmoothing))
        return;

    if (force)
        cache->clear();

    force = false;
    hasBuilt = true;
    lastSources = sources;
    lastZoneSmoothing = smoothing;
    loading = true;
    status = "Loading " + juce::String (sources.size()) + " samples ...";

    if (onStatusChanged != nullptr)
        onStatusChanged();

    const auto generation = ++relay->generation;

    pool.addJob ([sharedRelay = relay, sharedCache = cache, generation, smoothing, jobSources = std::move (sources)]
    {
        if (sharedRelay->generation != generation)
            return;

        juce::StringArray errors;
        auto built = buildSamplerInstrument (jobSources, *sharedCache, errors, smoothing);
        sharedCache->retainOnly (jobSources);

        juce::MessageManager::callAsync ([sharedRelay, generation, instrument = std::move (built), errors]
        {
            if (sharedRelay->owner != nullptr)
                sharedRelay->owner->buildFinished (generation, instrument, errors);
        });
    });
}

void SamplerLoader::buildFinished (int generation, std::shared_ptr<const SamplerInstrument> instrument,
                                   const juce::StringArray& errors)
{
    if (generation != relay->generation)
        return;

    loading = false;

    const auto& zones = instrument->getZones();
    const auto numLoops = std::count_if (zones.begin(), zones.end(), [] (const SamplerZone& z) { return z.loop; });

    if (zones.empty())
        status = errors.isEmpty() ? juce::String ("No samples") : "No playable samples";
    else
        status = juce::String ((int) zones.size()) + (zones.size() == 1 ? " sample" : " samples") + " playable, "
               + juce::String ((int) numLoops) + " with loop";

    if (! errors.isEmpty())
        status << " (" << errors.size() << (errors.size() == 1 ? " file missing: " : " files missing, e.g. ") << errors[0] << ")";

    if (onInstrumentReady != nullptr)
        onInstrumentReady (std::move (instrument));

    if (onStatusChanged != nullptr)
        onStatusChanged();
}

} // namespace autosampler
