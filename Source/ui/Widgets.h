#pragma once

#include "Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>
#include <vector>

namespace autosampler
{

/** Modul der Vorlage: abgerundete Fläche mit Rand und hartem Schatten, einem Schild als
    Überschrift und gestrichelten Linien zwischen den Zeilen. Zeichnet nur; die Bedienelemente der Zeilen
    liegen als Geschwister darüber und werden an getRowBounds() ausgerichtet. */
class FormSection final : public juce::Component
{
public:
    static constexpr int titleHeight = 34;
    static constexpr int rowHeight = 44;
    static constexpr int rowPadding = 14;

    explicit FormSection (juce::String title = {});

    void setTitle (const juce::String&);

    /** Höhe jeder Zeile; die Karte ist so hoch wie die Summe. */
    void setRows (std::vector<int> heights);
    void setRowCount (int numRows)           { setRows (std::vector<int> ((size_t) numRows, rowHeight)); }

    int getPreferredHeight() const;

    /** Fläche der Zeile in den Koordinaten des Elternelements (mit Innenrand links und rechts). */
    juce::Rectangle<int> getRowBounds (int row) const;

    void paint (juce::Graphics&) override;

private:
    juce::String title;
    std::vector<int> rows { rowHeight };
};

/** Segment-Schalter der Vorlage (Bereiche, Plugin/Samples, Echtzeit/Offline). */
class SegmentedControl final : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    explicit SegmentedControl (juce::StringArray items = {}, int itemHeight = 22, int itemPadding = 12);

    void setItems (juce::StringArray);

    /** Farbrolle (theme::ColourIds) je Schalter für den gewählten Zustand; 0 = accent. */
    void setActiveColourIds (std::vector<int> ids)  { activeColourIds = std::move (ids); repaint(); }

    /** Zeigt zwischen den Schaltern kleine >-Marker (Ablauf von links nach rechts). */
    void setFlowMarkers (bool shouldShow);
    int getSelectedIndex() const noexcept       { return selected; }
    void setSelectedIndex (int, juce::NotificationType = juce::sendNotification);
    int getPreferredWidth() const;
    int getPreferredHeight() const              { return height + theme::keyDepth; }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    int getGap() const noexcept                 { return showFlowMarkers ? 20 : 6; }
    int getItemWidth (int index) const;
    juce::Rectangle<float> getItemBounds (int index) const;

    juce::StringArray items;
    int height, padding, selected = 0;
    bool showFlowMarkers = false;
    std::vector<int> activeColourIds;
};

/** Zahlenfeld: Wert auf dem Bildschirm, daneben die Tasten − und +. */
class StepperSlider final : public juce::Slider
{
public:
    StepperSlider();

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void lookAndFeelChanged() override;

private:
    static constexpr int buttonsWidth = 66;
};

/** Kleine Marke mit Punkt, z. B. „Ungespeichert“. */
class StatusChip final : public juce::Component
{
public:
    void setText (const juce::String&, juce::Colour dot);
    int getPreferredWidth() const;

    void paint (juce::Graphics&) override;

private:
    juce::String text;
    juce::Colour dotColour;
};

/** Pfadanzeige im Stil eines Textfeldes: Ordnersymbol, Pfad gedämpft, letzter Teil hervorgehoben. */
class PathField final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    void setPath (const juce::String& fullPath);

    void paint (juce::Graphics&) override;

private:
    juce::String path;
};

/** Klaviatur mit Punkten für die gesampelten Noten (gefüllt: fertig, orange: läuft,
    Ring: offen). */
class PianoKeyboard final : public juce::MidiKeyboardComponent
{
public:
    enum class Mark { none, done, running, open, selected };

    PianoKeyboard (juce::MidiKeyboardState&, Orientation);

    void setMarks (std::map<int, Mark>);

protected:
    void drawWhiteNote (int midiNoteNumber, juce::Graphics&, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int midiNoteNumber, juce::Graphics&, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour noteFillColour) override;

private:
    void drawMark (juce::Graphics&, juce::Point<float> centre, float diameter, Mark) const;
    Mark getMark (int midiNoteNumber) const;

    std::map<int, Mark> marks;
};

} // namespace autosampler
