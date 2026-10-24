#include "Widgets.h"

namespace autosampler
{

//==============================================================================
FormSection::FormSection (juce::String titleToUse)
    : title (std::move (titleToUse))
{
    setInterceptsMouseClicks (false, false);
}

void FormSection::setTitle (const juce::String& newTitle)
{
    title = newTitle;
    repaint();
}

void FormSection::setRows (std::vector<int> heights)
{
    rows = std::move (heights);
    repaint();
}

int FormSection::getPreferredHeight() const
{
    auto total = (title.isEmpty() ? 6 : titleHeight) + theme::panelDepth;

    for (const auto height : rows)
        total += height;

    return total;
}

juce::Rectangle<int> FormSection::getRowBounds (int row) const
{
    auto y = getY() + (title.isEmpty() ? 6 : titleHeight);

    for (int i = 0; i < row && i < (int) rows.size(); ++i)
        y += rows[(size_t) i];

    const auto height = juce::isPositiveAndBelow (row, (int) rows.size()) ? rows[(size_t) row] : rowHeight;
    return { getX() + rowPadding, y, getWidth() - 2 * rowPadding, height };
}

void FormSection::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds().toFloat(), title, getLookAndFeel());

    // Gestrichelte Linien zwischen den Zeilen
    g.setColour (findColour (theme::separator));
    auto y = (title.isEmpty() ? 6 : titleHeight);

    for (size_t i = 0; i + 1 < rows.size(); ++i)
    {
        y += rows[i];

        for (auto x = 4; x < getWidth() - 4; x += 8)
            g.fillRect (x, y - 1, juce::jmin (5, getWidth() - 4 - x), 2);
    }
}

//==============================================================================
SegmentedControl::SegmentedControl (juce::StringArray itemsToUse, int itemHeight, int itemPadding)
    : items (std::move (itemsToUse)), height (itemHeight), padding (itemPadding)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
}

void SegmentedControl::setItems (juce::StringArray newItems)
{
    items = std::move (newItems);
    selected = juce::jlimit (0, juce::jmax (0, items.size() - 1), selected);
    repaint();
}

void SegmentedControl::setFlowMarkers (bool shouldShow)
{
    showFlowMarkers = shouldShow;
    repaint();
}

void SegmentedControl::setSelectedIndex (int index, juce::NotificationType notification)
{
    index = juce::jlimit (0, juce::jmax (0, items.size() - 1), index);

    if (index == selected)
        return;

    selected = index;
    repaint();

    if (notification != juce::dontSendNotification && onChange != nullptr)
        onChange (selected);
}

int SegmentedControl::getItemWidth (int index) const
{
    return juce::GlyphArrangement::getStringWidthInt (theme::getKeyFont (12.0f), items[index].toUpperCase()) + 2 * padding;
}

int SegmentedControl::getPreferredWidth() const
{
    auto total = getGap() * juce::jmax (0, items.size() - 1);

    for (int i = 0; i < items.size(); ++i)
        total += getItemWidth (i);

    return total;
}

juce::Rectangle<float> SegmentedControl::getItemBounds (int index) const
{
    // Tasten mit etwas Abstand; mehr Platz verteilt sich gleichmäßig
    const auto inner = getLocalBounds().toFloat();
    const auto extra = juce::jmax (0.0f, inner.getWidth() - (float) getPreferredWidth()) / (float) juce::jmax (1, items.size());

    auto x = inner.getX();

    for (int i = 0; i < index; ++i)
        x += (float) getItemWidth (i) + extra + (float) getGap();

    return { x, inner.getY(), (float) getItemWidth (index) + extra, inner.getHeight() };
}

void SegmentedControl::paint (juce::Graphics& g)
{
    const auto alpha = isEnabled() ? 1.0f : 0.5f;

    for (int i = 0; i < items.size(); ++i)
    {
        const auto item = getItemBounds (i);
        const auto on = i == selected;

        theme::drawKey (g, item, 7.0f, findColour (on ? (i < (int) activeColourIds.size() ? activeColourIds[(size_t) i] : (int) theme::accent) : (int) theme::control), findColour (theme::ink), on, alpha);

        auto text = item.withTrimmedBottom ((float) theme::keyDepth).toNearestInt();

        if (on)
            text = text.translated (0, 2);

        g.setColour (findColour (on ? theme::onAccent : theme::primaryText).withMultipliedAlpha (alpha));
        g.setFont (theme::getKeyFont (12.0f));
        g.drawText (items[i].toUpperCase(), text, juce::Justification::centred);

        if (showFlowMarkers && i < items.size() - 1)
        {
            const auto next = getItemBounds (i + 1);
            const auto marker = juce::Rectangle<float> (item.getRight(), item.getY(), next.getX() - item.getRight(), item.getHeight() - (float) theme::keyDepth);

            g.setColour (findColour (theme::secondaryText).withMultipliedAlpha (alpha));
            g.drawText (">", marker.toNearestInt(), juce::Justification::centred);
        }
    }

    if (hasKeyboardFocus (false))
    {
        g.setColour (findColour (theme::pink).withAlpha (0.8f));
        g.drawRoundedRectangle (getItemBounds (selected).withTrimmedBottom ((float) theme::keyDepth).expanded (2.5f), 9.0f, 1.5f);
    }
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    for (int i = 0; i < items.size(); ++i)
        if (getItemBounds (i).contains (e.position))
            setSelectedIndex (i);
}

bool SegmentedControl::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::leftKey)
    {
        setSelectedIndex (selected - 1);
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::rightKey)
    {
        setSelectedIndex (selected + 1);
        return true;
    }

    return false;
}

//==============================================================================
StepperSlider::StepperSlider()
{
    setSliderStyle (juce::Slider::IncDecButtons);
    setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_AutoDirection);
}

void StepperSlider::resized()
{
    setTextBoxStyle (juce::Slider::TextBoxLeft, false, juce::jmax (24, getWidth() - buttonsWidth), getHeight() - theme::keyDepth);
    juce::Slider::resized();
}

void StepperSlider::paint (juce::Graphics& g)
{
    // Anzeige auf dem Bildschirm; die Tasten − und + zeichnen sich selbst
    const auto display = juce::Rectangle<float> (0.0f, 0.0f, (float) (getWidth() - buttonsWidth), (float) (getHeight() - theme::keyDepth));
    theme::drawScreen (g, display, 6.0f, findColour (theme::screen), findColour (theme::screenHighlight),
                       findColour (theme::ink), isEnabled() ? 1.0f : 0.45f);
}

void StepperSlider::paintOverChildren (juce::Graphics&) {}

void StepperSlider::lookAndFeelChanged()
{
    // Der Wert steht auf dem Bildschirm
    setColour (juce::Slider::textBoxTextColourId, findColour (theme::phosphor));
    juce::Slider::lookAndFeelChanged();
}

//==============================================================================
void StatusChip::setText (const juce::String& newText, juce::Colour dot)
{
    text = newText;
    dotColour = dot;
    repaint();
}

int StatusChip::getPreferredWidth() const
{
    return 8 + 8 + 8 + juce::GlyphArrangement::getStringWidthInt (theme::getFont (theme::Face::ui, 11.0f, true), text.toUpperCase()) + 8;
}

void StatusChip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().withTrimmedBottom (2.0f);
    const auto ink = findColour (theme::ink);

    g.setColour (ink);
    g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f), 5.0f);
    g.setColour (findColour (theme::chip));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (ink);
    g.drawRoundedRectangle (bounds.reduced (1.0f), 4.0f, 2.0f);

    const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ 16.0f, bounds.getCentreY() });
    g.setColour (dotColour);
    g.fillRect (dot);
    g.setColour (ink);
    g.drawRect (dot, 1.5f);

    g.setColour (juce::Colour (0xff2a2620));
    g.setFont (theme::getFont (theme::Face::ui, 11.0f, true));
    g.drawText (text.toUpperCase(), bounds.withTrimmedLeft (28.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft);
}

//==============================================================================
void PathField::setPath (const juce::String& fullPath)
{
    path = fullPath;
    repaint();
}

void PathField::paint (juce::Graphics& g)
{
    theme::drawScreen (g, getLocalBounds().toFloat(), 6.0f, findColour (theme::screen), findColour (theme::screenHighlight),
                       findColour (theme::ink));

    auto area = getLocalBounds().reduced (10, 0);
    const auto font = theme::getFont (theme::Face::screen, 20.0f);
    const auto phosphor = findColour (theme::phosphor);
    const juce::String prompt = "C:\\> ";
    const auto promptWidth = juce::GlyphArrangement::getStringWidthInt (font, prompt);
    theme::drawGlowText (g, prompt, area.removeFromLeft (promptWidth), juce::Justification::centredLeft, phosphor.withAlpha (0.55f), font);

    const auto last = path.contains ("/") ? path.fromLastOccurrenceOf ("/", false, false) : path;
    const auto prefix = path.substring (0, path.length() - last.length());
    const auto lastWidth = juce::GlyphArrangement::getStringWidthInt (font, last);

    // Der letzte Ordnername bleibt sichtbar, der Anfang des Pfades wird gekürzt
    const auto prefixArea = area.withTrimmedRight (lastWidth);
    g.setFont (font);
    g.setColour (phosphor.withAlpha (0.6f));
    g.drawText (prefix, prefixArea, juce::Justification::centredRight, true);
    theme::drawGlowText (g, last, area.withLeft (juce::jmax (area.getX(), prefixArea.getX() + juce::GlyphArrangement::getStringWidthInt (font, prefix))),
                         juce::Justification::centredLeft, phosphor, font);
}

//==============================================================================
PianoKeyboard::PianoKeyboard (juce::MidiKeyboardState& state, Orientation orientation)
    : juce::MidiKeyboardComponent (state, orientation)
{
    setBlackNoteLengthProportion (0.6f);
}

void PianoKeyboard::setMarks (std::map<int, Mark> newMarks)
{
    if (newMarks == marks)
        return;

    marks = std::move (newMarks);
    repaint();
}

PianoKeyboard::Mark PianoKeyboard::getMark (int midiNoteNumber) const
{
    const auto found = marks.find (midiNoteNumber);
    return found != marks.end() ? found->second : Mark::none;
}

void PianoKeyboard::drawMark (juce::Graphics& g, juce::Point<float> centre, float diameter, Mark mark) const
{
    if (mark == Mark::none)
        return;

    // Quadratische Markierung mit Rand wie in der Vorlage
    const auto area = juce::Rectangle<float> (diameter, diameter).withCentre (centre);

    switch (mark)
    {
        case Mark::done:     g.setColour (findColour (theme::accent)); g.fillRect (area); break;
        case Mark::running:
        case Mark::selected: g.setColour (findColour (theme::pink)); g.fillRect (area); break;
        case Mark::open:     break;
        case Mark::none:     break;
    }

    g.setColour (mark == Mark::open ? findColour (theme::pending) : findColour (theme::keyBorder));
    g.drawRect (area, 2.0f);
}

void PianoKeyboard::drawWhiteNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                                   bool isDown, bool, juce::Colour, juce::Colour)
{
    const auto mark = getMark (midiNoteNumber);
    auto fill = findColour (theme::keyWhite);

    if (mark == Mark::selected)
        fill = fill.interpolatedWith (findColour (theme::pink), 0.2f);

    g.setColour (fill);
    g.fillRect (area);

    // Schattenkante unten
    g.setColour (juce::Colours::black.withAlpha (0.08f));
    g.fillRect (area.withTop (area.getBottom() - 6.0f));

    if (isDown)
    {
        g.setColour (findColour (theme::accent).withAlpha (0.4f));
        g.fillRect (area);
    }

    g.setColour (findColour (theme::keyBorder));
    g.fillRect (area.withLeft (area.getRight() - 2.0f));

    const auto centreX = area.getCentreX() - 1.0f;
    drawMark (g, { centreX, area.getBottom() - 6.0f - 11.0f - 6.0f - 4.0f }, 8.0f, mark);

    if (midiNoteNumber % 12 == 0)
    {
        g.setColour (findColour (theme::keyLabel));
        g.setFont (theme::getFont (theme::Face::ui, 9.0f, true));
        g.drawText (juce::MidiMessage::getMidiNoteName (midiNoteNumber, true, true, 3),
                    area.withTrimmedBottom (6.0f).removeFromBottom (11.0f), juce::Justification::centred);
    }
}

void PianoKeyboard::drawBlackNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                                   bool isDown, bool, juce::Colour)
{
    const auto mark = getMark (midiNoteNumber);
    auto fill = findColour (theme::keyBlack);

    if (mark == Mark::selected)
        fill = fill.brighter (0.2f);

    juce::Path key;
    key.addRoundedRectangle (area.getX(), area.getY(), area.getWidth(), area.getHeight(), 4.0f, 4.0f, false, false, true, true);
    g.setColour (fill);
    g.fillPath (key);

    // Lichtkante unten
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillRect (area.withTrimmedLeft (1.0f).withTrimmedRight (1.0f).withTop (area.getBottom() - 5.0f).withTrimmedBottom (1.0f));

    if (isDown)
    {
        g.setColour (findColour (theme::accent).withAlpha (0.6f));
        g.fillPath (key);
    }

    drawMark (g, { area.getCentreX(), area.getBottom() - 7.0f - 3.5f }, 7.0f, mark);
}

} // namespace autosampler
