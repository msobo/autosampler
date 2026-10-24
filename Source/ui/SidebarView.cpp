#include "SidebarView.h"

namespace autosampler
{

namespace
{
    // Monitor der Vorlage: Rahmen (Bezel) um einen Bildschirm in Phosphor
    constexpr int outerMargin = 14, bezelPadding = 12, screenPadding = 16, bezelFooter = 22, lineHeight = 22,
                  titleHeight = 34, subtitleHeight = 22, headingHeight = 24, labelWidth = 34;

    juce::Font screenFont (float cssSize)
    {
        return theme::getFont (theme::Face::screen, cssSize);
    }

    juce::Font uiFont (float cssSize, bool bold = false)
    {
        return theme::getFont (theme::Face::ui, cssSize, bold);
    }

    juce::String upper (const juce::String& text)
    {
        return text.toUpperCase();
    }

    /** Zeile "BEZEICHNUNG ..... Wert" mit gepunkteter Linie dazwischen. */
    void drawDottedRow (juce::Graphics& g, juce::Rectangle<int> row, const juce::String& label, const juce::String& value,
                        juce::Colour phosphor)
    {
        const auto font = screenFont (19.0f);
        const auto labelW = juce::GlyphArrangement::getStringWidthInt (font, label);
        const auto valueW = juce::GlyphArrangement::getStringWidthInt (font, value);

        theme::drawGlowText (g, label, row.withWidth (labelW), juce::Justification::centredLeft, phosphor, font);
        theme::drawGlowText (g, value, row.withLeft (row.getRight() - valueW), juce::Justification::centredRight, phosphor, font);

        g.setColour (phosphor.withAlpha (0.35f));

        for (auto x = row.getX() + labelW + 8; x < row.getRight() - valueW - 8; x += 4)
            g.fillRect (x, row.getBottom() - 6, 2, 2);
    }
}

SidebarView::SidebarView()
{
    addChildComponent (sampleList);
    sampleList.setRowHeight (26);
    sampleList.setOutlineThickness (0);
    sampleList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    sampleList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    sampleList.getVerticalScrollBar().setAutoHide (true);
    setOpaque (true);
}

SidebarView::~SidebarView() = default;

void SidebarView::setPluginInfo (PluginInfo info)
{
    pluginInfo = std::move (info);
    resized();
    repaint();
}

void SidebarView::setContent (Content newContent)
{
    if (content == newContent)
        return;

    content = newContent;
    sampleList.setVisible (content == Content::sampleList);
    resized();
    repaint();
}

void SidebarView::setRunOverview (RunOverview newOverview)
{
    overview = std::move (newOverview);
    resized();
    repaint();
}

void SidebarView::setSamples (std::vector<SampleItem> items)
{
    samples = std::move (items);
    sampleList.updateContent();
    repaint();
}

void SidebarView::setSelectedSample (int index)
{
    const juce::ScopedValueSetter<bool> guard (updatingSelection, true);

    if (juce::isPositiveAndBelow (index, (int) samples.size()))
    {
        sampleList.selectRow (index, false, true);
        sampleList.scrollToEnsureRowIsOnscreen (index);
    }
    else
    {
        sampleList.deselectAllRows();
    }
}

//==============================================================================
int SidebarView::getInfoHeight() const
{
    return (int) pluginInfo.rows.size() * lineHeight;
}

SidebarView::RunGrid SidebarView::getRunGrid (int innerWidth) const
{
    RunGrid grid;

    if (overview.cells.empty() || overview.cells.front().empty())
        return grid;

    // Eine Zeile je Velocity, eine Spalte je Note (bei vielen Noten zusammengefasst)
    constexpr int minCell = 8, maxCell = 12, gap = 3;
    const auto numNotes = (int) overview.cells.front().size();
    const auto inner = innerWidth - labelWidth - 6;
    const auto maxColumns = juce::jmax (1, (inner + gap) / (minCell + gap));

    grid.numRows = (int) overview.cells.size();
    grid.binSize = (numNotes + maxColumns - 1) / maxColumns;
    grid.numColumns = (numNotes + grid.binSize - 1) / grid.binSize;
    grid.cell = juce::jlimit (minCell, maxCell, (inner + gap) / juce::jmax (1, grid.numColumns) - gap);
    grid.gap = gap;
    grid.height = grid.numRows * (grid.cell + 5) + 4;
    return grid;
}

SidebarView::Layout SidebarView::getLayout() const
{
    Layout layout;
    const auto available = getLocalBounds().reduced (outerMargin).withTrimmedBottom (6);
    const auto innerWidth = available.getWidth() - 2 * bezelPadding - 2 * screenPadding;

    // Inhalt des Bildschirms von oben: Name, Hersteller, Daten
    auto contentHeight = screenPadding + titleHeight + subtitleHeight + 8 + getInfoHeight();

    if (content == Content::runOverview)
    {
        contentHeight += 16 + headingHeight + 4;
        const auto grid = getRunGrid (innerWidth);
        contentHeight += grid.height + (grid.height > 0 ? 10 + 20 : 0);
        contentHeight += 18;
    }
    else if (content == Content::sampleList)
    {
        contentHeight += 16 + headingHeight + 4;
    }
    else
    {
        contentHeight += 18;
    }

    const auto bezelChrome = 2 * bezelPadding + bezelFooter;
    auto screenHeight = contentHeight;

    // Mit der Sample-Liste füllt der Monitor die Seitenleiste
    if (content == Content::sampleList)
        screenHeight = juce::jmax (contentHeight + 80, available.getHeight() - bezelChrome);

    screenHeight = juce::jmin (screenHeight, available.getHeight() - bezelChrome);

    layout.bezel = available.withHeight (screenHeight + bezelChrome);
    layout.screen = layout.bezel.reduced (bezelPadding, bezelPadding).withHeight (screenHeight);
    layout.listArea = layout.screen.withTrimmedTop (contentHeight - 4).reduced (6, 0).withTrimmedBottom (8);
    layout.description = juce::Rectangle<int> (available.getX() + 4, layout.bezel.getBottom() + panelShadow + 12,
                                               available.getWidth() - 8, 80);
    return layout;
}

void SidebarView::resized()
{
    sampleList.setBounds (getLayout().listArea);
}

//==============================================================================
void SidebarView::paint (juce::Graphics& g)
{
    g.fillAll (findColour (theme::sidebar));

    const auto layout = getLayout();
    const auto ink = findColour (theme::ink);
    const auto phosphor = findColour (theme::phosphor);

    // Rahmen des Monitors mit hartem Schatten
    const auto bezel = layout.bezel.toFloat();
    g.setColour (ink);
    g.fillRoundedRectangle (bezel.translated (0.0f, (float) panelShadow), 18.0f);
    g.setColour (findColour (theme::bezel));
    g.fillRoundedRectangle (bezel, 18.0f);
    g.setColour (findColour (theme::caseHighlight));
    g.fillRect (bezel.reduced (14.0f, 0.0f).withHeight (2.0f).translated (0.0f, 3.0f));
    g.setColour (ink);
    g.drawRoundedRectangle (bezel.reduced (1.5f), 16.5f, 3.0f);

    const auto screen = layout.screen.toFloat();
    theme::drawScreen (g, screen, 14.0f, findColour (theme::screen), findColour (theme::screenHighlight), ink);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (layout.screen);
        paintScreen (g, layout, phosphor);

        // Spiegelung oben links
        juce::Path clip;
        clip.addRoundedRectangle (screen, 14.0f);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), screen.getX() + screen.getWidth() * 0.3f,
                                                 screen.getY() + screen.getHeight() * 0.15f, juce::Colours::transparentWhite,
                                                 screen.getX() + screen.getWidth() * 0.3f + screen.getWidth() * 0.45f,
                                                 screen.getY() + screen.getHeight() * 0.15f + screen.getWidth() * 0.3f, true));
        g.fillRoundedRectangle (screen, 14.0f);
    }

    // Beschriftung unter dem Bildschirm
    const auto footer = layout.bezel.withTop (layout.screen.getBottom() + 6).withTrimmedBottom (4).reduced (bezelPadding + 6, 0);
    g.setColour (findColour (theme::secondaryText));
    g.setFont (uiFont (10.0f, true).withExtraKerningFactor (0.12f));
    g.drawText ("SYS-MON 01", footer, juce::Justification::centredLeft);
    g.drawText ("POWER", footer.withTrimmedRight (16), juce::Justification::centredRight);

    const auto led = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) footer.getRight() - 4.0f, (float) footer.getCentreY() });
    g.setColour (findColour (theme::led).withAlpha (0.35f));
    g.fillEllipse (led.expanded (3.0f));
    g.setColour (findColour (theme::led));
    g.fillEllipse (led);
    g.setColour (ink);
    g.drawEllipse (led.reduced (0.75f), 1.5f);

    // Beschreibung des Laufs unter dem Monitor
    if (content == Content::runOverview)
    {
        const auto hasError = overview.error.isNotEmpty();
        juce::AttributedString text;
        text.append (hasError ? overview.error : overview.description, uiFont (12.0f),
                     findColour (hasError ? theme::error : theme::secondaryText));
        text.setWordWrap (juce::AttributedString::byWord);
        text.draw (g, layout.description.toFloat());
    }
}

void SidebarView::paintScreen (juce::Graphics& g, const Layout& layout, juce::Colour phosphor)
{
    auto area = layout.screen.reduced (screenPadding, 0).withTrimmedTop (screenPadding);

    // Plugin: Name groß, darunter der Hersteller
    theme::drawGlowText (g, upper (pluginInfo.name.isNotEmpty() ? pluginInfo.name : juce::String ("No instrument")),
                         area.removeFromTop (titleHeight), juce::Justification::centredLeft, phosphor, screenFont (26.0f));
    theme::drawGlowText (g, upper (pluginInfo.subtitle), area.removeFromTop (subtitleHeight), juce::Justification::centredLeft,
                         phosphor, screenFont (19.0f));
    area.removeFromTop (8);

    for (const auto& [label, value] : pluginInfo.rows)
        drawDottedRow (g, area.removeFromTop (lineHeight), upper (label), upper (value), phosphor);

    if (content == Content::runOverview)
    {
        area.removeFromTop (16);
        theme::drawGlowText (g, "> RUN MAP \xc2\xb7 "_u + juce::String (overview.numSamples) + " SMP", area.removeFromTop (headingHeight),
                             juce::Justification::centredLeft, phosphor, screenFont (19.0f));
        area.removeFromTop (4);
        paintRunGrid (g, area, phosphor);
    }
    else if (content == Content::sampleList)
    {
        area.removeFromTop (16);
        const auto header = area.removeFromTop (headingHeight);
        theme::drawGlowText (g, "> SAMPLES", header, juce::Justification::centredLeft, phosphor, screenFont (19.0f));

        const auto numLoops = (int) std::count_if (samples.begin(), samples.end(), [] (const auto& s) { return s.loop; });
        g.setColour (phosphor.withAlpha (0.7f));
        g.setFont (screenFont (17.0f));
        g.drawText (juce::String (numLoops) + "/" + juce::String ((int) samples.size()) + " LOOP", header, juce::Justification::centredRight);

        if (samples.empty())
        {
            g.setColour (phosphor.withAlpha (0.55f));
            g.setFont (screenFont (18.0f));
            g.drawText ("NO SAMPLES YET", area.withHeight (24), juce::Justification::centredLeft);
        }
    }
}

void SidebarView::paintRunGrid (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour phosphor)
{
    const auto grid = getRunGrid (area.getWidth());

    if (grid.numRows == 0)
        return;

    const auto numNotes = (int) overview.cells.front().size();
    auto y = area.getY();

    for (int row = 0; row < grid.numRows; ++row)
    {
        g.setColour (phosphor);
        g.setFont (screenFont (17.0f));
        g.drawText (row < (int) overview.rowLabels.size() ? upper (overview.rowLabels[(size_t) row]) : juce::String(),
                    area.getX(), y, labelWidth, grid.cell, juce::Justification::centredLeft);

        for (int column = 0; column < grid.numColumns; ++column)
        {
            // Zusammengefasst: läuft, wenn eine läuft; fertig, wenn alle fertig sind
            auto state = Cell::done;
            bool anyRunning = false;

            for (int n = column * grid.binSize; n < juce::jmin (numNotes, (column + 1) * grid.binSize); ++n)
            {
                const auto s = overview.cells[(size_t) row][(size_t) n];
                anyRunning = anyRunning || s == Cell::running;

                if (s != Cell::done)
                    state = Cell::open;
            }

            if (anyRunning)
                state = Cell::running;

            const auto rect = juce::Rectangle<float> ((float) (area.getX() + labelWidth + 6 + column * (grid.cell + grid.gap)),
                                                      (float) y, (float) grid.cell, (float) grid.cell);
            drawCell (g, rect, state, phosphor);
        }

        y += grid.cell + 5;
    }

    // Legende
    y += 10;
    auto x = area.getX();
    g.setFont (screenFont (16.0f));

    const auto legend = [&] (const juce::String& label, Cell state)
    {
        drawCell (g, juce::Rectangle<float> ((float) x, (float) y + 5.0f, 8.0f, 8.0f), state, phosphor);
        const auto width = juce::GlyphArrangement::getStringWidthInt (screenFont (16.0f), label);
        g.setColour (state == Cell::running ? findColour (theme::pink) : phosphor);
        g.drawText (label, x + 13, y, width + 2, 18, juce::Justification::centredLeft);
        x += 13 + width + 12;
    };

    legend ("DONE", Cell::done);
    legend ("RUNNING", Cell::running);
    legend ("OPEN", Cell::open);
}

void SidebarView::drawCell (juce::Graphics& g, juce::Rectangle<float> rect, Cell state, juce::Colour phosphor) const
{
    const auto colour = state == Cell::running ? findColour (theme::pink) : phosphor;

    if (state != Cell::open)
    {
        g.setColour (colour.withAlpha (0.3f));
        g.fillRect (rect.expanded (1.5f));
        g.setColour (colour);
        g.fillRect (rect);
    }
    else
    {
        g.setColour (phosphor.withAlpha (0.35f));
        g.drawRect (rect, 2.0f);
    }
}

//==============================================================================
void SidebarView::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) samples.size()))
        return;

    const auto& item = samples[(size_t) row];
    const auto phosphor = findColour (theme::phosphor);
    const auto bounds = juce::Rectangle<int> (0, 0, width, height);

    if (selected)
    {
        g.setColour (phosphor.withAlpha (0.2f));
        g.fillRect (bounds);
        g.setColour (findColour (theme::pink));
        g.fillRect (bounds.withWidth (3));
    }

    auto area = bounds.reduced (8, 0);
    const auto dim = phosphor.withAlpha (0.65f);

    theme::drawGlowText (g, upper (item.note), area.removeFromLeft (56), juce::Justification::centredLeft, phosphor, screenFont (20.0f));

    // Badge rechts: LOOP oder Strich
    auto badge = area.removeFromRight (item.loop ? 46 : 16);
    g.setFont (screenFont (16.0f));

    if (item.loop)
    {
        g.setColour (phosphor);
        g.drawRect (badge.toFloat().withSizeKeepingCentre ((float) badge.getWidth(), 16.0f), 1.5f);
        g.drawText ("LOOP", badge, juce::Justification::centred);
    }
    else
    {
        g.setColour (dim);
        g.drawText ("-", badge, juce::Justification::centred);
    }

    area.removeFromRight (6);

    if (item.locked)
    {
        auto lock = area.removeFromRight (12).toFloat().withSizeKeepingCentre (11.0f, 12.0f);
        theme::drawIcon (g, theme::Icon::lock, lock, findColour (theme::yellow));
        g.setColour (findColour (theme::yellow));
        g.fillRoundedRectangle (lock.withTrimmedTop (5.5f).reduced (1.5f, 0.0f), 1.2f);
        area.removeFromRight (6);
    }

    g.setColour (dim);
    g.setFont (screenFont (19.0f));
    g.drawText (upper (item.velocity), area, juce::Justification::centredLeft);
}

void SidebarView::selectedRowsChanged (int lastRowSelected)
{
    if (! updatingSelection && onSampleSelected != nullptr && lastRowSelected >= 0)
        onSampleSelected (lastRowSelected);
}

} // namespace autosampler
