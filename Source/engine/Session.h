#pragma once

#include "ExportSettings.h"
#include "LoopFinder.h"
#include "SamplingSettings.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace autosampler
{

namespace sessionIds
{
    inline const juce::Identifier session     { "Session" };
    inline const juce::Identifier samples     { "Samples" };
    inline const juce::Identifier sample      { "Sample" };
    inline const juce::Identifier name        { "name" };
    inline const juce::Identifier created     { "created" };
    inline const juce::Identifier file        { "file" };
    inline const juce::Identifier note        { "note" };
    inline const juce::Identifier velocity    { "velocity" };
    inline const juce::Identifier sampleRate  { "sampleRate" };
    inline const juce::Identifier numFrames   { "numFrames" };
    inline const juce::Identifier numChannels { "numChannels" };
    inline const juce::Identifier loopEnabled { "loopEnabled" };
    inline const juce::Identifier loopStart   { "loopStart" };
    inline const juce::Identifier loopEnd     { "loopEnd" };
    inline const juce::Identifier locked      { "locked" };
    inline const juce::Identifier crossfade     { "crossfade" };
    inline const juce::Identifier crossfadeMode { "crossfadeMode" };
    inline const juce::Identifier loopAlgorithm { "loopAlgorithm" };
    inline const juce::Identifier loopScore     { "loopScore" };
    inline const juce::Identifier exported      { "exported" };
    inline const juce::Identifier roundRobin    { "roundRobin" };
    inline const juce::Identifier variation     { "variation" };
}

/** Zugriff auf einen Sample-Knoten der Session. Änderungen laufen über den
    UndoManager und werden auf gültige Werte begrenzt. Loop-Punkte in Frames,
    loopEnd exklusiv. */
class SessionSample
{
public:
    static constexpr int minLoopLength = 16;

    SessionSample (juce::ValueTree, juce::UndoManager*);

    const juce::ValueTree& getTree() const noexcept     { return tree; }
    bool isValid() const                                 { return tree.hasType (sessionIds::sample); }

    juce::String getFileName() const;
    int getNote() const;
    int getVelocity() const;
    double getSampleRate() const;
    int getNumFrames() const;
    int getNumChannels() const;
    double getLengthSeconds() const;

    /** Round-Robin-Variante (0 = erste oder ohne Round Robin). */
    int getRoundRobin() const;
    /** Wert der Variation (MIDI CC/Parameter, 0–127), -1 = keine. */
    int getVariation() const;

    bool isLoopEnabled() const;
    int getLoopStart() const;
    int getLoopEnd() const;
    bool isLocked() const;

    /** Crossfade-Länge in Frames vor E, begrenzt auf getMaxCrossfade(). */
    int getCrossfade() const;
    CrossfadeMode getCrossfadeMode() const;
    /** Algorithmus, der den Loop gefunden hat; nullopt, wenn nie gesucht wurde. */
    std::optional<LoopAlgorithm> getLoopAlgorithm() const;
    /** Qualität 0–100, -1 = unbekannt. */
    float getLoopScore() const;

    /** Gesperrte Samples werden nicht verändert (alle Setter außer setLocked). */
    void setLoopEnabled (bool);
    void setLoopStart (int frame);
    void setLoopEnd (int frame);
    void setLoop (int start, int end);
    void setCrossfade (int frames);
    void setCrossfadeMode (CrossfadeMode);
    void setLocked (bool);

    /** Vom Export ausgenommene Samples; Standard true. Auch bei gesperrten Samples änderbar. */
    bool isExported() const;
    void setExported (bool);

    /** Abgeleiteter Wert, geht nicht über den UndoManager. */
    void setLoopScore (float);

    /** Übernimmt ein Suchergebnis (Loop an, Punkte, Crossfade, Algorithmus, Score)
        über den UndoManager. false bei gesperrtem Sample oder ohne Ergebnis. */
    bool applyLoopResult (const LoopResult&, CrossfadeMode);

    /** Standard-Loop, wenn noch keiner gesetzt ist: 50 % bis 90 % der Länge. */
    static juce::Range<int> getDefaultLoop (int numFrames);

private:
    juce::ValueTree tree;
    juce::UndoManager* undoManager;
};

//==============================================================================
/** Eine Session: Einstellungen, Plugin, Plugin-State und Samples mit nicht
    destruktiven Bearbeitungen (Loop-Punkte, Lock).

    Auf der Festplatte ein Paketordner <Name>.asession mit session.json,
    plugin-state.bin und den WAV-Dateien in samples/. Das Objekt lebt so lange wie die App;
    Laden, Neu und ein neuer Lauf ersetzen nur den Inhalt des ValueTree, damit
    Listener angemeldet bleiben. Nur auf dem Message-Thread verwenden.
*/
class Session final : public juce::ChangeBroadcaster,
                      private juce::ValueTree::Listener
{
public:
    static constexpr const char* packageExtension = ".asession";
    static constexpr const char* sessionFileName = "session.json";
    static constexpr const char* pluginStateFileName = "plugin-state.bin";
    static constexpr const char* samplesFolderName = "samples";
    static constexpr int formatVersion = 1;

    Session();
    ~Session() override;

    juce::ValueTree& getState() noexcept                { return state; }
    juce::UndoManager& getUndoManager() noexcept        { return undoManager; }

    juce::String getName() const;
    void setName (const juce::String&);

    /** Paketordner; leer, solange die Session nie gespeichert wurde. */
    juce::File getPackageFolder() const                 { return packageFolder; }
    juce::File getSamplesFolder() const;

    bool hasUnsavedChanges() const noexcept             { return dirty; }
    bool isEmpty() const;

    //==============================================================================
    std::optional<juce::PluginDescription> getPluginDescription() const;
    const juce::MemoryBlock& getPluginState() const noexcept { return pluginState; }
    /** Einstellungen des Laufs; nullopt, wenn die Session keine enthält. */
    std::optional<SamplingSettings> getSamplingSettings() const;

    /** Einstellungen der Loop-Suche; Standardwerte, wenn die Session keine enthält.
        Setzen geht nicht über den UndoManager. */
    LoopSearchSettings getLoopSearchSettings() const;
    void setLoopSearchSettings (const LoopSearchSettings&);

    /** Einstellungen des Exports; Standardwerte, wenn die Session keine enthält.
        Setzen geht nicht über den UndoManager. */
    ExportSettings getExportSettings() const;
    void setExportSettings (const ExportSettings&);

    /** Note und Ende der Haltezeit (aus den Sampling-Einstellungen) eines Samples. */
    LoopHints getLoopHints (const juce::ValueTree& sampleTree) const;

    //==============================================================================
    juce::ValueTree getSamplesTree() const;
    int getNumSamples() const;
    SessionSample getSample (int index);
    SessionSample wrap (const juce::ValueTree& sampleTree)  { return { sampleTree, &undoManager }; }
    juce::File getSampleFile (const juce::ValueTree& sampleTree) const;

    //==============================================================================
    /** Leert die Session (ohne Paketordner). */
    void clear();

    /** Beginnt eine Session für einen Sampling-Lauf: legt den Paketordner mit
        samples/ an und speichert sofort. Liefert eine Fehlermeldung oder "". */
    juce::String beginRecording (const juce::File& newPackageFolder, const juce::String& name,
                                 const juce::PluginDescription&, const juce::MemoryBlock& pluginState,
                                 const SamplingSettings&);

    /** Fügt eine Aufnahme hinzu (nicht rückgängig zu machen). */
    void addRecordedSample (const RecordedSample&);

    /** Speichert session.json und plugin-state.bin in den Paketordner. */
    juce::String save();

    /** Kopiert die Session samt Samples in einen neuen Paketordner und
        arbeitet danach dort weiter. */
    juce::String saveAs (const juce::File& newPackageFolder);

    /** Lädt einen Paketordner oder dessen session.json. Bei einem Fehler
        bleibt der bisherige Inhalt erhalten. */
    juce::String load (const juce::File& packageOrSessionFile);

    //==============================================================================
    /** ValueTree <-> JSON: { "type", "properties": {…}, "children": […] } */
    static juce::var toJson (const juce::ValueTree&);
    static juce::ValueTree fromJson (const juce::var&);

    /** Hängt .asession an, falls nötig. */
    static juce::File withPackageExtension (const juce::File&);

private:
    void replaceContent (const juce::ValueTree& newState, const juce::File& newPackageFolder,
                         juce::MemoryBlock newPluginState);
    juce::String writeFiles (const juce::File& folder) const;
    void setDirty (bool);

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override  { setDirty (true); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { setDirty (true); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { setDirty (true); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override              { setDirty (true); }

    juce::ValueTree state;
    juce::UndoManager undoManager;
    juce::File packageFolder;
    juce::MemoryBlock pluginState;
    bool dirty = false;
    bool replacing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};

} // namespace autosampler
