#pragma once

#include "AudioAnalysis.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace autosampler
{

/** Was für eine einzelne Note gerendert werden soll. */
struct NoteRenderSpec
{
    int note = 60;
    int velocity = 127;
    int channel = 1;
    int holdSamples = 0;        ///< Dauer von Note-On bis Note-Off
    int maxTailSamples = 0;     ///< Maximale Aufnahme nach dem Note-Off
    float tailThresholdDb = -70.0f;
    int controller = -1;        ///< MIDI CC direkt vor dem Note-On, -1 = keiner
    int controllerValue = 0;
};

struct RenderedNote
{
    juce::AudioBuffer<float> audio;     ///< Beginnt mit dem Note-On, latenzkompensiert
    bool hitMaxTail = false;            ///< Ausklang wurde beim Maximum abgeschnitten
};

//==============================================================================
/** Nimmt den Ausgang des Plugins für eine Note auf: erzeugt die MIDI-Events,
    zieht die Plugin-Latenz ab und erkennt das Ende des Ausklangs.

    begin() alloziert und läuft außerhalb des Audio-Threads; addMidi() und
    consume() sind echtzeitfähig.
*/
class NoteCapture
{
public:
    void begin (const NoteRenderSpec&, int latencySamples, double sampleRate, int numChannels);

    /** Fügt die MIDI-Events für den nächsten Block der Länge numSamples ein. */
    void addMidi (juce::MidiBuffer&, int numSamples) const;

    /** Übernimmt den Ausgang des zuletzt verarbeiteten Blocks.
        Liefert true, sobald die Note fertig aufgenommen ist. */
    bool consume (const juce::AudioBuffer<float>& pluginOutput, int numSamples) noexcept;

    bool isFinished() const noexcept            { return finished.load(); }
    int getNumRecorded() const noexcept         { return numRecorded.load(); }

    /** Gibt die Aufnahme zurück (nach isFinished()). */
    RenderedNote takeResult();

private:
    NoteRenderSpec spec;
    int latency = 0;
    int blockPosition = 0;  ///< Position im Plugin-Ausgang (inkl. Latenz)
    juce::AudioBuffer<float> recording;
    std::unique_ptr<TailDetector> tailDetector;
    std::atomic<int> numRecorded { 0 };
    std::atomic<bool> finished { false };
    bool hitMaxTail = false;
};

//==============================================================================
/** Spielhead mit festem Tempo, damit tempo-synchrone Plugins (LFOs, Arps)
    definierte Werte bekommen. */
class FixedTempoPlayHead final : public juce::AudioPlayHead
{
public:
    explicit FixedTempoPlayHead (double sampleRateToUse) : sampleRate (sampleRateToUse) {}

    juce::Optional<PositionInfo> getPosition() const override;

    void setSampleRate (double newRate) noexcept    { sampleRate = newRate; }
    void reset() noexcept                           { position = 0; }
    void advance (int numSamples) noexcept          { position += numSamples; }

private:
    double sampleRate;
    std::atomic<int64_t> position { 0 };
};

//==============================================================================
class NoteRenderer
{
public:
    virtual ~NoteRenderer() = default;

    virtual double getSampleRate() const = 0;

    /** Bringt das Plugin in einen definierten Zustand: Noten aus, optional
        Plugin-State wiederherstellen (restoreState darf nullptr sein), reset()
        (resetProcessor = false lässt interne Zähler wie Round Robin stehen), ausklingen. */
    virtual void prepareNextNote (const std::function<void()>& restoreState, bool resetProcessor = true) = 0;

    /** Rendert eine Note. nullopt bei Abbruch oder Fehler (error ist dann
        bei Fehlern gesetzt). */
    virtual std::optional<RenderedNote> render (const NoteRenderSpec&,
                                                const std::function<bool()>& shouldAbort,
                                                juce::String& error) = 0;
};

//==============================================================================
/** Rendert offline (setNonRealtime) im aufrufenden Thread, schneller als Echtzeit. */
class OfflineRenderer final : public NoteRenderer
{
public:
    OfflineRenderer (juce::AudioProcessor&, double sampleRate, int blockSize = 512);
    ~OfflineRenderer() override;

    double getSampleRate() const override   { return sampleRate; }
    void prepareNextNote (const std::function<void()>& restoreState, bool resetProcessor = true) override;
    std::optional<RenderedNote> render (const NoteRenderSpec&, const std::function<bool()>& shouldAbort,
                                        juce::String& error) override;

private:
    void processBlock (juce::MidiBuffer&);

    juce::AudioProcessor& processor;
    double sampleRate;
    int blockSize;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    FixedTempoPlayHead playHead;
    NoteCapture capture;
};

//==============================================================================
/** Spielt das Plugin in Echtzeit über das Audio-Gerät (hörbar) und nimmt
    dabei den Ausgang auf. Für Plugins, die offline Stille oder Artefakte liefern.
    render() blockiert den aufrufenden Thread, der Audio-Thread arbeitet.
*/
class RealtimeRenderer final : public NoteRenderer,
                               private juce::AudioIODeviceCallback
{
public:
    RealtimeRenderer (juce::AudioProcessor&, juce::AudioDeviceManager&);
    ~RealtimeRenderer() override;

    /** Leer, wenn das Gerät bereit ist. */
    juce::String getInitialisationError() const     { return initError; }

    double getSampleRate() const override           { return sampleRate; }
    void prepareNextNote (const std::function<void()>& restoreState, bool resetProcessor = true) override;
    std::optional<RenderedNote> render (const NoteRenderSpec&, const std::function<bool()>& shouldAbort,
                                        juce::String& error) override;

private:
    enum class Phase { idle, paused, recording, done };

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    void setPausedAndWait (bool shouldPause);

    juce::AudioProcessor& processor;
    juce::AudioDeviceManager& deviceManager;
    juce::String initError;
    double sampleRate = 0.0;
    int blockSize = 0;
    bool callbackAdded = false;

    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    FixedTempoPlayHead playHead { 48000.0 };
    NoteCapture capture;

    std::atomic<Phase> phase { Phase::idle };
    std::atomic<bool> pauseAcknowledged { false };
    std::atomic<bool> abortRequested { false };
    std::atomic<bool> sendAllNotesOff { false };
    std::atomic<bool> deviceChanged { false };
    int midiChannel = 1;
};

} // namespace autosampler
