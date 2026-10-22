#pragma once

#include "SamplingSettings.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace autosampler
{

/** Alles, was ein Sampling-Lauf braucht. Das Plugin darf während des Laufs
    von niemand anderem verarbeitet werden (AudioProcessorPlayer abkoppeln). */
struct SamplingContext
{
    explicit SamplingContext (juce::AudioProcessor& processorToUse) : processor (processorToUse) {}

    juce::AudioProcessor& processor;
    juce::AudioDeviceManager* deviceManager = nullptr;  ///< nullptr: kein Echtzeit-Render
    SamplingSettings settings;
    juce::Array<SampleRequest> requests;
    juce::MemoryBlock pluginState;          ///< Leer: State nicht wiederherstellen
    juce::File outputFolder;                ///< Muss existieren
    juce::String filePrefix;
    bool preferRealtime = false;            ///< Plugin ist als "nur Echtzeit" vorgemerkt
};

enum class LogLevel { info, warning, error };

struct SamplingResult
{
    int numWritten = 0;
    int numSilent = 0;
    int numClipped = 0;             ///< Nach Gain übersteuert (PCM)
    int numIdenticalGroups = 0;     ///< Real RR: Varianten ohne hörbaren Unterschied
    bool aborted = false;
    juce::String error;
    RenderMode modeUsed = RenderMode::offline;
};

/** Rückmeldungen eines Laufs. Alle Funktionen sind optional und werden im
    Thread des Laufs aufgerufen. */
struct SamplingCallbacks
{
    std::function<void (int index, int total, const SampleRequest&)> onSampleStarted;
    std::function<void (const juce::String& message, LogLevel)> onLog;
    std::function<void()> onRealtimeFallback;
    std::function<void (const RecordedSample&)> onSampleWritten;
    std::function<bool()> shouldAbort;

    /** Führt fn auf dem Message-Thread aus und wartet darauf (für
        setStateInformation). Liefert false bei Abbruch. nullptr: direkt aufrufen. */
    std::function<bool (std::function<void()> fn)> runOnMessageThread;
};

/** Kennung eines Parameters für die Variation: Parameter-ID des gehosteten
    Plugins (VST3/AU), sonst die ID oder der Index. */
juce::String getParameterIdentifier (const juce::AudioProcessorParameter&);

/** Parameter mit dieser Kennung, nullptr wenn es keinen gibt. */
juce::AudioProcessorParameter* findParameter (juce::AudioProcessor&, const juce::String& identifier);

/** Führt einen Lauf synchron im aufrufenden Thread aus: pro Sample Plugin
    vorbereiten (State, Variation), rendern (offline oder Echtzeit), trimmen,
    Gain/Pan, WAV schreiben. Im Automatik-Modus wird bei Stille offline auf
    Echtzeit gewechselt. Real RR stellt den State nur vor der ersten Variante
    einer Note wieder her, damit das Instrument weiterzählen kann. */
SamplingResult runSampling (const SamplingContext&, const SamplingCallbacks&);

//==============================================================================
/** Führt runSampling() in einem Hintergrund-Thread aus und meldet alles
    asynchron auf dem Message-Thread. Der Destruktor bricht ab und wartet. */
class SamplingJob final : private juce::Thread
{
public:
    explicit SamplingJob (SamplingContext);
    ~SamplingJob() override;

    void start();
    void cancel();

    // Werden auf dem Message-Thread aufgerufen
    std::function<void (int index, int total, const SampleRequest&)> onSampleStarted;
    std::function<void (const juce::String& message, LogLevel)> onLog;
    std::function<void()> onRealtimeFallback;
    std::function<void (const RecordedSample&)> onSampleWritten;
    std::function<void (const SamplingResult&)> onFinished;

private:
    struct Relay;

    void run() override;
    void post (std::function<void (SamplingJob&)>);

    SamplingContext context;
    std::shared_ptr<Relay> relay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplingJob)
};

} // namespace autosampler
