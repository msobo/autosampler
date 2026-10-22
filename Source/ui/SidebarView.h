#pragma once

#include "Text.h"
#include "Theme.h"

namespace autosampler
{

/** Seitenleiste: Daten des geladenen Plugins und darunter je nach Bereich der
    Überblick über den Lauf (Autosample) oder die Liste der Samples (Editor). */
class SidebarView final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::ListBoxModel
{
public:
    enum class Content { none, runOverview, sampleList };
    enum class Cell { open, running, done };

    struct PluginInfo
    {
        juce::String name, subtitle;
        std::vector<std::pair<juce::String, juce::String>> rows;
    };

    /** Überblick über einen Lauf: Zeilen je Velocity, Spalten je Note. */
    struct RunOverview
    {
        int numSamples = 0;
        juce::String description;
        std::vector<juce::String> rowLabels;
        std::vector<int> columnNotes;               // MIDI-Note jeder Spalte
        std::vector<std::vector<Cell>> cells;
        juce::String error;
    };

    struct SampleItem
    {
        juce::String note, velocity;
        bool loop = false, locked = false;
    };

    SidebarView();
    ~SidebarView() override;

    void setPluginInfo (PluginInfo);
    void setContent (Content);
    void setRunOverview (RunOverview);

    void setSamples (std::vector<SampleItem>);
    void setSelectedSample (int index);

    /** Wird aufgerufen, wenn ein Sample in der Liste gewählt wird. */
    std::function<void (int index)> onSampleSelected;

    /** Wird aufgerufen, wenn der Schalter unter dem Bildschirm (Hell/Dunkel) angeklickt wird. */
    std::function<void()> onPowerClicked;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> getFooterBounds() const;
    juce::Rectangle<int> getPowerBounds() const;

    // ListBoxModel
    int getNumRows() override                       { return (int) samples.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    struct Layout
    {
        juce::Rectangle<int> bezel, screen, listArea, description;
    };

    struct RunGrid
    {
        int numRows = 0, numColumns = 0, binSize = 1, cell = 12, gap = 3, height = 0;
    };

    static constexpr int panelShadow = 5;

    int getInfoHeight() const;
    RunGrid getRunGrid (int innerWidth) const;
    Layout getLayout() const;
    void paintScreen (juce::Graphics&, const Layout&, juce::Colour phosphor);
    void paintRunGrid (juce::Graphics&, juce::Rectangle<int> area, juce::Colour phosphor);
    void drawCell (juce::Graphics&, juce::Rectangle<float>, Cell, juce::Colour phosphor) const;

    PluginInfo pluginInfo;
    Content content = Content::none;
    RunOverview overview;
    std::vector<SampleItem> samples;

    juce::ListBox sampleList { {}, this };
    bool updatingSelection = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidebarView)
};

} // namespace autosampler
