#pragma once

#include "Text.h"
#include "Widgets.h"
#include "engine/Exporter.h"
#include "engine/LivePlaySettings.h"
#include "engine/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace autosampler
{

/** Export: Ziel (WAV, SFZ, Tonverk), Name, Format, Aufbereitung und Auswahl der
    Samples. Die Einstellungen gehören zur Session. Der Export läuft auf einem
    eigenen Thread mit Fortschrittsfenster. */
class ExportView final : public juce::Component,
                         private juce::ListBoxModel,
                         private juce::ValueTree::Listener,
                         private juce::ChangeListener,
                         private juce::AsyncUpdater
{
public:
    explicit ExportView (Session&);
    ~ExportView() override;

    /** Liefert die aktuellen Einstellungen des Play-Tabs (Hüllkurve, Velocity, One Shot). */
    std::function<SamplerSettings()> getPlaySettings;

    /** Vorgabe für den Zielordner, solange hier noch keiner gewählt wurde (Zielordner des Autosamplings). */
    std::function<juce::File()> getDefaultFolder;

    /** Meldungen für die Statuszeile der App. */
    std::function<void (const juce::String&, bool isError)> onStatus;

    /** Fragt nach dem Zielordner und exportiert. */
    void startExport();

    bool isExporting() const noexcept           { return job != nullptr; }
    void setExportAllowed (bool);

    /** Leiste unten mit Zusammenfassung und Exportieren; MainComponent zeigt sie über die ganze Breite. */
    juce::Component& getBottomBar()             { return exportBar; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class ExportJob;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleAsyncUpdate() override;

    void updateControls();
    void storeSettings();
    void updateSummary();
    void setAllExported (bool);
    void runExportTo (const juce::File& folder);
    void exportFinished (const ExportResult&);
    juce::String getInstrumentName() const;

    Session& session;
    juce::ValueTree sessionState;
    ExportSettings settings;

    /** Leiste: Punkt, Zusammenfassung und Exportieren. */
    class ExportBar final : public juce::Component
    {
    public:
        ExportBar();

        void paint (juce::Graphics&) override;
        void resized() override;

        juce::Label titleLabel, summaryLabel;
        juce::TextButton exportButton { "Export..." };
        bool ready = false;
    };

    void layoutContent();

    juce::Viewport viewport;
    juce::Component content;
    FormSection targetSection { "Target" }, audioSection { "Audio" }, playbackSection { "Playback" }, samplesSection { "Samples" };
    ExportBar exportBar;

    juce::Label targetLabel { {}, "Format" }, nameLabel { {}, "Name" }, outputLabel { {}, "Output" };
    juce::ComboBox targetBox, outputBox;
    juce::TextEditor nameEditor;
    juce::Label targetHint;

    juce::Label rateLabel { {}, "Sample rate" }, bitsLabel { {}, "Bit depth" }, tailLabel { {}, "Tail" };
    juce::ComboBox rateBox, bitsBox, tailBox;
    juce::ToggleButton normalizeButton { "Normalize to" }, monoButton { "Mono" }, loopOnlyButton { "Loop only" };
    juce::Slider peakSlider;
    juce::Label smoothingLabel { {}, "Zone Smoothing" };
    juce::Slider smoothingSlider;

    juce::ToggleButton loopOnReleaseButton { "Loop on release" }, oneShotButton { "No loop: one shot" };
    juce::Label attackLabel { {}, "Attack" }, releaseLabel { {}, "Release" }, velocityLabel { {}, "Velocity" };
    juce::Slider attackSlider, releaseSlider, velocitySlider;
    juce::TextButton fromPlayButton { "Take from Play"_u };

    juce::ListBox sampleList { {}, this };
    juce::TextButton allButton { "All" }, noneButton { "None" };

    juce::Label& summaryLabel = exportBar.summaryLabel;
    juce::TextButton& exportButton = exportBar.exportButton;

    std::unique_ptr<ExportJob> job;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File lastFolder;
    bool exportAllowed = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportView)
};

} // namespace autosampler
