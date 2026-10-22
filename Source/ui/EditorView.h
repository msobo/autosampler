#pragma once

#include "SampleCard.h"
#include "Text.h"
#include "Widgets.h"
#include "engine/LoopBatch.h"

namespace autosampler
{

/** Editor: alle Samples der Session als Karten in einem Raster,
    Undo/Redo, Anzeigengröße der Samples und Loop-Suche (Algorithmus, Blend, Crossfade,
    Pad Amount, Suche für alle). Folgt der Session, auch während eines Laufs. */
class EditorView final : public juce::Component,
                         private juce::ValueTree::Listener,
                         private juce::ChangeListener,
                         private juce::AsyncUpdater
{
public:
    explicit EditorView (Session&);
    ~EditorView() override;

    /** Ziel der Hörprüfung auf den Karten (Play-Knopf, Leertaste). */
    void setAudition (SampleAudition*);

    /** Sucht Loops für alle entsperrten Samples (wie „Alle Loops suchen“). */
    void findAllLoops();

    /** Ausgewähltes Sample (Rahmen auf der Karte, Seitenleiste, Klaviatur); -1: keines. */
    int getSelectedIndex() const noexcept       { return selectedIndex; }
    void setSelectedIndex (int);

    /** Statusleiste unten (Speichern, Weiter zu Export); MainComponent zeigt sie über die ganze Breite. */
    juce::Component& getBottomBar()             { return statusBar; }

    std::function<void (int index)> onSelectionChanged;
    std::function<void()> onSave, onContinueToExport;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** Statusleiste: Punkt, Zustand der Session, Speichern und Weiter zu Export. */
    class StatusBar final : public juce::Component
    {
    public:
        StatusBar();

        void paint (juce::Graphics&) override;
        void resized() override;

        juce::Label titleLabel, detailLabel;
        juce::TextButton saveButton { "Save" }, continueButton { "Continue to Export" };
        bool hasSamples = false;
    };

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override         { triggerAsyncUpdate(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleAsyncUpdate() override;

    void updateCards();
    void layoutPage();
    void updateHeader();
    void updateLoopControls();
    void storeLoopSettings();
    void updateSearchStatus();
    void disableAllLoops();
    void findLoops (const std::vector<juce::ValueTree>&);
    int layoutLoopSection (int x, int y, int width);

    Session& session;
    juce::ValueTree sessionState;

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 512 };

    juce::Label titleLabel, countLabel;
    StatusChip unsavedChip;
    juce::TextButton undoButton, redoButton;
    juce::Label sizeLabel { {}, "Display size"_u };
    juce::Slider sizeSlider;

    LoopBatch loopBatch;
    FormSection loopSection;
    juce::Label algorithmLabel { {}, "Algorithm" }, blendLabel { {}, "Blend" },
                crossfadeLabel { {}, "Crossfade" }, padLabel { {}, "Pad Amount" };
    juce::ComboBox algorithmBox, blendBox;
    juce::Slider crossfadeSlider, padSlider;
    juce::TextButton searchAllButton { "Find all loops" };
    juce::TextButton disableAllButton { "Disable all" };
    juce::Label searchStatus;
    juce::String lastSearchMessage;

    SampleAudition* audition = nullptr;

    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<SampleCard>> cards;
    int selectedIndex = -1;
    StatusBar statusBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorView)
};

} // namespace autosampler
