#pragma once

#include "engine/PerformTools.h"
#include "engine/SamplerEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace autosampler
{

/** Das, was der AudioProcessorPlayer live spielt:

    MIDI (Keyboard, Hardware) ─► Chord ─► Arp/Sequencer ─┬─► Plugin  ─┐
                                                          └─► Sampler ─┴─► Monitor-Reverb ─► Ausgang

    Die Quelle (Plugin oder Samples) bestimmt nur, wohin die Noten gehen; beide
    laufen weiter, damit Release-Fahnen beim Umschalten (A/B) ausklingen. Die
    Hörprüfung aus dem Editor spielt immer über den Sampler.

    Message-Thread: setPlugin, setInstrument, setSettings, panic, Hörprüfung.
    Der Audio-Thread übernimmt Einstellungen und Instrument per SpinLock ohne zu
    warten; alte Instrumente werden auf dem Message-Thread freigegeben, wenn
    keine Stimme sie mehr hält. */
class LivePlayProcessor final : public juce::AudioProcessor,
                                public SampleAudition,
                                private juce::Timer
{
public:
    LivePlayProcessor();
    ~LivePlayProcessor() override;

    /** Plugin für die Wiedergabe, nullptr zum Abkoppeln (z. B. während des Samplings).
        Das Plugin wird hier vorbereitet und beim Abkoppeln freigegeben. */
    void setPlugin (juce::AudioPluginInstance*);

    void setInstrument (std::shared_ptr<const SamplerInstrument>);

    void setSettings (const LivePlaySettings&);
    const LivePlaySettings& getSettings() const noexcept        { return settings; }

    /** Alle Noten aus, Arp/Sequencer und Chord vergessen, Hall leeren. */
    void panic();

    int getCurrentStep() const noexcept                         { return pattern.getCurrentStep(); }
    int getNumActiveVoices() const noexcept                     { return sampler.getNumActiveVoices(); }

    // SampleAudition
    void startAudition (juce::int64 sampleId, int velocity) override;
    void stopAudition() override;
    int getAuditionPosition (juce::int64 sampleId) const override;

    // AudioProcessor
    const juce::String getName() const override                 { return "Live Play"; }
    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    double getTailLengthSeconds() const override                { return 0.0; }
    bool acceptsMidi() const override                           { return true; }
    bool producesMidi() const override                          { return false; }
    juce::AudioProcessorEditor* createEditor() override         { return nullptr; }
    bool hasEditor() const override                             { return false; }
    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return {}; }
    void changeProgramName (int, const juce::String&) override  {}
    void getStateInformation (juce::MemoryBlock&) override      {}
    void setStateInformation (const void*, int) override        {}

private:
    class PlayHead;

    struct Command
    {
        enum class Type { auditionStart, auditionStop, panic };

        Type type = Type::panic;
        juce::int64 sampleId = 0;
        int velocity = 0;
    };

    void pushCommand (const Command&);
    void handleCommands();
    void applySettings();
    void preparePlugin (juce::AudioPluginInstance&);
    void allocateBuffers (int blockSize);
    void timerCallback() override;

    // Message-Thread
    LivePlaySettings settings;
    std::vector<std::shared_ptr<const SamplerInstrument>> releasePool;

    // Übergabe an den Audio-Thread
    juce::SpinLock settingsLock, instrumentLock;
    LivePlaySettings pendingSettings;
    bool settingsChanged = true;
    std::shared_ptr<const SamplerInstrument> pendingInstrument;
    bool instrumentChanged = false;

    static constexpr int commandCapacity = 64;
    juce::AbstractFifo commandFifo { commandCapacity };
    std::array<Command, commandCapacity> commands;

    // Audio-Thread (das Plugin zusätzlich unter dem Callback-Lock)
    juce::AudioPluginInstance* plugin = nullptr;
    LivePlaySettings audioSettings;
    PlaySource activeSource = PlaySource::plugin;
    bool reverbActive = false;
    SamplerEngine sampler;
    ChordProcessor chord;
    PatternPlayer pattern;
    juce::Reverb reverb;
    juce::AudioBuffer<float> pluginBuffer, mixBuffer;
    juce::MidiBuffer chordMidi, patternMidi, pluginMidi, samplerMidi, scratchMidi;
    std::unique_ptr<PlayHead> pluginPlayHead;

    double currentSampleRate = 0.0;
    int currentBlockSize = 0;
    std::atomic<bool> prepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePlayProcessor)
};

} // namespace autosampler
