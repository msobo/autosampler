#pragma once

#include "LivePlayProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace autosampler
{

/** Zentrale Host-Engine: Audio-/MIDI-Geräte, Plugin-Liste, geladenes
    Instrument und das MIDI-Routing (In-App-Keyboard und Hardware-Eingänge).
    Gespielt wird immer über den LivePlayProcessor (Plugin oder Samples,
    Chord, Arp/Sequencer, Monitor-Reverb).
*/
class PluginHost final : public juce::ChangeBroadcaster,
                         private juce::ChangeListener,
                         private juce::MidiInputCallback,
                         private juce::MidiKeyboardState::Listener
{
public:
    explicit PluginHost (juce::PropertiesFile& settings);
    ~PluginHost() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept         { return deviceManager; }
    juce::AudioPluginFormatManager& getFormatManager() noexcept   { return formatManager; }
    juce::KnownPluginList& getKnownPluginList() noexcept          { return knownPlugins; }
    juce::MidiKeyboardState& getKeyboardState() noexcept          { return keyboardState; }
    juce::PropertiesFile& getSettings() noexcept                  { return settings; }
    juce::File getDeadMansPedalFile() const;

    /** Alle bekannten Instrumente, alphabetisch sortiert. */
    juce::Array<juce::PluginDescription> getInstruments() const;

    using LoadCallback = std::function<void (const juce::String& error)>;

    /** Lädt ein Plugin asynchron; das bisherige wird vorher entladen.
        Der Callback kommt auf dem Message-Thread (leerer String = Erfolg). */
    void loadPlugin (const juce::PluginDescription&, LoadCallback onDone);
    void unloadPlugin();

    /** Wird synchron aufgerufen, bevor die Plugin-Instanz zerstört wird.
        Hier müssen Editor-Fenster geschlossen werden. */
    std::function<void()> onBeforePluginUnload;

    juce::AudioPluginInstance* getPlugin() const noexcept         { return plugin.get(); }
    LivePlayProcessor& getLivePlay() noexcept                     { return livePlay; }

    /** Lässt alle hängenden Noten los (Panic). */
    void allNotesOff();

    /** Koppelt das Plugin vom Live-Spiel ab (false), z. B. für einen
        Sampling-Lauf, und wieder an (true). Der Sampler bleibt angekoppelt. */
    void setLivePlaybackEnabled (bool shouldBeEnabled);
    bool isLivePlaybackEnabled() const noexcept                   { return livePlaybackEnabled; }

    /** Plugins, die offline Stille liefern, werden für Echtzeit-Render vorgemerkt. */
    bool prefersRealtimeRendering (const juce::PluginDescription&) const;
    void setPrefersRealtimeRendering (const juce::PluginDescription&, bool shouldPrefer);

    void saveSettings();

    /** MIDI Learn: Der nächste Controller eines Hardware-Eingangs wird auf dem
        Message-Thread gemeldet (Controller-Nummer). nullptr beendet das Lernen. */
    void setMidiLearnCallback (std::function<void (int controller)>);

private:
    void loadPluginImpl (const juce::PluginDescription&, LoadCallback onDone, bool isRetry);

    /** Scannt die Datei eines nicht ladbaren VST3 neu. Liefert die neue
        Beschreibung, falls sie sich vom alten Eintrag unterscheidet. */
    std::optional<juce::PluginDescription> rescanFile (const juce::PluginDescription& stale);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;

    juce::PropertiesFile& settings;

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::AudioProcessorPlayer player;
    LivePlayProcessor livePlay;
    juce::MidiKeyboardState keyboardState;

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    int loadGeneration = 0;
    bool livePlaybackEnabled = true;

    std::function<void (int)> midiLearnCallback;    ///< nur Message-Thread
    std::atomic<bool> midiLearnActive { false };
    juce::WeakReference<PluginHost> learnTarget;    ///< auf dem Message-Thread angelegt, vom MIDI-Thread kopiert

    JUCE_DECLARE_WEAK_REFERENCEABLE (PluginHost)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHost)
};

} // namespace autosampler
