#include "LoopBatch.h"
#include "SampleWriter.h"

namespace autosampler
{

/** Geteilt mit den Jobs: owner wird nur auf dem Message-Thread gelesen und
    beim Löschen auf nullptr gesetzt, generation verwirft veraltete Suchaufträge. */
struct LoopBatch::Relay
{
    LoopBatch* owner = nullptr;
    std::atomic<int> generation { 0 };
};

namespace
{
    constexpr int rescoreDelayMs = 300;

    bool isLoopProperty (const juce::Identifier& id)
    {
        return id == sessionIds::loopStart || id == sessionIds::loopEnd || id == sessionIds::crossfade
            || id == sessionIds::crossfadeMode || id == sessionIds::loopEnabled;
    }
}

LoopBatch::LoopBatch (Session& sessionToUse)
    : session (sessionToUse),
      sessionState (session.getState()),
      pool (juce::ThreadPoolOptions{}.withThreadName ("Loop-Suche")
                                     .withNumberOfThreads (juce::jmax (1, juce::SystemStats::getNumCpus() - 1))),
      relay (std::make_shared<Relay>())
{
    relay->owner = this;
    sessionState.addListener (this);
}

LoopBatch::~LoopBatch()
{
    relay->owner = nullptr;
    ++relay->generation;
    sessionState.removeListener (this);
    pool.removeAllJobs (true, 10000);
}

bool LoopBatch::belongsToSession (const juce::ValueTree& tree) const
{
    return tree.hasType (sessionIds::sample) && tree.getParent() == session.getSamplesTree();
}

LoopBatch::Input LoopBatch::makeInput (const juce::ValueTree& tree) const
{
    const auto sample = SessionSample (tree, nullptr);

    Input input;
    input.file = session.getSampleFile (tree);
    input.hints = session.getLoopHints (tree);
    input.settings = session.getLoopSearchSettings();
    input.loopStart = sample.getLoopStart();
    input.loopEnd = sample.getLoopEnd();
    input.crossfade = sample.getCrossfade();
    input.mode = sample.getCrossfadeMode();
    return input;
}

//==============================================================================
void LoopBatch::search (const std::vector<juce::ValueTree>& samples, const LoopSearchSettings& settings)
{
    cancel();

    for (const auto& tree : samples)
        if (belongsToSession (tree) && ! SessionSample (tree, nullptr).isLocked())
            searchSamples.push_back (tree);

    if (searchSamples.empty())
    {
        if (onFinished != nullptr)
            onFinished (0, 0, "No unlocked samples");

        return;
    }

    searchResults.assign (searchSamples.size(), std::nullopt);
    searchTotal = (int) searchSamples.size();
    searchMode = settings.crossfadeMode;
    const auto currentGeneration = relay->generation.load();

    for (size_t i = 0; i < searchSamples.size(); ++i)
    {
        auto input = makeInput (searchSamples[i]);
        input.settings = settings;

        pool.addJob ([sharedRelay = relay, currentGeneration, index = (int) i, input]
        {
            if (sharedRelay->generation != currentGeneration)
                return;

            LoopResult result;
            result.algorithm = input.settings.algorithm;
            juce::AudioBuffer<float> audio;
            double sampleRate = 0.0;

            if (const auto error = readWavFile (input.file, audio, sampleRate); error.isNotEmpty())
                result.message = error;
            else
                result = findLoop (audio, sampleRate, input.settings, input.hints);

            juce::MessageManager::callAsync ([sharedRelay, currentGeneration, index, result]
            {
                if (sharedRelay->owner != nullptr)
                    sharedRelay->owner->searchFinished (currentGeneration, index, result);
            });
        });
    }

    if (onProgress != nullptr)
        onProgress();
}

void LoopBatch::cancel()
{
    const auto wasSearching = isSearching();

    ++relay->generation;
    searchSamples.clear();
    searchResults.clear();
    searchDone = searchTotal = 0;

    if (wasSearching && onProgress != nullptr)
        onProgress();
}

void LoopBatch::searchFinished (int resultGeneration, int index, const LoopResult& result)
{
    if (resultGeneration != relay->generation || ! juce::isPositiveAndBelow (index, (int) searchResults.size()))
        return;

    searchResults[(size_t) index] = result;
    ++searchDone;

    if (searchDone >= searchTotal)
        applySearchResults();
    else if (onProgress != nullptr)
        onProgress();
}

void LoopBatch::applySearchResults()
{
    int numApplied = 0, numFailed = 0;
    juce::String message;

    {
        const juce::ScopedValueSetter<bool> guard (applying, true);
        session.getUndoManager().beginNewTransaction (searchSamples.size() == 1 ? "Find loop" : "Find loops");

        for (size_t i = 0; i < searchSamples.size(); ++i)
        {
            const auto& tree = searchSamples[i];
            const auto& result = searchResults[i];

            if (! belongsToSession (tree) || ! result.has_value())
                continue;

            if (session.wrap (tree).applyLoopResult (*result, searchMode))
            {
                ++numApplied;
            }
            else
            {
                ++numFailed;
                message = result->message;
            }
        }
    }

    searchSamples.clear();
    searchResults.clear();
    searchDone = searchTotal = 0;

    if (onProgress != nullptr)
        onProgress();

    if (onFinished != nullptr)
        onFinished (numApplied, numFailed, message);
}

//==============================================================================
void LoopBatch::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id)
{
    if (applying || ! isLoopProperty (id) || ! tree.hasType (sessionIds::sample))
        return;

    if (std::find (scorePending.begin(), scorePending.end(), tree) == scorePending.end())
        scorePending.push_back (tree);

    // Während des Ziehens immer wieder neu starten: bewertet wird erst danach
    startTimer (rescoreDelayMs);
}

void LoopBatch::timerCallback()
{
    stopTimer();

    for (const auto& tree : scorePending)
    {
        if (! belongsToSession (tree) || ! SessionSample (tree, nullptr).isLoopEnabled())
            continue;

        const auto requestId = nextScoreRequest++;
        scoreRequests[requestId] = tree;

        pool.addJob ([sharedRelay = relay, requestId, input = makeInput (tree)]
        {
            juce::AudioBuffer<float> audio;
            double sampleRate = 0.0;
            float score = -1.0f;

            if (readWavFile (input.file, audio, sampleRate).isEmpty())
                score = scoreLoop (audio, sampleRate, input.loopStart, input.loopEnd, input.crossfade, input.mode);

            juce::MessageManager::callAsync ([sharedRelay, requestId, input, score]
            {
                if (sharedRelay->owner != nullptr)
                    sharedRelay->owner->scoreFinished (requestId, input, score);
            });
        });
    }

    scorePending.clear();
}

void LoopBatch::scoreFinished (int requestId, const Input& input, float score)
{
    const auto node = scoreRequests.extract (requestId);

    if (node.empty() || ! belongsToSession (node.mapped()))
        return;

    // Nur übernehmen, wenn sich der Loop seitdem nicht geändert hat
    auto sample = session.wrap (node.mapped());

    if (sample.getLoopStart() == input.loopStart && sample.getLoopEnd() == input.loopEnd
        && sample.getCrossfade() == input.crossfade && sample.getCrossfadeMode() == input.mode)
        sample.setLoopScore (score);
}

} // namespace autosampler
