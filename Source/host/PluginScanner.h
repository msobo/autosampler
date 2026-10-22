#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <condition_variable>
#include <mutex>
#include <queue>

namespace autosampler
{

/** Kennung, über die der Hauptprozess seinen Scan-Subprozess startet. */
inline constexpr const char* scannerProcessUID = "autosamplerpluginscanner";

//==============================================================================
/** Läuft im Subprozess: scannt einzelne Plugin-Dateien und schickt die
    Beschreibungen als XML an den Hauptprozess zurück. Stürzt ein Plugin ab,
    stirbt nur dieser Prozess.
*/
class ScannerWorker final : private juce::ChildProcessWorker,
                            private juce::AsyncUpdater
{
public:
    ScannerWorker();

    /** Gibt true zurück, wenn die App als Scan-Subprozess gestartet wurde. */
    bool initialiseFromCommandLine (const juce::String& commandLine);

private:
    void handleMessageFromCoordinator (const juce::MemoryBlock&) override;
    void handleConnectionLost() override;
    void handleAsyncUpdate() override;

    juce::OwnedArray<juce::PluginDescription> doScan (const juce::MemoryBlock&);
    void sendResults (const juce::OwnedArray<juce::PluginDescription>&);

    std::mutex mutex;
    std::queue<juce::MemoryBlock> pendingBlocks;
    juce::AudioPluginFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScannerWorker)
};

//==============================================================================
/** Ersetzt den In-Process-Scan der KnownPluginList: jede Datei wird im
    Subprozess geprüft. Bricht der Subprozess ab, landet das Plugin auf der
    Blacklist (Dead-Man's-Pedal) und der Scan läuft weiter.
*/
class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    OutOfProcessScanner();
    ~OutOfProcessScanner() override;

    bool findPluginTypesFor (juce::AudioPluginFormat&,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

    void scanFinished() override;

private:
    class Coordinator;

    bool scanInSubprocess (const juce::String& formatName,
                           const juce::String& fileOrIdentifier,
                           juce::OwnedArray<juce::PluginDescription>& result);

    std::unique_ptr<Coordinator> coordinator;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutOfProcessScanner)
};

} // namespace autosampler
