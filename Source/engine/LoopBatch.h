#pragma once

#include "Session.h"

namespace autosampler
{

/** Loop-Suche und Bewertung für die Samples einer Session auf einem Thread-Pool.

    - search(): sucht Loops für die übergebenen Samples (gesperrte werden
      übersprungen). Die Ergebnisse werden gesammelt und am Ende als ein
      Undo-Schritt übernommen. Eine neue Suche ersetzt eine laufende.
    - Nach Änderungen an Loop-Punkten oder Crossfade (Ziehen, Nudge, Undo)
      wird der Score des Samples kurz danach im Hintergrund neu berechnet.

    Nur auf dem Message-Thread verwenden. Die Jobs bekommen Kopien aller Daten;
    der ValueTree wird nur auf dem Message-Thread angefasst. */
class LoopBatch final : private juce::ValueTree::Listener,
                        private juce::Timer
{
public:
    explicit LoopBatch (Session&);
    ~LoopBatch() override;

    void search (const std::vector<juce::ValueTree>& samples, const LoopSearchSettings&);
    void cancel();

    bool isSearching() const noexcept       { return searchTotal > 0; }
    int getNumDone() const noexcept         { return searchDone; }
    int getNumTotal() const noexcept        { return searchTotal; }

    /** Fortschritt und Ende einer Suche: (übernommen, ohne Ergebnis, letzte Meldung). */
    std::function<void()> onProgress;
    std::function<void (int numApplied, int numFailed, const juce::String& message)> onFinished;

private:
    struct Input
    {
        juce::File file;
        LoopHints hints;
        LoopSearchSettings settings;
        int loopStart = 0, loopEnd = 0, crossfade = 0;  // nur für die Bewertung
        CrossfadeMode mode = CrossfadeMode::equalPower;
    };

    struct Relay;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void timerCallback() override;

    bool belongsToSession (const juce::ValueTree&) const;
    Input makeInput (const juce::ValueTree&) const;
    void searchFinished (int generation, int index, const LoopResult&);
    void scoreFinished (int requestId, const Input&, float score);
    void applySearchResults();

    Session& session;
    juce::ValueTree sessionState;
    juce::ThreadPool pool;
    std::shared_ptr<Relay> relay;

    int searchDone = 0, searchTotal = 0;
    std::vector<juce::ValueTree> searchSamples;
    std::vector<std::optional<LoopResult>> searchResults;
    CrossfadeMode searchMode = CrossfadeMode::equalPower;

    std::vector<juce::ValueTree> scorePending;
    std::map<int, juce::ValueTree> scoreRequests;
    int nextScoreRequest = 0;
    bool applying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopBatch)
};

} // namespace autosampler
