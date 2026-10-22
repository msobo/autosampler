#pragma once

#include "SamplerInstrument.h"
#include "Session.h"

namespace autosampler
{

/** Quelle für Sampler und Export aus einem Sample der Session (mit Loop-Punkten). */
SamplerZoneSource makeSamplerSource (const Session&, const juce::ValueTree& sampleTree);

/** Quellen für den Sampler aus allen Samples der Session (mit Loop-Punkten). */
std::vector<SamplerZoneSource> getSamplerSources (const Session&);

/** Hält ein spielbares Instrument aus den Samples der Session aktuell:
    Nach jeder Änderung (neue Aufnahme, Loop-Punkte, Crossfade, Laden einer
    Session, Zone Smoothing im Export) wird kurz danach im Hintergrund neu gebaut. Rohdaten und
    gerenderte Loops bleiben im Cache, unveränderte Samples werden nicht neu
    geladen. Nur auf dem Message-Thread verwenden. */
class SamplerLoader final : private juce::ValueTree::Listener,
                            private juce::Timer
{
public:
    explicit SamplerLoader (Session&);
    ~SamplerLoader() override;

    /** Neues Instrument (Message-Thread). */
    std::function<void (std::shared_ptr<const SamplerInstrument>)> onInstrumentReady;
    std::function<void()> onStatusChanged;

    bool isLoading() const noexcept             { return loading; }
    juce::String getStatus() const              { return status; }

    /** Baut sofort neu, auch ohne Änderung (z. B. nachdem Dateien ersetzt wurden). */
    void reload();

private:
    struct Relay;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override         { scheduleBuild(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override  { scheduleBuild(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override          { scheduleBuild(); }
    void timerCallback() override;

    void scheduleBuild();
    void buildFinished (int generation, std::shared_ptr<const SamplerInstrument>, const juce::StringArray& errors);

    Session& session;
    juce::ValueTree sessionState;
    juce::ThreadPool pool;
    std::shared_ptr<Relay> relay;
    std::shared_ptr<SamplerAudioCache> cache;

    std::vector<SamplerZoneSource> lastSources;
    float lastZoneSmoothing = 0.0f;
    bool hasBuilt = false, force = false, loading = false;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerLoader)
};

} // namespace autosampler
