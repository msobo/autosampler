#pragma once

#include "SidebarView.h"
#include "Text.h"
#include "Widgets.h"
#include "engine/SamplingJob.h"
#include "engine/Session.h"
#include "host/PluginHost.h"

namespace autosampler
{

/** Einstellungen und Start eines Sampling-Laufs: Notenbereich, Velocities,
    Round Robin, Pegel, Zeiten, Render-Modus, Format und Ziel; Fortschritt und
    Protokoll. Jeder Lauf beginnt eine neue Session im Paketordner <Name>.asession. */
class AutosampleView final : public juce::Component,
                             private juce::ChangeListener,
                             private juce::ScrollBar::Listener,
                             private juce::AudioProcessorListener
{
public:
    /** Zielordner für neue Läufe (gewählt oder Standard). */
    juce::File getBaseFolder() const;

    AutosampleView (PluginHost&, Session&);
    ~AutosampleView() override;

    bool isRunning() const noexcept     { return job != nullptr; }

    /** Laufleiste (Status, Fortschritt, Start); MainComponent zeigt sie über die ganze Breite unten. */
    juce::Component& getBottomBar()     { return runBar; }

    /** Überblick über den geplanten und laufenden Durchlauf für die Seitenleiste und die Klaviatur. */
    SidebarView::RunOverview getRunOverview() const;

    /** Wird aufgerufen, wenn sich getRunOverview() ändert. */
    std::function<void()> onOverviewChanged;

    /** Übernimmt Einstellungen, z. B. aus einer geladenen Session. */
    void applySettings (const SamplingSettings&);

    /** Beendet MIDI/Parameter Learn (z. B. bevor das Plugin entladen wird). */
    void stopLearning();

    /** Wird aufgerufen, wenn ein Lauf startet oder endet. */
    std::function<void (bool running)> onRunningChanged;

    /** Wird nach einem Lauf mit Samples aufgerufen, wenn „Alle Loops automatisch suchen“ an ist. */
    std::function<void()> onFindLoopsRequested;

    /** Wird vor einem Lauf aufgerufen, der die aktuelle Session ersetzt.
        proceed startet den Lauf (z. B. nach Rückfrage wegen ungespeicherter
        Änderungen). nullptr: sofort starten. */
    std::function<void (std::function<void()> proceed)> onBeforeReplacingSession;

    void resized() override;
    void lookAndFeelChanged() override;

private:
    /** Pfeil nach unten am unteren Rand: zeigt, dass weiter unten noch Einstellungen liegen; Klick scrollt eine Seite. */
    class ScrollHint final : public juce::Component,
                             public juce::SettableTooltipClient
    {
    public:
        ScrollHint();
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        std::function<void()> onClick;
    };

    void scrollBarMoved (juce::ScrollBar*, double) override { updateScrollHint(); }
    void updateScrollHint();

    ScrollHint scrollHint;

    /** Statusleiste unten: Punkt, Titel, Beschreibung bzw. Fortschritt, Start/Abbrechen. */
    class RunBar final : public juce::Component
    {
    public:
        RunBar();

        void setRunning (bool);
        void paint (juce::Graphics&) override;
        void resized() override;

        juce::Label titleLabel, detailLabel, remainingLabel;
        double progress = 0.0;
        juce::ProgressBar progressBar { progress };
        juce::TextButton startButton { "Start sampling" };

    private:
        bool running = false;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // Parameter Learn; kommt von beliebigen Threads
    void audioProcessorParameterChanged (juce::AudioProcessor*, int parameterIndex, float) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}

    void startLearning();
    void learnParameter (int parameterIndex);
    void learnController (int controller);
    void settingsChanged();

    void loadSettings();
    void saveSettings();
    SamplingSettings collectSettings() const;
    void updateSummary();
    void updateControls();
    void updateOverview (const juce::Array<SampleRequest>&, const SamplingSettings&);
    void chooseFolder();
    void layoutContent();

    void start();
    void startNow();
    void cancel();
    void finish (const SamplingResult&);
    void log (const juce::String& message, LogLevel = LogLevel::info);
    void appendToLog (const juce::String& message, LogLevel);

    PluginHost& host;
    Session& session;

    juce::Viewport viewport;
    juce::Component content;

    FormSection notesSection { "Notes" }, dynamicsSection { "Dynamics & Variation" }, levelSection { "Level & Timing" },
                renderSection { "Render" }, outputSection { "Output" }, logSection { "Log" };

    juce::Label rangeLabel { {}, "Range" }, toLabel { {}, "to" }, stepLabel { {}, "Grid" }, noteCountLabel;
    juce::ComboBox lowNoteBox, highNoteBox, stepBox;
    juce::ToggleButton customNotesToggle { "Use custom notes" };
    juce::TextEditor customNotesEditor;

    juce::Label velocitiesLabel { {}, "Velocities" };
    juce::TextEditor velocitiesEditor;

    juce::Label roundRobinLabel { {}, "Round Robin" }, modeLabel { {}, "Mode" }, stepsLabel { {}, "Steps" };
    juce::ComboBox roundRobinSourceBox, roundRobinModeBox;
    StepperSlider stepsSlider;

    juce::Label variationLabel { {}, "Variation" }, valueRangeLabel { {}, "Value range" }, toValueLabel { {}, "to" };
    juce::ComboBox variationTargetBox;
    StepperSlider controllerSlider, fromSlider, toSlider;
    juce::Label parameterNameLabel;
    juce::TextButton learnButton { "Learn" };
    juce::String parameterId;

    juce::Label gainLabel { {}, "Level" }, panLabel { {}, "Pan" };
    StepperSlider gainSlider, panSlider;

    juce::Label holdLabel { {}, "Hold time" }, tailLabel { {}, "Max. Tail" }, thresholdLabel { {}, "Tail threshold" };
    StepperSlider holdSlider, tailSlider, thresholdSlider;

    juce::Label renderLabel { {}, "Mode" }, formatLabel { {}, "Format" };
    SegmentedControl renderModeControl { { "Automatic", "Offline", "Realtime" }, 22, 10 };
    juce::ComboBox sampleRateBox, bitDepthBox;
    juce::ToggleButton restoreStateToggle { "Restore plugin state" };
    juce::ToggleButton trimStartToggle { "Remove silence at start" };
    juce::ToggleButton autoFindLoopsToggle { "Find loops automatically" };

    juce::Label nameLabel { {}, "Name" };
    juce::TextEditor nameEditor;
    juce::Label folderLabel { {}, "Destination" };
    PathField folderField;
    juce::TextButton folderButton { "Choose..."_u };
    juce::TextButton revealButton { "Show in Finder" };

    juce::String summaryBreakdown, summaryLimit, summaryError;
    RunBar runBar;
    juce::TextEditor logEditor;
    std::vector<std::pair<juce::String, LogLevel>> logLines;   // zum Neueinfärben nach Hell/Dunkel

    // Fortschritt des laufenden Durchlaufs für die Seitenleiste
    juce::Array<SampleRequest> plannedRequests;
    int numStarted = 0;
    juce::uint32 runStartTime = 0;

    juce::File outputFolder;
    juce::File lastRunFolder;
    juce::PluginDescription runningPlugin;
    std::unique_ptr<SamplingJob> job;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool loadingSettings = false;
    juce::AudioProcessor* learningPlugin = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutosampleView)
};

} // namespace autosampler
