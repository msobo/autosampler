#include "PluginHost.h"
#include "BusLayout.h"
#include "PluginScanner.h"
#include "Text.h"

namespace autosampler
{

namespace
{
    constexpr const char* pluginListKey  = "pluginList";
    constexpr const char* audioDeviceKey = "audioDeviceState";
    constexpr const char* realtimePluginsKey = "realtimeRenderPlugins";

    // Hardware-MIDI wird direkt ans Plugin geschickt und nur zur Anzeige in den
    // KeyboardState gespiegelt. Dieses Flag verhindert, dass der Listener die
    // Noten dabei ein zweites Mal weiterleitet.
    thread_local bool mirroringHardwareMidi = false;
}

PluginHost::PluginHost (juce::PropertiesFile& settingsToUse)
    : settings (settingsToUse)
{
    juce::addDefaultFormatsToManager (formatManager);

    knownPlugins.setCustomScanner (std::make_unique<OutOfProcessScanner>());

    if (auto xml = settings.getXmlValue (pluginListKey))
        knownPlugins.recreateFromXml (*xml);

    knownPlugins.addChangeListener (this);

    const auto savedDeviceState = settings.getXmlValue (audioDeviceKey);
    const auto error = deviceManager.initialise (2, 2, savedDeviceState.get(), true);

    if (error.isNotEmpty())
        DBG ("Audio-Gerät konnte nicht initialisiert werden: "_u + error);

    deviceManager.addChangeListener (this);
    player.setProcessor (&livePlay);
    deviceManager.addAudioCallback (&player);
    deviceManager.addMidiInputDeviceCallback ({}, this);

    keyboardState.addListener (this);
}

PluginHost::~PluginHost()
{
    keyboardState.removeListener (this);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (&player);
    deviceManager.removeChangeListener (this);
    knownPlugins.removeChangeListener (this);

    unloadPlugin();
    player.setProcessor (nullptr);
    saveSettings();
}

juce::File PluginHost::getDeadMansPedalFile() const
{
    return settings.getFile().getSiblingFile ("RecentlyCrashedPlugins");
}

juce::Array<juce::PluginDescription> PluginHost::getInstruments() const
{
    juce::Array<juce::PluginDescription> result;

    for (const auto& desc : knownPlugins.getTypes())
        if (desc.isInstrument)
            result.add (desc);

    std::sort (result.begin(), result.end(), [] (const auto& a, const auto& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });

    return result;
}

void PluginHost::loadPlugin (const juce::PluginDescription& desc, LoadCallback callback)
{
    loadPluginImpl (desc, std::move (callback), false);
}

std::optional<juce::PluginDescription> PluginHost::rescanFile (const juce::PluginDescription& stale)
{
    if (stale.pluginFormatName != "VST3")
        return std::nullopt;

    juce::AudioPluginFormat* format = nullptr;

    for (auto* f : formatManager.getFormats())
        if (f->getName() == stale.pluginFormatName)
            format = f;

    if (format == nullptr)
        return std::nullopt;

    // Alten Eintrag entfernen, sonst bleibt er neben dem korrigierten stehen
    knownPlugins.removeType (stale);

    juce::OwnedArray<juce::PluginDescription> found;
    knownPlugins.scanAndAddFile (stale.fileOrIdentifier, false, found, *format);
    knownPlugins.scanFinished();

    for (const auto* d : found)
        if (d->name == stale.name && d->uniqueId != stale.uniqueId)
            return *d;

    // Keine geänderte Beschreibung gefunden: Rescan hilft nicht weiter
    if (found.isEmpty())
        knownPlugins.addType (stale);

    return std::nullopt;
}

void PluginHost::loadPluginImpl (const juce::PluginDescription& desc, LoadCallback callback, bool isRetry)
{
    unloadPlugin();

    const auto generation = ++loadGeneration;
    double sampleRate = 44100.0;
    int blockSize = 512;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        sampleRate = device->getCurrentSampleRate();
        blockSize  = device->getCurrentBufferSizeSamples();
    }

    juce::WeakReference<PluginHost> weakThis (this);

    formatManager.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [weakThis, generation, desc, isRetry, onDone = std::move (callback)]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) mutable
        {
            auto* self = weakThis.get();

            // Host weg oder inzwischen ein anderes Plugin angefordert
            if (self == nullptr || generation != self->loadGeneration)
                return;

            if (instance == nullptr)
            {
                // Eintrag stammt evtl. aus einer veralteten moduleinfo.json:
                // Datei einmal neu scannen und mit der neuen Beschreibung erneut laden
                if (! isRetry)
                    if (const auto rescanned = self->rescanFile (desc))
                    {
                        self->loadPluginImpl (*rescanned, std::move (onDone), true);
                        return;
                    }

                if (onDone != nullptr)
                    onDone (error.isNotEmpty() ? error : juce::String ("Plugin konnte nicht geladen werden."));

                return;
            }

            prepareInstrumentBuses (*instance);

            self->plugin = std::move (instance);
            self->livePlaybackEnabled = true;
            self->livePlay.setPlugin (self->plugin.get());
            self->sendChangeMessage();

            if (onDone != nullptr)
                onDone ({});
        });
}

void PluginHost::unloadPlugin()
{
    if (plugin == nullptr)
        return;

    if (onBeforePluginUnload != nullptr)
        onBeforePluginUnload();

    livePlay.setPlugin (nullptr);
    plugin = nullptr;
    sendChangeMessage();
}

void PluginHost::allNotesOff()
{
    keyboardState.allNotesOff (0);
    livePlay.panic();

    for (int channel = 1; channel <= 16; ++channel)
        player.getMidiMessageCollector().addMessageToQueue (
            juce::MidiMessage::allNotesOff (channel).withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001));
}

void PluginHost::setLivePlaybackEnabled (bool shouldBeEnabled)
{
    livePlaybackEnabled = shouldBeEnabled;

    if (! shouldBeEnabled)
        livePlay.panic();

    livePlay.setPlugin (shouldBeEnabled ? plugin.get() : nullptr);
}

bool PluginHost::prefersRealtimeRendering (const juce::PluginDescription& desc) const
{
    juce::StringArray ids;
    ids.addLines (settings.getValue (realtimePluginsKey));
    return ids.contains (desc.createIdentifierString());
}

void PluginHost::setPrefersRealtimeRendering (const juce::PluginDescription& desc, bool shouldPrefer)
{
    juce::StringArray ids;
    ids.addLines (settings.getValue (realtimePluginsKey));
    ids.removeEmptyStrings();
    ids.removeString (desc.createIdentifierString());

    if (shouldPrefer)
        ids.add (desc.createIdentifierString());

    settings.setValue (realtimePluginsKey, ids.joinIntoString ("\n"));
    settings.saveIfNeeded();
}

void PluginHost::saveSettings()
{
    if (auto xml = knownPlugins.createXml())
        settings.setValue (pluginListKey, xml.get());

    if (auto xml = deviceManager.createStateXml())
        settings.setValue (audioDeviceKey, xml.get());

    settings.saveIfNeeded();
}

void PluginHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Plugin-Liste oder Geräteeinstellungen haben sich geändert
    saveSettings();
}

void PluginHost::setMidiLearnCallback (std::function<void (int)> callback)
{
    midiLearnCallback = std::move (callback);
    learnTarget = this;
    midiLearnActive = midiLearnCallback != nullptr;
}

void PluginHost::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    player.getMidiMessageCollector().addMessageToQueue (message);

    if (message.isController() && midiLearnActive.exchange (false))
    {
        juce::MessageManager::callAsync ([weakThis = learnTarget, cc = message.getControllerNumber()]
        {
            if (weakThis == nullptr || weakThis->midiLearnCallback == nullptr)
                return;

            // Der Callback darf sich selbst ersetzen oder entfernen
            auto callback = std::exchange (weakThis->midiLearnCallback, nullptr);
            callback (cc);
        });
    }

    if (message.isNoteOnOrOff())
    {
        const juce::ScopedValueSetter<bool> guard (mirroringHardwareMidi, true);
        keyboardState.processNextMidiEvent (message);
    }
}

void PluginHost::handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    if (mirroringHardwareMidi)
        return;

    player.getMidiMessageCollector().addMessageToQueue (
        juce::MidiMessage::noteOn (channel, note, velocity).withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001));
}

void PluginHost::handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    if (mirroringHardwareMidi)
        return;

    player.getMidiMessageCollector().addMessageToQueue (
        juce::MidiMessage::noteOff (channel, note, velocity).withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001));
}

} // namespace autosampler
