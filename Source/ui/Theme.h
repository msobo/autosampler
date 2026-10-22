#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace autosampler
{

/** Darstellung der App: wie das System, immer hell oder immer dunkel. */
enum class AppearanceMode { system, light, dark };

juce::String toString (AppearanceMode);
AppearanceMode appearanceModeFromString (const juce::String&);

namespace theme
{
    /** Farbrollen der App zusätzlich zu den JUCE-Farben. Werte stehen im
        LookAndFeel und wechseln mit Hell/Dunkel; im Code nur über findColour
        (beim Zeichnen) oder bindColour (für gesetzte Farben) verwenden. */
    enum ColourIds
    {
        accent = 0x41530001,    // Loop-Bereich, aktive Knöpfe, Auswahl
        onAccent,               // Text auf accent (Marker-Fahnen)
        crossfade,              // Crossfade-Bereich und Marker X
        waveform,
        waveformBackground,
        card,                   // Hintergrund der Gruppen, Sample-Karten und Sequencer-Schritte
        cardOutline,
        primaryText,            // Titel auf Karten und Schritten
        secondaryText,          // Hinweise, Status, Fußzeilen
        tertiaryText,           // Platzhalter, leere Ansichten
        error,
        warning,
        locked,                 // Rahmen gesperrter Samples
        playhead,               // Abspielposition und ausgewählter Marker
        listBackground,
        listSelection,
        controlOutline,         // Rahmen von Kästchen, Feldern und Knöpfen
        canvas,                 // Fensterhintergrund
        toolbar,                // Werkzeugleiste, Statusleiste und Keyboard-Feld
        sidebar,                // Seitenleiste
        separator,              // Trennlinien in Gruppen und Leisten
        control,                // Fläche von Knöpfen und Auswahlfeldern
        field,                  // Fläche von Textfeldern
        chip,                   // Marken und kleine Schaltflächen
        segmented,              // Hintergrund der Segment-Schalter
        segmentedActive,        // gewähltes Segment
        switchOff,              // ausgeschalteter Schalter
        track,                  // Laufbahn von Schiebereglern und Fortschrittsbalken
        pending,                // offene Schritte, Ruhezustand
        accentText,             // Akzentfarbe für Text auf Flächen
        running,                // laufender Schritt (Orange)
        success,                // beendet (Grün)
        keyWhite,
        keyBlack,
        keyBorder,
        keyLabel,
        room,                   // Hintergrund hinter dem Gerät (mit Raster)
        roomGrid,
        ink,                    // Umrisse und harte Schatten
        caseHighlight,          // Lichtkante oben am Gehäuse und an Tasten
        deck,                   // Werkzeugleiste und Leiste des Bereichs
        bezel,                  // Rahmen um den Monitor
        plate,                  // Schild mit Überschrift
        plateText,
        screw,
        screen,                 // Bildschirmfläche (CRT)
        screenHighlight,
        phosphor,               // Schrift und Anzeigen auf dem Bildschirm
        pink,                   // Laufender Schritt, Markierungen
        yellow,                 // Panic, Learn
        red,                    // Abbrechen
        led                     // Kontrollleuchte an
    };

    constexpr ColourIds allColourIds[] { accent, onAccent, crossfade, waveform, waveformBackground, card, cardOutline,
                                         primaryText, secondaryText, tertiaryText, error, warning, locked, playhead,
                                         listBackground, listSelection, controlOutline, canvas, toolbar, sidebar,
                                         separator, control, field, chip, segmented, segmentedActive, switchOff, track,
                                         pending, accentText, running, success, keyWhite, keyBlack, keyBorder, keyLabel,
                                         room, roomGrid, ink, caseHighlight, deck, bezel, plate, plateText, screw, screen,
                                         screenHighlight, phosphor, pink, yellow, red, led };

    /** Farben, die in Hell und Dunkel gleich sind (Akzent, Statusfarben und Bildschirm der Vorlage). */
    constexpr ColourIds sharedColourIds[] { accent, onAccent, crossfade, running, success, waveform, playhead, chip, segmentedActive,
                                            phosphor, pink, yellow, red, led };

    juce::LookAndFeel_V4::ColourScheme getColourScheme (bool dark);
    juce::Colour getColour (ColourIds, bool dark);

    /** Kontrastverhältnis nach WCAG 2 (1 bis 21), Alpha wird ignoriert. */
    double getContrastRatio (juce::Colour, juce::Colour);

    /** Setzt die Farbe targetColourId der Komponente auf die Farbe sourceColourId
        des LookAndFeel und merkt sich die Zuordnung, damit refreshColourBindings()
        sie nach einem Wechsel Hell/Dunkel erneuert. */
    void bindColour (juce::Component&, int targetColourId, int sourceColourId);

    /** Erneuert alle mit bindColour gesetzten Farben der Komponente und ihrer Kinder. */
    void refreshColourBindings (juce::Component&);

    //==============================================================================
    /** Schriftgröße für eine CSS-Größe der Vorlage (JUCE misst Ober- plus Unterlänge).
        Gilt für die Schrift der Oberfläche (Menlo). */
    inline float fontSize (float cssPixels) noexcept    { return cssPixels * 1.17f; }

    /** Schriften der Vorlage: Oberfläche (Menlo), Bildschirm (VT323), Schilder (Rubik Bold)
        und Logo (Rubik Black). Die Bildschirm- und Schildschriften kommen aus den
        Ressourcen (registerTypefaces); ohne sie gelten Ersatzschriften des Systems. */
    enum class Face { ui, screen, plate, logo };

    void registerTypefaces (juce::Typeface::Ptr screen, juce::Typeface::Ptr plateBold, juce::Typeface::Ptr logoBlack);

    /** Schrift für eine CSS-Größe der Vorlage; bold nur für Face::ui. */
    juce::Font getFont (Face, float cssPixels, bool bold = false);

    /** Text einer Knopfbeschriftung: Großbuchstaben mit etwas Zeichenabstand. */
    juce::Font getKeyFont (float cssPixels = 12.0f);

    //==============================================================================
    /** Tiefe des harten Schattens unter Tasten (Pixel). Tasten sind um diese Höhe
        höher als ihr Körper. */
    constexpr int keyDepth = 3;

    /** Zeichnet eine Taste der Vorlage (Körper, Lichtkante, Rand und harter Schatten).
        bounds enthält den Schatten; gedrückt ist die Taste zwei Pixel nach unten gerückt. */
    void drawKey (juce::Graphics&, juce::Rectangle<float> bounds, float radius, juce::Colour fill,
                  juce::Colour ink, bool pressed, float alpha = 1.0f);

    /** Bildschirm (CRT): dunkle Fläche mit Zeilen, Schein und Innenschatten, Rand in Ink. */
    void drawScreen (juce::Graphics&, juce::Rectangle<float> bounds, float radius, juce::Colour screen,
                     juce::Colour screenHighlight, juce::Colour ink, float alpha = 1.0f);

    /** Text auf dem Bildschirm mit Leuchten. */
    void drawGlowText (juce::Graphics&, const juce::String&, juce::Rectangle<int>, juce::Justification,
                       juce::Colour, const juce::Font&);

    /** Fläche eines Moduls: Rand in Ink, harter Schatten, Schilder-Überschrift und Schrauben. */
    void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& title,
                    const juce::LookAndFeel&, float alpha = 1.0f);

    /** Breite des Schildes mit dieser Überschrift. */
    int getPlateWidth (const juce::String&);

    /** Schild mit Überschrift (dunkel, helle Schrift); gibt die Breite zurück. */
    int drawPlate (juce::Graphics&, juce::Point<int> topLeft, const juce::String&, const juce::LookAndFeel&);

    /** Tiefe des harten Schattens unter Modulen. */
    constexpr int panelDepth = 4;

    /** Leiste unten im Gehäuse (Lauf, Export, Editor): Fläche des Decks mit Ink-Linie oben. */
    void drawDeckBar (juce::Graphics&, juce::Rectangle<int> bounds, const juce::LookAndFeel&);

    /** Anzeige in einer Leiste: Bildschirm links von den Tasten. */
    juce::Rectangle<int> getBarDisplayBounds (juce::Rectangle<int> barBounds, int rightEdge);

    /** Quadratische Leuchte auf dem Bildschirm. */
    void drawScreenLed (juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool lit);

    /** Zeichen in Knöpfen: werden vom LookAndFeel vor dem Text gezeichnet. */
    enum class Icon
    {
        none, play, lock, loop, search, chevronUpDown, undo, redo, plug, sliders, folder, chevronRight, learn
    };

    void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour);
    void setIcon (juce::Button&, Icon);
    Icon getIcon (const juce::Component&);

    /** Hervorgehobener Knopf in der Akzentfarbe (Sampling starten, Alle Loops suchen). */
    void setPrimary (juce::Button&, bool);
    bool isPrimary (const juce::Component&);

    /** Eigene Farbe der Taste (Panic gelb, Abbrechen rot); die Schrift ist dann dunkel. */
    void setTint (juce::Button&, juce::Colour);
    juce::Colour getTint (const juce::Component&);

    /** Auswahlfeld als Kassettenschacht (Instrument in der Werkzeugleiste). */
    void setCartridge (juce::ComboBox&, bool);

    /** Schaltet die Eckenrundung eines Knopfes (Vorgabe 7). */
    void setCornerSize (juce::Component&, float);
    float getCornerSize (const juce::Component&);

    /** Eingeschaltet als helle Fläche statt in der Akzentfarbe (Sperren). */
    void setChipToggle (juce::Button&, bool);

    /** Zweite, kleinere Zeile unter der Beschriftung eines Schalters. */
    void setHint (juce::Button&, const juce::String&);
    void setKeyText (juce::Button&, const juce::String&);

    /** Textfelder zeichnen ihre Ecken selbst. */
    void prepareTextEditor (juce::TextEditor&);
}

/** LookAndFeel der App: JUCE V4 mit einem hellen und einem dunklen Schema,
    den Farbrollen aus theme::ColourIds und der Gestaltung der Vorlage
    (abgerundete Knöpfe, Schalter, Auswahlfelder und Schieberegler). */
class AppLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AppLookAndFeel();

    void setDark (bool);
    bool isDark() const noexcept        { return dark; }

    // Knöpfe
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void changeToggleButtonWidthToFitText (juce::ToggleButton&) override;

    // Auswahlfelder, Beschriftungen, Textfelder
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawComboBoxTextWhenNothingSelected (juce::Graphics&, juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    // Schieberegler
    int getSliderThumbRadius (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Button* createSliderButton (juce::Slider&, bool isIncrement) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height, double progress, const juce::String&) override;
    void drawGroupComponentOutline (juce::Graphics&, int width, int height, const juce::String& text,
                                    const juce::Justification&, juce::GroupComponent&) override;

private:
    bool dark = true;
};

} // namespace autosampler
