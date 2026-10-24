#include "Theme.h"

namespace autosampler
{

juce::String toString (AppearanceMode mode)
{
    switch (mode)
    {
        case AppearanceMode::light: return "light";
        case AppearanceMode::dark:  return "dark";
        case AppearanceMode::system: break;
    }

    return "system";
}

AppearanceMode appearanceModeFromString (const juce::String& text)
{
    if (text == "light") return AppearanceMode::light;
    if (text == "dark")  return AppearanceMode::dark;
    return AppearanceMode::system;
}

namespace theme
{

namespace
{
    const juce::Identifier bindingsProperty ("themeColourBindings");
    const juce::Identifier iconProperty ("themeIcon");
    const juce::Identifier primaryProperty ("themePrimary");
    const juce::Identifier tintProperty ("themeTint");
    const juce::Identifier cartridgeProperty ("themeCartridge");
    const juce::Identifier stepperProperty ("themeStepper");
    const juce::Identifier cornerProperty ("themeCornerSize");
    const juce::Identifier hintProperty ("themeHint");
    const juce::Identifier keyTextProperty ("themeKeyText");
    const juce::Identifier chipToggleProperty ("themeChipToggle");

    constexpr float defaultCornerSize = 7.0f;

    juce::Typeface::Ptr screenTypeface, plateTypeface, logoTypeface;

    // Kontraste sind in ThemeTests geprüft: Text mindestens 4,5:1, Grafik und Platzhalter 3:1
    struct Entry
    {
        ColourIds id;
        juce::uint32 dark, light;
    };

    // Werte der Designvorlage "Autosampler Retro": hell = Putty, dunkel = Night
    constexpr Entry entries[] {
        { accent,             0xff7a4fe0, 0xff7a4fe0 },
        { onAccent,           0xffffffff, 0xffffffff },
        { crossfade,          0xfff08a24, 0xfff08a24 },
        { waveform,           0xff86f5b0, 0xff86f5b0 },
        { waveformBackground, 0xff060e0a, 0xff0b1610 },
        { card,               0xff353f5e, 0xffefe8d8 },
        { cardOutline,        0xff070d1c, 0xff2a2620 },
        { primaryText,        0xffeee7d7, 0xff2a2620 },
        { secondaryText,      0xffb4bbcd, 0xff5b5345 },
        { tertiaryText,       0xff8892b2, 0xff7a705e },
        { error,              0xffff8a7e, 0xffb3261e },
        { warning,            0xffffc23d, 0xff8a4b00 },
        { locked,             0xccffc23d, 0xcc9a5c00 },
        { playhead,           0xffffffff, 0xffffffff },
        { listBackground,     0xff353f5e, 0xffefe8d8 },
        { listSelection,      0x447a4fe0, 0x337a4fe0 },
        { controlOutline,     0xff070d1c, 0xff2a2620 },
        { canvas,             0xff2e3854, 0xffe6decb },
        { toolbar,            0xff283150, 0xffddd3bd },
        { sidebar,            0xff2e3854, 0xffe6decb },
        { separator,          0x2eeee7d7, 0x382a2620 },
        { control,            0xff4a5682, 0xfff7f2e6 },
        { field,              0xff060e0a, 0xff0b1610 },
        { chip,               0xffff8cc4, 0xffff8cc4 },
        { segmented,          0xff232b45, 0xffd3c8ae },
        { segmentedActive,    0xff7a4fe0, 0xff7a4fe0 },
        { switchOff,          0xff1a2238, 0xff6a5f4c },
        { track,              0xff0e1428, 0xff3b352c },
        { pending,            0xff6a7290, 0xff9a8f78 },
        { accentText,         0xffb79cff, 0xff7a4fe0 },
        { running,            0xffff5fa8, 0xffff5fa8 },
        { success,            0xff5bff8f, 0xff5bff8f },
        { keyWhite,           0xffdcd5c4, 0xfff7f1e3 },
        { keyBlack,           0xff0e1324, 0xff2a2620 },
        { keyBorder,          0xff070d1c, 0xff2a2620 },
        { keyLabel,           0xff3e4560, 0xff5b5345 },
        { room,               0xff0b1630, 0xff1b3a6b },
        { roomGrid,           0x1778aaff, 0x14ffffff },
        { ink,                0xff070d1c, 0xff2a2620 },
        { caseHighlight,      0x1affffff, 0x8cffffff },
        { deck,               0xff283150, 0xffddd3bd },
        { bezel,              0xff232b45, 0xffd3c8ae },
        { plate,              0xffeee7d7, 0xff2a2620 },
        { plateText,          0xff1a2238, 0xfff1eadb },
        { screw,              0xff5a6690, 0xffbfb49a },
        { screen,             0xff060e0a, 0xff0b1610 },
        { screenHighlight,    0xff0f2016, 0xff13261b },
        { phosphor,           0xff86f5b0, 0xff86f5b0 },
        { pink,               0xffff5fa8, 0xffff5fa8 },
        { yellow,             0xffffc23d, 0xffffc23d },
        { red,                0xffff6b5e, 0xffff6b5e },
        { led,                0xff5bff8f, 0xff5bff8f }
    };

    double getLuminance (juce::Colour c)
    {
        const auto channel = [] (juce::uint8 value)
        {
            const auto v = value / 255.0;
            return v <= 0.03928 ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4);
        };

        return 0.2126 * channel (c.getRed()) + 0.7152 * channel (c.getGreen()) + 0.0722 * channel (c.getBlue());
    }

    /** Pfade der Zeichen in den Koordinaten der Vorlage (SVG-Pfade). */
    struct IconShape
    {
        float width, height;
        const char* stroke;      // Linienzüge
        const char* fill;        // gefüllte Flächen
    };

    IconShape getShape (Icon icon)
    {
        switch (icon)
        {
            case Icon::play:          return { 10, 10, nullptr, "M2 1.2v7.6L8.5 5z" };
            case Icon::lock:          return { 11, 12, "M3.3 5.5V4a2.2 2.2 0 0 1 4.4 0", nullptr };
            case Icon::loop:          return { 12, 10, "M3.5 2.5H8a2.5 2.5 0 0 1 0 5H4a2.5 2.5 0 0 1-2.5-2.5M5 1 3.5 2.5 5 4", nullptr };
            case Icon::search:        return { 13, 13, "M8.5 8.5 12 12", nullptr };
            case Icon::chevronUpDown: return { 10, 12, "M3 4.5 5 2.5l2 2M3 7.5l2 2 2-2", nullptr };
            case Icon::undo:          return { 14, 14, "M5 3 2 6l3 3M2.5 6H9a3 3 0 0 1 0 6H7", nullptr };
            case Icon::redo:          return { 14, 14, "M9 3l3 3-3 3M11.5 6H5a3 3 0 0 0 0 6h2", nullptr };
            case Icon::plug:          return { 14, 14, "M1.5 5h11", nullptr };
            case Icon::sliders:       return { 14, 14, "M3 2v10M7 2v10M11 2v10", nullptr };
            case Icon::folder:        return { 14, 12, nullptr, "M1 2.5A1.5 1.5 0 0 1 2.5 1h3l1.5 1.5h4.5A1.5 1.5 0 0 1 13 4v5.5A1.5 1.5 0 0 1 11.5 11h-9A1.5 1.5 0 0 1 1 9.5z" };
            case Icon::chevronRight:  return { 10, 10, "M3.5 1.5 7 5 3.5 8.5", nullptr };
            case Icon::learn:         return { 12, 12, nullptr, nullptr };
            case Icon::none:          break;
        }

        return { 0, 0, nullptr, nullptr };
    }
}

juce::LookAndFeel_V4::ColourScheme getColourScheme (bool dark)
{
    // Reihenfolge: Fenster, Widget, Menü, Rahmen, Text, Füllung, Text hervorgehoben, Füllung hervorgehoben, Menütext.
    if (dark)
        return { getColour (canvas, true).getARGB(), getColour (card, true).getARGB(), getColour (card, true).getARGB(),
                 getColour (cardOutline, true).getARGB(), getColour (primaryText, true).getARGB(), getColour (accent, true).getARGB(),
                 0xffffffff, getColour (accent, true).getARGB(), getColour (primaryText, true).getARGB() };

    return { getColour (canvas, false).getARGB(), getColour (card, false).getARGB(), getColour (card, false).getARGB(),
             getColour (cardOutline, false).getARGB(), getColour (primaryText, false).getARGB(), getColour (accent, false).getARGB(),
             0xffffffff, getColour (accent, false).getARGB(), getColour (primaryText, false).getARGB() };
}

juce::Colour getColour (ColourIds id, bool dark)
{
    for (const auto& entry : entries)
        if (entry.id == id)
            return juce::Colour (dark ? entry.dark : entry.light);

    jassertfalse;
    return {};
}

double getContrastRatio (juce::Colour a, juce::Colour b)
{
    const auto la = getLuminance (a), lb = getLuminance (b);
    return (juce::jmax (la, lb) + 0.05) / (juce::jmin (la, lb) + 0.05);
}

void bindColour (juce::Component& component, int targetColourId, int sourceColourId)
{
    auto bindings = component.getProperties()[bindingsProperty].toString();
    const auto target = juce::String::toHexString (targetColourId);
    auto pairs = juce::StringArray::fromTokens (bindings, ";", {});

    // Eine neue Bindung derselben Zielfarbe ersetzt die alte (z. B. Status normal/Fehler)
    for (int i = pairs.size(); --i >= 0;)
        if (pairs[i].upToFirstOccurrenceOf ("=", false, false) == target)
            pairs.remove (i);

    pairs.add (target + "=" + juce::String::toHexString (sourceColourId));
    component.getProperties().set (bindingsProperty, pairs.joinIntoString (";"));
    component.setColour (targetColourId, component.getLookAndFeel().findColour (sourceColourId));
}

void refreshColourBindings (juce::Component& component)
{
    if (const auto* bindings = component.getProperties().getVarPointer (bindingsProperty))
    {
        for (const auto& pair : juce::StringArray::fromTokens (bindings->toString(), ";", {}))
        {
            const auto target = pair.upToFirstOccurrenceOf ("=", false, false).getHexValue32();
            const auto source = pair.fromFirstOccurrenceOf ("=", false, false).getHexValue32();
            component.setColour (target, component.getLookAndFeel().findColour (source));
        }
    }

    for (auto* child : component.getChildren())
        refreshColourBindings (*child);
}

//==============================================================================
void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto shape = getShape (icon);

    if (shape.width <= 0.0f)
        return;

    const auto scale = juce::jmin (area.getWidth() / shape.width, area.getHeight() / shape.height);
    const auto origin = area.getCentre() - juce::Point<float> (shape.width, shape.height) * (0.5f * scale);
    const auto toArea = juce::AffineTransform::scale (scale).translated (origin);
    const auto strokeWidth = 1.3f * scale;
    const juce::PathStrokeType stroke (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    g.setColour (colour);

    if (shape.fill != nullptr)
    {
        auto path = juce::Drawable::parseSVGPath (shape.fill);
        path.applyTransform (toArea);
        g.fillPath (path);
    }

    if (shape.stroke != nullptr)
    {
        auto path = juce::Drawable::parseSVGPath (shape.stroke);
        path.applyTransform (toArea);
        g.strokePath (path, stroke);
    }

    // Zeichen mit Grundformen
    switch (icon)
    {
        case Icon::lock:
        {
            const auto body = juce::Rectangle<float> (1.5f, 5.5f, 8.0f, 5.5f).transformedBy (toArea);
            g.drawRoundedRectangle (body, 1.2f * scale, strokeWidth);
            break;
        }

        case Icon::search:
            g.drawEllipse (juce::Rectangle<float> (1.5f, 1.5f, 8.0f, 8.0f).transformedBy (toArea), strokeWidth);
            break;

        case Icon::plug:
            g.drawRoundedRectangle (juce::Rectangle<float> (1.5f, 2.5f, 11.0f, 9.0f).transformedBy (toArea), 2.0f * scale, strokeWidth);
            break;

        case Icon::sliders:
            for (const auto& [x, y] : { std::pair { 3.0f, 9.0f }, { 7.0f, 4.5f }, { 11.0f, 7.5f } })
            {
                const auto knob = juce::Rectangle<float> (x - 1.6f, y - 1.6f, 3.2f, 3.2f).transformedBy (toArea);
                g.drawEllipse (knob, strokeWidth);
            }
            break;

        case Icon::learn:
            g.drawEllipse (juce::Rectangle<float> (1.5f, 1.5f, 9.0f, 9.0f).transformedBy (toArea), strokeWidth);
            g.fillEllipse (juce::Rectangle<float> (4.4f, 4.4f, 3.2f, 3.2f).transformedBy (toArea));
            break;

        case Icon::none:
        case Icon::play:
        case Icon::loop:
        case Icon::chevronUpDown:
        case Icon::undo:
        case Icon::redo:
        case Icon::folder:
        case Icon::chevronRight:
            break;
    }
}

void setIcon (juce::Button& button, Icon icon)
{
    button.getProperties().set (iconProperty, (int) icon);
    button.repaint();
}

Icon getIcon (const juce::Component& component)
{
    return (Icon) (int) component.getProperties()[iconProperty];
}

void setPrimary (juce::Button& button, bool primary)
{
    button.getProperties().set (primaryProperty, primary);
    button.repaint();
}

bool isPrimary (const juce::Component& component)
{
    return (bool) component.getProperties()[primaryProperty];
}

void setTint (juce::Button& button, juce::Colour colour)
{
    button.getProperties().set (tintProperty, (juce::int64) colour.getARGB());
    button.repaint();
}

juce::Colour getTint (const juce::Component& component)
{
    const auto& value = component.getProperties()[tintProperty];
    return value.isVoid() ? juce::Colour() : juce::Colour ((juce::uint32) (juce::int64) value);
}

void setCartridge (juce::ComboBox& box, bool cartridge)
{
    box.getProperties().set (cartridgeProperty, cartridge);

    if (cartridge)
        box.setColour (juce::ComboBox::textColourId, juce::Colour (0xff2a2620));

    box.repaint();
}

void setCornerSize (juce::Component& component, float size)
{
    component.getProperties().set (cornerProperty, size);
    component.repaint();
}

float getCornerSize (const juce::Component& component)
{
    const auto& value = component.getProperties()[cornerProperty];
    return value.isDouble() ? (float) (double) value : defaultCornerSize;
}

void setChipToggle (juce::Button& button, bool chip)
{
    button.getProperties().set (chipToggleProperty, chip);
    button.repaint();
}

void setKeyText (juce::Button& button, const juce::String& text)
{
    button.getProperties().set (keyTextProperty, text);
    button.repaint();
}

void setHint (juce::Button& button, const juce::String& hint)
{
    button.getProperties().set (hintProperty, hint);
    button.repaint();
}

void prepareTextEditor (juce::TextEditor& editor)
{
    editor.setOpaque (false);
    editor.setJustification (juce::Justification::centredLeft);
    editor.setIndents (8, 0);
    editor.setBorder ({ 0, 2, 0, 2 });
    editor.setFont (getFont (Face::screen, 21.0f));
}

//==============================================================================
void registerTypefaces (juce::Typeface::Ptr screen, juce::Typeface::Ptr plateBold, juce::Typeface::Ptr logoBlack)
{
    screenTypeface = std::move (screen);
    plateTypeface = std::move (plateBold);
    logoTypeface = std::move (logoBlack);
}

juce::Font getFont (Face face, float cssPixels, bool bold)
{
    // JUCE misst Ober- plus Unterlänge, CSS die Schriftgröße: Verhältnis aus den Schriftdaten
    switch (face)
    {
        case Face::screen:
            if (screenTypeface != nullptr)
                return juce::Font (juce::FontOptions (screenTypeface).withHeight (cssPixels * 1.0f));

            return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), cssPixels * 1.0f, juce::Font::plain));

        case Face::plate:
            if (plateTypeface != nullptr)
                return juce::Font (juce::FontOptions (plateTypeface).withHeight (cssPixels * 1.185f));

            return juce::Font (juce::FontOptions (cssPixels * 1.185f)).boldened();

        case Face::logo:
            if (logoTypeface != nullptr)
                return juce::Font (juce::FontOptions (logoTypeface).withHeight (cssPixels * 1.185f));

            return juce::Font (juce::FontOptions (cssPixels * 1.185f)).boldened();

        case Face::ui:
            break;
    }

    juce::Font font { juce::FontOptions (fontSize (cssPixels)) };
    return bold ? font.boldened() : font;
}

juce::Font getKeyFont (float cssPixels)
{
    return getFont (Face::ui, cssPixels, true).withExtraKerningFactor (0.05f);
}

//==============================================================================
void drawKey (juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour fill,
              juce::Colour ink, bool pressed, float alpha)
{
    auto body = bounds.withTrimmedBottom ((float) keyDepth);
    const auto shadow = pressed ? 1.0f : (float) keyDepth;

    if (pressed)
        body = body.translated (0.0f, 2.0f);

    g.setColour (ink.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (body.translated (0.0f, shadow), radius);

    g.setColour (fill.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (body, radius);

    // Lichtkante oben und Schatten unten (gedrückt: Innenschatten oben)
    const auto inner = body.reduced (radius * 0.6f + 1.0f, 2.0f);

    if (pressed)
    {
        g.setColour (juce::Colours::black.withAlpha (0.22f * alpha));
        g.fillRect (inner.withHeight (2.0f));
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.32f * alpha));
        g.fillRect (inner.withHeight (2.0f));
        g.setColour (juce::Colours::black.withAlpha (0.13f * alpha));
        g.fillRect (inner.withTop (inner.getBottom() - 2.0f));
    }

    g.setColour (ink.withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (body.reduced (1.0f), juce::jmax (0.0f, radius - 1.0f), 2.0f);
}

void drawScreen (juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour screen,
                 juce::Colour screenHighlight, juce::Colour ink, float alpha)
{
    juce::ColourGradient glow (screenHighlight.withMultipliedAlpha (alpha), bounds.getCentre(),
                               screen.withMultipliedAlpha (alpha),
                               { bounds.getCentreX() + bounds.getWidth() * 0.5f, bounds.getCentreY() + bounds.getHeight() * 0.9f },
                               true);
    g.setGradientFill (glow);
    g.fillRoundedRectangle (bounds, radius);

    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (bounds, radius);
        g.reduceClipRegion (clip);

        // Rasterzeilen
        g.setColour (juce::Colours::black.withAlpha (0.30f * alpha));

        for (auto y = bounds.getY() + 1.0f; y < bounds.getBottom(); y += 3.0f)
            g.fillRect (bounds.getX(), y, bounds.getWidth(), 1.0f);

        // Innenschatten
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha ((0.42f - 0.1f * (float) i) * alpha));
            g.drawRoundedRectangle (bounds.reduced (1.0f + (float) i * 1.5f), juce::jmax (0.0f, radius - (float) i), 1.5f);
        }
    }

    g.setColour (ink.withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (bounds.reduced (1.0f), juce::jmax (0.0f, radius - 1.0f), 2.0f);
}

void drawGlowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                   juce::Justification justification, juce::Colour colour, const juce::Font& font)
{
    g.setFont (font);
    g.setColour (colour.withMultipliedAlpha (0.28f));

    for (const auto& [dx, dy] : { std::pair { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } })
        g.drawText (text, area.translated (dx, dy), justification, true);

    g.setColour (colour);
    g.drawText (text, area, justification, true);
}

int getPlateWidth (const juce::String& title)
{
    return juce::GlyphArrangement::getStringWidthInt (getFont (Face::plate, 12.0f).withExtraKerningFactor (0.08f), title.toUpperCase()) + 18;
}

int drawPlate (juce::Graphics& g, juce::Point<int> topLeft, const juce::String& title, const juce::LookAndFeel& lf)
{
    const auto font = getFont (Face::plate, 12.0f).withExtraKerningFactor (0.08f);
    const auto text = title.toUpperCase();
    const auto width = juce::GlyphArrangement::getStringWidthInt (font, text) + 18;
    const juce::Rectangle<int> area (topLeft.x, topLeft.y, width, 20);

    g.setColour (lf.findColour (theme::plate));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (lf.findColour (theme::plateText));
    g.setFont (font);
    g.drawText (text, area, juce::Justification::centred);
    return width;
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title,
                const juce::LookAndFeel& lf, float alpha)
{
    const auto body = bounds.withTrimmedBottom ((float) panelDepth);
    const auto ink = lf.findColour (theme::ink).withMultipliedAlpha (alpha);

    g.setColour (ink);
    g.fillRoundedRectangle (body.translated (0.0f, (float) panelDepth), 12.0f);
    g.setColour (lf.findColour (card).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (body, 12.0f);
    g.setColour (ink);
    g.drawRoundedRectangle (body.reduced (1.0f), 11.0f, 2.0f);

    if (title.isNotEmpty())
        drawPlate (g, { juce::roundToInt (body.getX()) + 12, juce::roundToInt (body.getY()) + 10 }, title, lf);

    // Zwei Schrauben rechts oben
    for (int i = 0; i < 2; ++i)
    {
        const auto screwArea = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ body.getRight() - 16.0f - 14.0f * (float) (1 - i), body.getY() + 20.0f });
        g.setColour (lf.findColour (screw).withMultipliedAlpha (alpha));
        g.fillEllipse (screwArea);
        g.setColour (ink);
        g.drawEllipse (screwArea.reduced (0.75f), 1.5f);
    }
}

void drawDeckBar (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::LookAndFeel& lf)
{
    g.setColour (lf.findColour (deck));
    g.fillRect (bounds);
    g.setColour (lf.findColour (caseHighlight));
    g.fillRect (bounds.withTrimmedTop (3).withHeight (2));
    g.setColour (lf.findColour (ink));
    g.fillRect (bounds.withHeight (3));
}

juce::Rectangle<int> getBarDisplayBounds (juce::Rectangle<int> barBounds, int rightEdge)
{
    auto area = barBounds.reduced (16, 0).withTrimmedTop (3).withSizeKeepingCentre (barBounds.getWidth(), barBounds.getHeight() - 3);
    return area.reduced (16, 7).withRight (rightEdge - 16).withHeight (42).withY (barBounds.getY() + 3 + (barBounds.getHeight() - 3 - 42) / 2);
}

void drawScreenLed (juce::Graphics& g, juce::Rectangle<float> led, juce::Colour colour, bool lit)
{
    if (lit)
    {
        g.setColour (colour.withAlpha (0.3f));
        g.fillRect (led.expanded (2.5f));
        g.setColour (colour);
        g.fillRect (led);
    }
    else
    {
        g.setColour (colour.withAlpha (0.4f));
        g.drawRect (led, 2.0f);
    }
}

} // namespace theme

//==============================================================================
AppLookAndFeel::AppLookAndFeel()
{
    setDefaultSansSerifTypefaceName ("Menlo");
    setDark (true);
}

void AppLookAndFeel::setDark (bool shouldBeDark)
{
    dark = shouldBeDark;
    setColourScheme (theme::getColourScheme (dark));

    for (const auto id : theme::allColourIds)
        setColour (id, theme::getColour (id, dark));

    const auto col = [this] (theme::ColourIds id) { return theme::getColour (id, dark); };

    // Eingeschaltete Knöpfe (A/B, Learn) und Drehregler in der Akzentfarbe
    setColour (juce::TextButton::buttonColourId, col (theme::control));
    setColour (juce::TextButton::buttonOnColourId, col (theme::accent));
    setColour (juce::TextButton::textColourOffId, col (theme::primaryText));
    setColour (juce::TextButton::textColourOnId, col (theme::onAccent));
    setColour (juce::ToggleButton::textColourId, col (theme::primaryText));
    setColour (juce::Slider::rotarySliderFillColourId, col (theme::accent));
    setColour (juce::Slider::rotarySliderOutlineColourId, col (theme::track));
    setColour (juce::Slider::trackColourId, col (theme::accent));
    setColour (juce::Slider::backgroundColourId, col (theme::track));
    setColour (juce::Slider::thumbColourId, col (theme::control));
    setColour (juce::Slider::textBoxTextColourId, col (theme::primaryText));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, col (theme::phosphor).withAlpha (0.35f));
    setColour (juce::ListBox::backgroundColourId, col (theme::listBackground));
    setColour (juce::ListBox::outlineColourId, col (theme::cardOutline));

    setColour (juce::ComboBox::backgroundColourId, col (theme::screen));
    setColour (juce::ComboBox::outlineColourId, col (theme::ink));
    setColour (juce::ComboBox::textColourId, col (theme::phosphor));
    setColour (juce::ComboBox::arrowColourId, col (theme::phosphor));
    setColour (juce::ComboBox::focusedOutlineColourId, col (theme::pink));
    setColour (juce::TextEditor::backgroundColourId, col (theme::screen));
    setColour (juce::TextEditor::textColourId, col (theme::phosphor));
    setColour (juce::TextEditor::outlineColourId, col (theme::ink));
    setColour (juce::TextEditor::focusedOutlineColourId, col (theme::pink));
    setColour (juce::TextEditor::highlightColourId, col (theme::phosphor).withAlpha (0.3f));
    setColour (juce::TextEditor::highlightedTextColourId, col (theme::phosphor));
    setColour (juce::CaretComponent::caretColourId, col (theme::phosphor));
    setColour (juce::Label::textColourId, col (theme::primaryText));
    setColour (juce::GroupComponent::textColourId, col (theme::secondaryText));
    setColour (juce::GroupComponent::outlineColourId, col (theme::cardOutline));
    setColour (juce::ProgressBar::foregroundColourId, col (theme::phosphor));
    setColour (juce::ProgressBar::backgroundColourId, col (theme::track));
    setColour (juce::PopupMenu::backgroundColourId, col (theme::card));
    setColour (juce::PopupMenu::textColourId, col (theme::primaryText));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col (theme::accent));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ScrollBar::thumbColourId, col (theme::secondaryText).withAlpha (0.45f));
    setColour (juce::TooltipWindow::backgroundColourId, col (theme::card));
    setColour (juce::TooltipWindow::textColourId, col (theme::primaryText));
    setColour (juce::TooltipWindow::outlineColourId, col (theme::cardOutline));
}

//==============================================================================
juce::Font AppLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return theme::getKeyFont (12.0f);
}

void AppLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                           bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const auto alpha = button.isEnabled() ? 1.0f : 0.45f;
    const auto radius = theme::getCornerSize (button);
    const auto chipToggle = (bool) button.getProperties()[theme::chipToggleProperty];
    const auto tint = theme::getTint (button);
    const auto accentFill = theme::isPrimary (button) || (button.getToggleState() && ! chipToggle);

    auto fill = findColour (accentFill ? theme::accent
                          : button.getToggleState() && chipToggle ? theme::chip
                                                                   : theme::control);

    if (! accentFill && tint.getAlpha() > 0)
        fill = tint;

    if (highlighted && ! down && button.isEnabled())
        fill = fill.brighter (0.07f);

    const auto pressed = (down && ! theme::isPrimary (button)) || button.getToggleState() || (down && theme::isPrimary (button));
    theme::drawKey (g, bounds, radius, fill, findColour (theme::ink), pressed, alpha);
}

void AppLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    const auto chipToggle = (bool) button.getProperties()[theme::chipToggleProperty];
    const auto accentFill = theme::isPrimary (button) || (button.getToggleState() && ! chipToggle);
    const auto tinted = theme::getTint (button).getAlpha() > 0 && ! accentFill;
    const auto alpha = button.isEnabled() ? 1.0f : 0.45f;
    const auto stepper = (bool) button.getProperties()[theme::stepperProperty];
    const auto pressed = down || button.getToggleState();

    auto colour = accentFill ? findColour (theme::onAccent)
                : tinted || (button.getToggleState() && chipToggle) ? juce::Colour (0xff2a2620)
                                                                    : button.findColour (juce::TextButton::textColourOffId);
    colour = colour.withMultipliedAlpha (alpha);

    auto bounds = button.getLocalBounds().withTrimmedBottom (theme::keyDepth);

    if (pressed)
        bounds = bounds.translated (0, 2);

    auto font = stepper ? theme::getFont (theme::Face::ui, 15.0f, true) : getTextButtonFont (button, bounds.getHeight());
    const auto icon = theme::getIcon (button);
    const auto text = stepper ? button.getButtonText() : button.getButtonText().toUpperCase();
    const auto textWidth = juce::GlyphArrangement::getStringWidthInt (font, text);
    const auto iconSize = icon == theme::Icon::none ? 0 : (icon == theme::Icon::play ? 10 : 13);
    const auto gap = icon != theme::Icon::none && text.isNotEmpty() ? 6 : 0;
    const auto total = iconSize + gap + textWidth;
    auto content = bounds.withSizeKeepingCentre (juce::jmin (total, bounds.getWidth() - 4), bounds.getHeight());

    if (icon != theme::Icon::none)
    {
        auto iconArea = content.removeFromLeft (iconSize).toFloat().withSizeKeepingCentre ((float) iconSize, (float) iconSize);

        // Das Schloss schließt sich, wenn der Knopf eingeschaltet ist
        if (icon == theme::Icon::lock && button.getToggleState())
        {
            theme::drawIcon (g, icon, iconArea, colour);
            g.setColour (colour);
            g.fillRoundedRectangle (iconArea.withTrimmedTop (iconArea.getHeight() * 0.46f).reduced (iconArea.getWidth() * 0.14f, 0.0f), 1.5f);
        }
        else
        {
            theme::drawIcon (g, icon, iconArea, colour);
        }

        content.removeFromLeft (gap);
    }

    if (text.isNotEmpty())
    {
        g.setColour (colour);
        g.setFont (font);
        g.drawFittedText (text, content, icon == theme::Icon::none ? juce::Justification::centred : juce::Justification::centredLeft, 1);
    }
}

void AppLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    constexpr int keyWidth = 74;
    const auto bounds = button.getLocalBounds();
    const auto alpha = button.isEnabled() ? 1.0f : 0.45f;
    const auto on = button.getToggleState();
    const auto ink = findColour (theme::ink);

    const auto keyBounds = bounds.withLeft (bounds.getRight() - keyWidth).withSizeKeepingCentre (keyWidth, juce::jmin (bounds.getHeight(), 31 + theme::keyDepth))
                                 .toFloat();
    auto fill = findColour (theme::control);

    if (highlighted && button.isEnabled())
        fill = fill.brighter (0.07f);

    theme::drawKey (g, keyBounds, 7.0f, fill, ink, on, alpha);

    // Kontrollleuchte und AN/AUS
    const auto body = keyBounds.withTrimmedBottom ((float) theme::keyDepth).translated (0.0f, on ? 2.0f : 0.0f);
    const auto led = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ body.getX() + 17.0f, body.getCentreY() });

    if (on)
    {
        g.setColour (findColour (theme::led).withAlpha (0.3f * alpha));
        g.fillEllipse (led.expanded (3.0f));
    }

    g.setColour (findColour (on ? theme::led : theme::switchOff).withMultipliedAlpha (alpha));
    g.fillEllipse (led);
    g.setColour (ink.withMultipliedAlpha (alpha));
    g.drawEllipse (led.reduced (0.75f), 1.5f);

    g.setColour (findColour (theme::primaryText).withMultipliedAlpha (alpha));
    g.setFont (theme::getFont (theme::Face::ui, 11.0f, true));
    const auto keyText = button.getProperties()[theme::keyTextProperty].toString();
    g.drawText (keyText.isNotEmpty() ? keyText : juce::String (on ? "AN" : "AUS"), body.withTrimmedLeft (28.0f).toNearestInt(), juce::Justification::centredLeft);

    if (button.hasKeyboardFocus (false))
    {
        g.setColour (findColour (theme::pink).withAlpha (0.8f));
        g.drawRoundedRectangle (body.expanded (2.5f), 9.0f, 1.5f);
    }

    if (keyText.isNotEmpty())
        return;

    auto textArea = bounds.withTrimmedRight (keyWidth + 12);
    const auto hint = button.getProperties()[theme::hintProperty].toString();

    g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (alpha));
    g.setFont (theme::getFont (theme::Face::ui, 13.0f));

    if (hint.isNotEmpty())
    {
        g.drawFittedText (button.getButtonText(), textArea.removeFromTop (textArea.getHeight() / 2 + 1),
                          juce::Justification::bottomLeft, 1);
        g.setColour (findColour (theme::secondaryText).withMultipliedAlpha (alpha));
        g.setFont (theme::getFont (theme::Face::ui, 11.0f));
        g.drawFittedText (hint, textArea, juce::Justification::topLeft, 1);
    }
    else
    {
        g.drawFittedText (button.getButtonText(), textArea, juce::Justification::centredLeft, 1);
    }
}

void AppLookAndFeel::changeToggleButtonWidthToFitText (juce::ToggleButton& button)
{
    const auto font = theme::getFont (theme::Face::ui, 13.0f);
    button.setSize (juce::GlyphArrangement::getStringWidthInt (font, button.getButtonText()) + 12 + 74, button.getHeight());
}

//==============================================================================
void AppLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat();
    const auto alpha = box.isEnabled() ? 1.0f : 0.45f;
    const auto ink = findColour (theme::ink);

    if ((bool) box.getProperties()[theme::cartridgeProperty])
    {
        // Kassettenschacht mit eingesteckter Kassette
        g.setColour (findColour (theme::track).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
        g.fillRect (bounds.reduced (6.0f, 0.0f).withHeight (3.0f).translated (0.0f, 2.0f));
        g.setColour (ink.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (bounds.reduced (1.0f), 7.0f, 2.0f);

        const auto cart = bounds.reduced (6.0f, 5.0f).withTrimmedRight (26.0f);
        g.setColour (juce::Colour (0xff7fd3c0).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (cart, 4.0f);
        g.setColour (ink.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (cart.reduced (1.0f), 3.0f, 2.0f);

        const auto eject = juce::Rectangle<float> (16.0f, 14.0f).withCentre ({ bounds.getRight() - 18.0f, bounds.getCentreY() });
        g.setColour (findColour (theme::screw).withMultipliedAlpha (alpha));
        juce::Path triangle;
        triangle.addTriangle (eject.getCentreX(), eject.getY(), eject.getRight() - 2.0f, eject.getCentreY() + 1.0f,
                              eject.getX() + 2.0f, eject.getCentreY() + 1.0f);
        g.fillPath (triangle);
        g.fillRect (eject.getX() + 2.0f, eject.getBottom() - 3.0f, eject.getWidth() - 4.0f, 2.0f);
        return;
    }

    theme::drawScreen (g, bounds, 6.0f, findColour (theme::screen), findColour (theme::screenHighlight), ink, alpha);

    if (box.hasKeyboardFocus (false))
    {
        g.setColour (findColour (theme::pink).withAlpha (0.8f * alpha));
        g.drawRoundedRectangle (bounds.reduced (0.75f), 6.0f, 1.5f);
    }

    juce::Path arrow;
    const auto centre = juce::Point<float> ((float) width - 16.0f, (float) height * 0.5f);
    arrow.addTriangle (centre.x - 5.0f, centre.y - 3.0f, centre.x + 5.0f, centre.y - 3.0f, centre.x, centre.y + 4.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (alpha));
    g.fillPath (arrow);
}

void AppLookAndFeel::drawComboBoxTextWhenNothingSelected (juce::Graphics& g, juce::ComboBox& box, juce::Label& label)
{
    // Standard zeichnet halbtransparent; auf dem Kassettenetikett ist das kaum lesbar
    g.setColour (box.findColour (juce::ComboBox::textColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.45f));
    g.setFont (getComboBoxFont (box));
    g.drawFittedText (box.getTextWhenNothingSelected(), label.getBounds().reduced (2, 0), label.getJustificationType(), 1);
}

juce::Font AppLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    if ((bool) box.getProperties()[theme::cartridgeProperty])
        return theme::getFont (theme::Face::ui, 12.0f, true);

    return theme::getFont (theme::Face::screen, 21.0f);
}

void AppLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const auto cartridge = (bool) box.getProperties()[theme::cartridgeProperty];

    if (cartridge)
    {
        label.setBounds (14, 6, box.getWidth() - 14 - 38, box.getHeight() - 12);
        label.setColour (juce::Label::textColourId, juce::Colours::black.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.45f));
    }
    else
    {
        label.setBounds (10, 2, box.getWidth() - 32, box.getHeight() - 4);
        label.setColour (juce::Label::textColourId, box.findColour (juce::ComboBox::textColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.45f));
    }

    label.setFont (getComboBoxFont (box));
}

juce::Font AppLookAndFeel::getPopupMenuFont()
{
    return theme::getFont (theme::Face::ui, 13.0f);
}

void AppLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
    const auto outline = label.findColour (juce::Label::outlineColourId);
    const auto bounds = label.getLocalBounds().toFloat();
    const auto field = outline.getAlpha() > 0;

    // Beschriftungen mit Rahmen sind Felder (Parametername) und erscheinen als Bildschirm
    if (field)
        theme::drawScreen (g, bounds, 6.0f, findColour (theme::screen), findColour (theme::screenHighlight), findColour (theme::ink), alpha);
    else
        g.fillAll (label.findColour (juce::Label::backgroundColourId));

    if (! label.isBeingEdited())
    {
        auto font = getLabelFont (label);

        if (field)
            font = theme::getFont (theme::Face::screen, 20.0f);

        const auto textColour = field ? findColour (theme::phosphor) : label.findColour (juce::Label::textColourId);
        g.setColour (textColour.withMultipliedAlpha (alpha));
        g.setFont (font);

        const auto textArea = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
        g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                          juce::jmax (1, (int) ((float) textArea.getHeight() / font.getHeight())),
                          label.getMinimumHorizontalScale());
    }
}

void AppLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    theme::drawScreen (g, juce::Rectangle<int> (0, 0, width, height).toFloat(), 6.0f, findColour (theme::screen),
                       findColour (theme::screenHighlight), editor.findColour (juce::TextEditor::outlineColourId),
                       editor.isEnabled() ? 1.0f : 0.5f);
}

void AppLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (editor.hasKeyboardFocus (true) && ! editor.isReadOnly())
    {
        g.setColour (editor.findColour (juce::TextEditor::focusedOutlineColourId).withAlpha (0.9f));
        g.drawRoundedRectangle (juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.75f), 6.0f, 1.5f);
    }
}

//==============================================================================
int AppLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    return slider.isHorizontal() ? 9 : juce::LookAndFeel_V4::getSliderThumbRadius (slider);
}

void AppLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                       juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto alpha = slider.isEnabled() ? 1.0f : 0.45f;
    const auto ink = findColour (theme::ink).withMultipliedAlpha (alpha);
    const auto centreY = (float) y + (float) height * 0.5f - 1.0f;
    const auto track = juce::Rectangle<float> ((float) x, centreY - 4.0f, (float) width, 8.0f);

    // Rille mit Füllung bis zum Griff
    g.setColour (slider.findColour (juce::Slider::backgroundColourId).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track, 4.0f);

    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (track, 4.0f);
        g.reduceClipRegion (clip);
        g.setColour (slider.findColour (juce::Slider::trackColourId).withMultipliedAlpha (alpha));
        g.fillRect (track.withRight (juce::jmax (track.getX(), sliderPos)));
    }

    g.setColour (ink);
    g.drawRoundedRectangle (track.reduced (1.0f), 3.0f, 2.0f);

    const auto thumb = juce::Rectangle<float> (14.0f, 20.0f + (float) theme::keyDepth).withCentre ({ sliderPos, centreY + (float) theme::keyDepth * 0.5f });
    theme::drawKey (g, thumb, 4.0f, slider.findColour (juce::Slider::thumbColourId), findColour (theme::ink), slider.isMouseButtonDown(), alpha);
}

juce::Button* AppLookAndFeel::createSliderButton (juce::Slider&, bool isIncrement)
{
    auto* button = new juce::TextButton (isIncrement ? juce::String::fromUTF8 ("+") : juce::String::fromUTF8 ("\xe2\x88\x92"));
    button->getProperties().set (theme::stepperProperty, true);
    button->setWantsKeyboardFocus (false);
    return button;
}

juce::Label* AppLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);

    if (slider.getSliderStyle() == juce::Slider::IncDecButtons)
        label->setJustificationType (juce::Justification::centredRight);

    label->setFont (theme::getFont (theme::Face::screen, 22.0f));
    label->setBorderSize ({ 1, 6, 0, 10 });
    return label;
}

void AppLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                                      double progress, const juce::String& text)
{
    if (progress < 0.0 || progress > 1.0)
    {
        juce::LookAndFeel_V4::drawProgressBar (g, bar, width, height, progress, text);
        return;
    }

    // Segmentierte Anzeige wie im Bildschirm der Laufleiste
    constexpr float segment = 10.0f, gap = 3.0f;
    const auto numSegments = juce::jmax (1, (int) (((float) width + gap) / (segment + gap)));
    const auto cellWidth = ((float) width + gap) / (float) numSegments - gap;
    const auto filled = (int) std::floor (progress * numSegments + 0.0001);
    const auto cellHeight = juce::jmin (16.0f, (float) height);
    const auto top = ((float) height - cellHeight) * 0.5f;
    const auto phosphor = bar.findColour (juce::ProgressBar::foregroundColourId);

    for (int i = 0; i < numSegments; ++i)
    {
        const auto cell = juce::Rectangle<float> ((float) i * (cellWidth + gap), top, cellWidth, cellHeight);

        if (i < filled)
        {
            g.setColour (phosphor.withAlpha (0.25f));
            g.fillRect (cell.expanded (1.5f));
            g.setColour (phosphor);
        }
        else if (i == filled && progress < 1.0)
        {
            g.setColour (findColour (theme::pink));
        }
        else
        {
            g.setColour (phosphor.withAlpha (0.12f));
        }

        g.fillRect (cell);
    }
}

void AppLookAndFeel::drawGroupComponentOutline (juce::Graphics& g, int width, int height, const juce::String& text,
                                                const juce::Justification&, juce::GroupComponent&)
{
    theme::drawPanel (g, juce::Rectangle<int> (0, 0, width, height).toFloat(), text, *this);
}

} // namespace autosampler
