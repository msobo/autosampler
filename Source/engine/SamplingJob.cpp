#include "SamplingJob.h"
#include "AudioAnalysis.h"
#include "NoteRenderer.h"
#include "SampleWriter.h"
#include "Text.h"

namespace autosampler
{

namespace
{
    juce::String describe (const SampleRequest& request)
    {
        auto text = getNoteName (request.note) + " v" + juce::String (request.velocity);

        if (request.roundRobinCount > 1)
            text << " rr" << (request.roundRobin + 1);

        if (request.variation >= 0)
            text << " (" << request.variation << ")";

        return text;
    }

    /** Ab hier gilt eine Aufnahme als identisch mit der ersten Variante. */
    constexpr float identicalDifferenceDb = -60.0f;

    bool hasSound (const juce::AudioBuffer<float>& audio, float thresholdDb)
    {
        return findFirstSampleAbove (audio, juce::Decibels::decibelsToGain (thresholdDb, -200.0f)) >= 0;
    }
}

juce::String getParameterIdentifier (const juce::AudioProcessorParameter& parameter)
{
    if (const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*> (&parameter))
        if (const auto id = hosted->getParameterID(); id.isNotEmpty())
            return id;

    if (const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*> (&parameter))
        return withId->paramID;

    return juce::String (parameter.getParameterIndex());
}

juce::AudioProcessorParameter* findParameter (juce::AudioProcessor& processor, const juce::String& identifier)
{
    if (identifier.isEmpty())
        return nullptr;

    for (auto* parameter : processor.getParameters())
        if (getParameterIdentifier (*parameter) == identifier)
            return parameter;

    return nullptr;
}

SamplingResult runSampling (const SamplingContext& ctx, const SamplingCallbacks& callbacks)
{
    const auto& settings = ctx.settings;
    SamplingResult result;

    const auto log = [&] (const juce::String& message, LogLevel level = LogLevel::info)
    {
        if (callbacks.onLog != nullptr)
            callbacks.onLog (message, level);
    };

    const auto shouldAbort = [&]
    {
        return callbacks.shouldAbort != nullptr && callbacks.shouldAbort();
    };

    const auto runOnMessageThread = [&callbacks] (std::function<void()> fn)
    {
        if (callbacks.runOnMessageThread != nullptr)
            callbacks.runOnMessageThread (std::move (fn));
        else
            fn();
    };

    // Plugin-State vor jedem Sample wiederherstellen (auf dem Message-Thread)
    std::function<void()> restoreState;

    if (settings.restoreStatePerSample && ! ctx.pluginState.isEmpty())
    {
        restoreState = [&ctx, &runOnMessageThread, state = std::make_shared<juce::MemoryBlock> (ctx.pluginState)]
        {
            auto* processor = &ctx.processor;
            runOnMessageThread ([processor, state] { processor->setStateInformation (state->getData(), (int) state->getSize()); });
        };
    }

    // Round Robin: Variation über MIDI CC oder einen Plugin-Parameter
    const auto realRoundRobin = settings.roundRobinSource == RoundRobinSource::real;
    const auto varies = settings.roundRobinSource == RoundRobinSource::midiCC;
    juce::AudioProcessorParameter* parameter = nullptr;
    auto originalParameterValue = 0.0f;

    if (varies && settings.variationTarget == VariationTarget::parameter)
    {
        parameter = findParameter (ctx.processor, settings.variationParameterId);

        if (parameter == nullptr)
        {
            result.error = "Variation parameter not found: "_u
                         + (settings.variationParameterName.isNotEmpty() ? settings.variationParameterName
                                                                         : settings.variationParameterId);
            return result;
        }

        originalParameterValue = parameter->getValue();
    }

    const auto createRenderer = [&] (RenderMode mode, juce::String& error) -> std::unique_ptr<NoteRenderer>
    {
        if (mode != RenderMode::realtime)
            return std::make_unique<OfflineRenderer> (ctx.processor, settings.sampleRate);

        if (ctx.deviceManager == nullptr)
        {
            error = "Realtime render is not available here"_u;
            return nullptr;
        }

        auto renderer = std::make_unique<RealtimeRenderer> (ctx.processor, *ctx.deviceManager);
        error = renderer->getInitialisationError();

        if (error.isNotEmpty())
            return nullptr;

        if (! juce::approximatelyEqual (renderer->getSampleRate(), settings.sampleRate))
            log ("Realtime render at " + juce::String (renderer->getSampleRate(), 0) + " Hz (audio device), "_u
                 "output is converted to " + juce::String (settings.sampleRate, 0) + " Hz");

        return renderer;
    };

    auto mode = settings.renderMode == RenderMode::realtime ? RenderMode::realtime : RenderMode::offline;

    if (settings.renderMode == RenderMode::automatic && ctx.preferRealtime && ctx.deviceManager != nullptr)
    {
        mode = RenderMode::realtime;
        log ("Plugin is remembered for realtime render"_u);
    }

    // Automatik: Liefert offline Stille, wird Echtzeit probiert (höchstens dreimal)
    auto canFallBack = settings.renderMode == RenderMode::automatic && mode == RenderMode::offline
                       && ctx.deviceManager != nullptr;
    auto fallbackTests = 0;

    juce::String error;
    auto renderer = createRenderer (mode, error);

    if (renderer == nullptr)
    {
        result.error = error;
        return result;
    }

    log (juce::String (ctx.requests.size()) + " samples, " + (mode == RenderMode::realtime ? "Realtime" : "Offline")
         + " render, destination: " + ctx.outputFolder.getFullPathName());

    if (realRoundRobin)
        log ("Round Robin: Real, " + juce::String (getRoundRobinCount (settings))
             + " variations per note (state only before the first variation, no reset)");
    else if (varies)
        log ("Round Robin: "_u + (settings.roundRobinMode == RoundRobinMode::linked ? "Linked" : "Independent") + ", "
             + (parameter != nullptr ? "Parameter " + parameter->getName (64)
                                     : "CC " + juce::String (settings.variationController))
             + " from " + juce::String (settings.variationFrom) + " to " + juce::String (settings.variationTo));

    juce::AudioBuffer<float> firstVariant;      // Real RR: erste Variante der aktuellen Note
    auto allVariantsIdentical = true;

    const auto total = ctx.requests.size();

    for (int index = 0; index < total; ++index)
    {
        if (shouldAbort())
        {
            result.aborted = true;
            break;
        }

        const auto& request = ctx.requests.getReference (index);

        if (callbacks.onSampleStarted != nullptr)
            callbacks.onSampleStarted (index, total, request);

        // Real RR: Das Instrument soll zwischen den Varianten weiterzählen
        const auto continuesVariants = realRoundRobin && request.roundRobin > 0;
        const auto restoreNow = restoreState != nullptr && ! continuesVariants;
        const auto setsParameter = parameter != nullptr && request.variation >= 0;
        std::function<void()> prepare;

        if (restoreNow || setsParameter)
        {
            prepare = [&, restoreNow, setsParameter, value = request.variation]
            {
                if (restoreNow)
                    restoreState();

                if (setsParameter)
                    runOnMessageThread ([parameter, value] { parameter->setValueNotifyingHost ((float) value / 127.0f); });
            };
        }

        const auto renderNote = [&] (NoteRenderer& r) -> std::optional<RenderedNote>
        {
            NoteRenderSpec spec;
            spec.note = request.note;
            spec.velocity = request.velocity;
            spec.channel = settings.midiChannel;
            spec.holdSamples = juce::roundToInt (settings.holdSeconds * r.getSampleRate());
            spec.maxTailSamples = juce::roundToInt (settings.maxTailSeconds * r.getSampleRate());
            spec.tailThresholdDb = settings.tailThresholdDb;

            if (varies && parameter == nullptr && request.variation >= 0)
            {
                spec.controller = settings.variationController;
                spec.controllerValue = request.variation;
            }

            r.prepareNextNote (prepare, ! continuesVariants);
            return r.render (spec, shouldAbort, error);
        };

        auto rendered = renderNote (*renderer);

        if (rendered.has_value() && canFallBack && ! hasSound (rendered->audio, settings.startThresholdDb))
        {
            ++fallbackTests;
            canFallBack = fallbackTests < 3;
            log (describe (request) + ": silent offline, testing realtime render ...", LogLevel::warning);

            renderer = nullptr;
            juce::String realtimeError;

            if (auto realtime = createRenderer (RenderMode::realtime, realtimeError))
            {
                auto realtimeResult = renderNote (*realtime);

                if (realtimeResult.has_value() && hasSound (realtimeResult->audio, settings.startThresholdDb))
                {
                    log ("Plugin delivers silence offline: switching to realtime render", LogLevel::warning);
                    mode = RenderMode::realtime;
                    canFallBack = false;
                    renderer = std::move (realtime);
                    rendered = std::move (realtimeResult);

                    if (callbacks.onRealtimeFallback != nullptr)
                        callbacks.onRealtimeFallback();
                }
                else if (! realtimeResult.has_value())
                {
                    rendered = std::nullopt;
                }
            }
            else
            {
                log ("Realtime render not possible: "_u + realtimeError, LogLevel::warning);
                canFallBack = false;
            }

            if (renderer == nullptr)
                renderer = createRenderer (RenderMode::offline, error);
        }
        else if (rendered.has_value() && mode == RenderMode::offline && hasSound (rendered->audio, settings.startThresholdDb))
        {
            canFallBack = false;    // Offline funktioniert
        }

        if (! rendered.has_value())
        {
            if (error.isNotEmpty())
                result.error = describe (request) + ": " + error;
            else
                result.aborted = true;

            break;
        }

        auto audio = std::move (rendered->audio);

        if (rendered->hitMaxTail)
            log (describe (request) + ": tail cut off at " + juce::String (settings.maxTailSeconds, 1)
                 + " s", LogLevel::warning);

        audio = resample (audio, renderer->getSampleRate(), settings.sampleRate);

        // Real RR: Varianten, die sich nicht unterscheiden, deuten auf ein Instrument ohne eigenes Round Robin
        if (realRoundRobin && request.roundRobinCount > 1)
        {
            if (request.roundRobin == 0)
            {
                firstVariant.makeCopyOf (audio);
                allVariantsIdentical = true;
            }
            else
            {
                allVariantsIdentical = allVariantsIdentical
                                    && measureDifferenceDb (firstVariant, audio) < identicalDifferenceDb;

                if (request.roundRobin == request.roundRobinCount - 1 && allVariantsIdentical)
                {
                    ++result.numIdenticalGroups;
                    log (getNoteName (request.note) + " v" + juce::String (request.velocity)
                         + ": variations identical, the instrument does not vary by itself (use MIDI CC?)",
                         LogLevel::warning);
                }
            }
        }

        TrimSettings trim;
        trim.trimStart = settings.trimStart;
        trim.startThresholdDb = settings.startThresholdDb;
        trim.endThresholdDb = settings.tailThresholdDb;
        trim.preRollSamples = juce::roundToInt (settings.preRollMs * 0.001 * settings.sampleRate);
        trim.fadeOutSamples = juce::roundToInt (settings.fadeOutMs * 0.001 * settings.sampleRate);
        trim.windowSamples = juce::roundToInt (0.01 * settings.sampleRate);

        if (! trimSample (audio, trim))
        {
            ++result.numSilent;
            log (describe (request) + ": no signal, skipped"_u, LogLevel::warning);
            continue;
        }

        if (! juce::approximatelyEqual (settings.gainDb, 0.0f) || ! juce::approximatelyEqual (settings.pan, 0.0f))
            applyGainAndPan (audio, settings.gainDb, settings.pan);

        if (settings.bitDepth < 32 && audio.getMagnitude (0, audio.getNumSamples()) > 1.0f)
        {
            ++result.numClipped;
            log (describe (request) + ": clipped (reduce gain)"_u, LogLevel::warning);
        }

        const auto file = ctx.outputFolder.getChildFile (makeSampleFileName (ctx.filePrefix, request));

        if (const auto writeError = writeWavFile (file, audio, settings.sampleRate, settings.bitDepth, request.note);
            writeError.isNotEmpty())
        {
            result.error = writeError;
            break;
        }

        ++result.numWritten;

        if (callbacks.onSampleWritten != nullptr)
            callbacks.onSampleWritten ({ file, request, settings.sampleRate, audio.getNumSamples(), audio.getNumChannels() });

        log (describe (request) + " -> " + file.getFileName() + " ("
             + juce::String ((double) audio.getNumSamples() / settings.sampleRate, 2) + " s)");
    }

    // Parameter wieder auf den Wert vor dem Lauf
    if (parameter != nullptr)
        runOnMessageThread ([parameter, originalParameterValue] { parameter->setValueNotifyingHost (originalParameterValue); });

    result.modeUsed = mode;
    return result;
}

//==============================================================================
/** Verbindung vom Job-Thread zum Message-Thread. job wird nur auf dem
    Message-Thread gelesen und geschrieben (nullptr, sobald der Job zerstört wird). */
struct SamplingJob::Relay
{
    SamplingJob* job = nullptr;
};

SamplingJob::SamplingJob (SamplingContext contextToUse)
    : juce::Thread ("Sampling"),
      context (std::move (contextToUse)),
      relay (std::make_shared<Relay>())
{
    relay->job = this;
}

SamplingJob::~SamplingJob()
{
    relay->job = nullptr;
    stopThread (15000);
}

void SamplingJob::start()
{
    startThread();
}

void SamplingJob::cancel()
{
    signalThreadShouldExit();
}

void SamplingJob::post (std::function<void (SamplingJob&)> fn)
{
    juce::MessageManager::callAsync ([r = relay, call = std::move (fn)]
    {
        if (r->job != nullptr)
            call (*r->job);
    });
}

void SamplingJob::run()
{
    SamplingCallbacks callbacks;

    callbacks.onSampleStarted = [this] (int index, int total, const SampleRequest& request)
    {
        post ([index, total, request] (SamplingJob& job)
        {
            if (job.onSampleStarted != nullptr)
                job.onSampleStarted (index, total, request);
        });
    };

    callbacks.onLog = [this] (const juce::String& message, LogLevel level)
    {
        post ([message, level] (SamplingJob& job)
        {
            if (job.onLog != nullptr)
                job.onLog (message, level);
        });
    };

    callbacks.onRealtimeFallback = [this]
    {
        post ([] (SamplingJob& job)
        {
            if (job.onRealtimeFallback != nullptr)
                job.onRealtimeFallback();
        });
    };

    callbacks.onSampleWritten = [this] (const RecordedSample& sample)
    {
        post ([sample] (SamplingJob& job)
        {
            if (job.onSampleWritten != nullptr)
                job.onSampleWritten (sample);
        });
    };

    callbacks.shouldAbort = [this] { return threadShouldExit(); };

    callbacks.runOnMessageThread = [this] (std::function<void()> fn)
    {
        auto done = std::make_shared<juce::WaitableEvent>();

        juce::MessageManager::callAsync ([call = std::move (fn), done]
        {
            call();
            done->signal();
        });

        while (! done->wait (20))
            if (threadShouldExit())
                return false;

        return true;
    };

    const auto result = runSampling (context, callbacks);

    post ([result] (SamplingJob& job)
    {
        if (job.onFinished != nullptr)
            job.onFinished (result);
    });
}

} // namespace autosampler
