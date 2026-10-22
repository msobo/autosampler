#include "PluginScanner.h"
#include "Vst3ModuleInfoFix.h"

namespace autosampler
{

//==============================================================================
ScannerWorker::ScannerWorker()
{
    juce::addDefaultFormatsToManager (formatManager);
}

bool ScannerWorker::initialiseFromCommandLine (const juce::String& commandLine)
{
    return ChildProcessWorker::initialiseFromCommandLine (commandLine, scannerProcessUID);
}

void ScannerWorker::handleMessageFromCoordinator (const juce::MemoryBlock& mb)
{
    if (mb.isEmpty())
        return;

    const std::lock_guard<std::mutex> lock (mutex);

    // Manche Formate (z. B. AU) müssen auf dem Message-Thread erzeugt werden.
    // Liefert der direkte Scan nichts, wird er dort nachgeholt.
    if (const auto results = doScan (mb); ! results.isEmpty())
    {
        sendResults (results);
    }
    else
    {
        pendingBlocks.emplace (mb);
        triggerAsyncUpdate();
    }
}

void ScannerWorker::handleConnectionLost()
{
    juce::JUCEApplicationBase::quit();
}

void ScannerWorker::handleAsyncUpdate()
{
    for (;;)
    {
        const std::lock_guard<std::mutex> lock (mutex);

        if (pendingBlocks.empty())
            return;

        sendResults (doScan (pendingBlocks.front()));
        pendingBlocks.pop();
    }
}

juce::OwnedArray<juce::PluginDescription> ScannerWorker::doScan (const juce::MemoryBlock& block)
{
    juce::MemoryInputStream stream { block, false };
    const auto formatName = stream.readString();
    const auto identifier = stream.readString();

    juce::PluginDescription pd;
    pd.fileOrIdentifier = identifier;
    pd.uniqueId = pd.deprecatedUid = 0;

    juce::AudioPluginFormat* matchingFormat = nullptr;

    for (auto* format : formatManager.getFormats())
        if (format->getName() == formatName)
            matchingFormat = format;

    juce::OwnedArray<juce::PluginDescription> results;

    if (matchingFormat != nullptr
        && (juce::MessageManager::getInstance()->isThisTheMessageThread()
            || matchingFormat->requiresUnblockedMessageThreadDuringCreation (pd)))
    {
        matchingFormat->findAllTypesForFile (results, identifier);

        if (formatName == "VST3")
            fixStaleVst3ModuleInfo (results, identifier);
    }

    return results;
}

void ScannerWorker::sendResults (const juce::OwnedArray<juce::PluginDescription>& results)
{
    juce::XmlElement xml ("LIST");

    for (const auto* desc : results)
        xml.addChildElement (desc->createXml().release());

    const auto str = xml.toString();
    sendMessageToCoordinator ({ str.toRawUTF8(), str.getNumBytesAsUTF8() });
}

//==============================================================================
class OutOfProcessScanner::Coordinator final : private juce::ChildProcessCoordinator
{
public:
    enum class State { timeout, gotResult, connectionLost };

    struct Response
    {
        State state;
        std::unique_ptr<juce::XmlElement> xml;
    };

    Coordinator()
    {
        launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                             scannerProcessUID, 0, 0);
    }

    Response getResponse()
    {
        std::unique_lock<std::mutex> lock { mutex };

        if (! condvar.wait_for (lock, std::chrono::milliseconds { 50 },
                                [&] { return gotResult || connectionLost; }))
            return { State::timeout, nullptr };

        const auto state = connectionLost ? State::connectionLost : State::gotResult;
        connectionLost = false;
        gotResult = false;

        return { state, std::move (description) };
    }

    using ChildProcessCoordinator::sendMessageToWorker;

private:
    void handleMessageFromWorker (const juce::MemoryBlock& mb) override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        description = juce::parseXML (mb.toString());
        gotResult = true;
        condvar.notify_one();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        connectionLost = true;
        condvar.notify_one();
    }

    std::mutex mutex;
    std::condition_variable condvar;
    std::unique_ptr<juce::XmlElement> description;
    bool connectionLost = false;
    bool gotResult = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Coordinator)
};

//==============================================================================
OutOfProcessScanner::OutOfProcessScanner() = default;
OutOfProcessScanner::~OutOfProcessScanner() = default;

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format,
                                              juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    if (scanInSubprocess (format.getName(), fileOrIdentifier, result))
        return true;

    // Subprozess ist abgestürzt: beim nächsten Plugin neu starten.
    coordinator = nullptr;
    return false;
}

void OutOfProcessScanner::scanFinished()
{
    coordinator = nullptr;
}

bool OutOfProcessScanner::scanInSubprocess (const juce::String& formatName,
                                            const juce::String& fileOrIdentifier,
                                            juce::OwnedArray<juce::PluginDescription>& result)
{
    if (coordinator == nullptr)
        coordinator = std::make_unique<Coordinator>();

    juce::MemoryBlock block;
    juce::MemoryOutputStream stream { block, true };
    stream.writeString (formatName);
    stream.writeString (fileOrIdentifier);

    if (! coordinator->sendMessageToWorker (block))
        return false;

    for (;;)
    {
        if (shouldExit())
            return true;

        const auto response = coordinator->getResponse();

        if (response.state == Coordinator::State::timeout)
            continue;

        if (response.xml != nullptr)
        {
            for (const auto* item : response.xml->getChildIterator())
            {
                auto desc = std::make_unique<juce::PluginDescription>();

                if (desc->loadFromXml (*item))
                    result.add (std::move (desc));
            }
        }

        return response.state == Coordinator::State::gotResult;
    }
}

} // namespace autosampler
