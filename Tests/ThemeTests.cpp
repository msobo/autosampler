#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>

using namespace autosampler;

namespace
{
    juce::Colour scheme (bool dark, juce::LookAndFeel_V4::ColourScheme::UIColour colour)
    {
        return theme::getColourScheme (dark).getUIColour (colour);
    }
}

TEST_CASE ("Theme: Darstellung als Text", "[theme]")
{
    for (const auto mode : { AppearanceMode::system, AppearanceMode::light, AppearanceMode::dark })
        CHECK (appearanceModeFromString (toString (mode)) == mode);

    CHECK (appearanceModeFromString ({}) == AppearanceMode::system);
    CHECK (appearanceModeFromString ("sepia") == AppearanceMode::system);
}

TEST_CASE ("Theme: Kontrast in Hell und Dunkel", "[theme]")
{
    using UI = juce::LookAndFeel_V4::ColourScheme::UIColour;

    for (const auto dark : { false, true })
    {
        INFO ((dark ? "dunkel" : "hell"));
        const auto window = scheme (dark, UI::windowBackground);
        const auto widget = scheme (dark, UI::widgetBackground);
        const auto card = theme::getColour (theme::card, dark);
        const auto list = theme::getColour (theme::listBackground, dark);

        // Text (WCAG AA): Statuszeile und Hinweise auf dem Fenster, Protokoll im Textfeld, Fußzeilen auf Karten
        CHECK (theme::getContrastRatio (scheme (dark, UI::defaultText), window) >= 7.0);
        CHECK (theme::getContrastRatio (scheme (dark, UI::defaultText), widget) >= 7.0);
        CHECK (theme::getContrastRatio (theme::getColour (theme::primaryText, dark), card) >= 7.0);
        CHECK (theme::getContrastRatio (theme::getColour (theme::primaryText, dark), list) >= 7.0);

        for (const auto id : { theme::secondaryText, theme::error, theme::warning })
        {
            CHECK (theme::getContrastRatio (theme::getColour (id, dark), window) >= 4.5);
            CHECK (theme::getContrastRatio (theme::getColour (id, dark), widget) >= 4.5);
            CHECK (theme::getContrastRatio (theme::getColour (id, dark), card) >= 4.5);
        }

        // Platzhalter und Grafik: mindestens 3:1
        CHECK (theme::getContrastRatio (theme::getColour (theme::tertiaryText, dark), window) >= 3.0);
        CHECK (theme::getContrastRatio (theme::getColour (theme::tertiaryText, dark), list) >= 3.0);

        // Bildschirme: Phosphor und Wellenform auf dem Bildschirm, Akzent neben dem Ink-Rand
        const auto screen = theme::getColour (theme::screen, dark);
        CHECK (theme::getContrastRatio (theme::getColour (theme::phosphor, dark), screen) >= 7.0);
        CHECK (theme::getContrastRatio (theme::getColour (theme::waveform, dark), theme::getColour (theme::waveformBackground, dark)) >= 7.0);
        CHECK (theme::getContrastRatio (theme::getColour (theme::accent, dark), theme::getColour (theme::ink, dark)) >= 2.5);
        CHECK (theme::getContrastRatio (theme::getColour (theme::accentText, dark), card) >= 3.0);

        // Orange der Vorlage: Marker X und Fahnen sind zusätzlich beschriftet
        CHECK (theme::getContrastRatio (theme::getColour (theme::crossfade, dark), card) >= 2.0);

        // Beschriftung der Marker-Fahnen und eingeschalteter Knöpfe
        CHECK (theme::getContrastRatio (theme::getColour (theme::onAccent, dark), theme::getColour (theme::accent, dark)) >= 4.5);
        CHECK (theme::getContrastRatio (scheme (dark, UI::highlightedText), scheme (dark, UI::highlightedFill)) >= 4.5);
    }
}

TEST_CASE ("Theme: Hell und Dunkel unterscheiden sich", "[theme]")
{
    using UI = juce::LookAndFeel_V4::ColourScheme::UIColour;

    CHECK (scheme (true, UI::windowBackground).getPerceivedBrightness() < 0.4f);
    CHECK (scheme (false, UI::windowBackground).getPerceivedBrightness() > 0.8f);

    // Akzent, Statusfarben und Phosphor sind in beiden Darstellungen gleich (Vorlage)
    for (const auto id : theme::allColourIds)
    {
        const auto shared = std::find (std::begin (theme::sharedColourIds), std::end (theme::sharedColourIds), id)
                            != std::end (theme::sharedColourIds);
        CHECK ((theme::getColour (id, true) != theme::getColour (id, false)) == ! shared);
    }
}

TEST_CASE ("Theme: LookAndFeel und gebundene Farben", "[theme]")
{
    AppLookAndFeel lookAndFeel;
    CHECK (lookAndFeel.isDark());

    juce::Component parent;
    juce::Label label;
    parent.setLookAndFeel (&lookAndFeel);
    parent.addAndMakeVisible (label);

    theme::bindColour (label, juce::Label::textColourId, theme::secondaryText);
    CHECK (label.findColour (juce::Label::textColourId) == theme::getColour (theme::secondaryText, true));

    // Neue Bindung derselben Zielfarbe ersetzt die alte
    theme::bindColour (label, juce::Label::textColourId, theme::error);
    theme::bindColour (label, juce::Label::outlineColourId, theme::controlOutline);

    lookAndFeel.setDark (false);
    CHECK (lookAndFeel.findColour (theme::accent) == theme::getColour (theme::accent, false));
    CHECK (lookAndFeel.findColour (juce::TextButton::buttonOnColourId) == theme::getColour (theme::accent, false));

    // Gesetzte Farben bleiben bis zum Erneuern stehen
    CHECK (label.findColour (juce::Label::textColourId) == theme::getColour (theme::error, true));

    theme::refreshColourBindings (parent);
    CHECK (label.findColour (juce::Label::textColourId) == theme::getColour (theme::error, false));
    CHECK (label.findColour (juce::Label::outlineColourId) == theme::getColour (theme::controlOutline, false));

    parent.removeChildComponent (&label);
    parent.setLookAndFeel (nullptr);
}
