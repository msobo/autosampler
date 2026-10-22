#include "MainComponent.h"
#include "host/PluginScanner.h"
#include "ui/Appearance.h"
#include "ui/Fonts.h"

namespace autosampler
{

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (PluginHost& host, Appearance& appearance)
        : DocumentWindow (JUCE_APPLICATION_NAME_STRING,
                          juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::allButtons)
    {
        theme::bindColour (*this, juce::ResizableWindow::backgroundColourId, juce::ResizableWindow::backgroundColourId);
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (host, appearance), true);
        setResizable (true, true);
        setResizeLimits (1100, 700, 4000, 3000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

//==============================================================================
class AutosamplerApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    // Muss true sein, damit der Scan-Subprozess starten kann
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // Wurde die App als Scan-Subprozess gestartet? Dann nur scannen.
        auto worker = std::make_unique<ScannerWorker>();

        if (worker->initialiseFromCommandLine (commandLine))
        {
            juce::Process::setDockIconVisible (false);
            scannerWorker = std::move (worker);
            return;
        }

        juce::PropertiesFile::Options options;
        options.applicationName     = "Autosampler";
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "Autosampler";
        settings = std::make_unique<juce::PropertiesFile> (options);

        loadBundledFonts();
        appearance = std::make_unique<Appearance> (*settings);
        host = std::make_unique<PluginHost> (*settings);
        mainWindow = std::make_unique<MainWindow> (*host, *appearance);

        // Session als Argument, z. B. Autosampler ~/Music/Autosampler/X.asession [--tab play]
        const auto args = juce::StringArray::fromTokens (commandLine, true);

        if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
        {
            for (int i = 0; i < args.size(); ++i)
            {
                const auto arg = args[i].unquoted();

                if (arg == "--tab" && i + 1 < args.size())
                    main->showTab (args[++i].unquoted());
                else if (juce::File::isAbsolutePath (arg))
                    main->openSessionFile (juce::File (arg));
            }
        }
    }

    void shutdown() override
    {
        // Reihenfolge: erst UI (Editor-Fenster), dann Host, LookAndFeel, Einstellungen
        mainWindow = nullptr;
        host = nullptr;
        appearance = nullptr;
        settings = nullptr;
        scannerWorker = nullptr;
    }

    void systemRequestedQuit() override
    {
        // Bei ungespeicherten Änderungen der Session vorher nachfragen
        if (mainWindow != nullptr)
            if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            {
                main->confirmDiscardChanges ([] { quit(); });
                return;
            }

        quit();
    }

private:
    std::unique_ptr<ScannerWorker> scannerWorker;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<Appearance> appearance;
    std::unique_ptr<PluginHost> host;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace autosampler

START_JUCE_APPLICATION (autosampler::AutosamplerApplication)
