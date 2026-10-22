#include "EditorView.h"
#include "Text.h"
#include "Theme.h"

namespace autosampler
{

namespace
{
    constexpr LoopAlgorithm algorithms[] { LoopAlgorithm::pad, LoopAlgorithm::universal, LoopAlgorithm::tonal };
    constexpr CrossfadeMode crossfadeModes[] { CrossfadeMode::linear, CrossfadeMode::equalPower,
                                               CrossfadeMode::sCurve, CrossfadeMode::spectral };

    template <typename Array, typename Value>
    int indexOf (const Array& array, Value value)
    {
        return (int) std::distance (std::begin (array), std::find (std::begin (array), std::end (array), value));
    }
}

EditorView::EditorView (Session& sessionToUse)
    : session (sessionToUse),
      sessionState (session.getState()),
      loopBatch (sessionToUse)
{
    formatManager.registerBasicFormats();

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &countLabel, &unsavedChip, &sizeLabel, &sizeSlider,
                                                             &undoButton, &redoButton, &loopSection })
        content.addAndMakeVisible (c);

    titleLabel.setFont (juce::Font (juce::FontOptions (theme::fontSize (20.0f))).boldened());
    theme::bindColour (countLabel, juce::Label::textColourId, theme::secondaryText);
    theme::bindColour (sizeLabel, juce::Label::textColourId, theme::secondaryText);

    theme::setIcon (undoButton, theme::Icon::undo);
    theme::setIcon (redoButton, theme::Icon::redo);
    undoButton.onClick = [this] { session.getUndoManager().undo(); };
    redoButton.onClick = [this] { session.getUndoManager().redo(); };

    sizeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sizeSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    sizeSlider.setRange (80.0, 200.0, 10.0);
    sizeSlider.setValue (120.0, juce::dontSendNotification);
    sizeSlider.onValueChange = [this] { layoutPage(); };

    for (auto* c : std::initializer_list<juce::Component*> { &algorithmLabel, &algorithmBox, &blendLabel, &blendBox,
                                                             &crossfadeLabel, &crossfadeSlider, &padLabel, &padSlider,
                                                             &searchAllButton, &disableAllButton, &searchStatus })
        content.addAndMakeVisible (c);

    for (auto* label : { &algorithmLabel, &blendLabel, &crossfadeLabel, &padLabel })
        theme::bindColour (*label, juce::Label::textColourId, theme::secondaryText);

    theme::setPrimary (searchAllButton, true);
    theme::setIcon (searchAllButton, theme::Icon::search);

    for (size_t i = 0; i < std::size (algorithms); ++i)
        algorithmBox.addItem (getDisplayName (algorithms[i]), (int) i + 1);

    for (size_t i = 0; i < std::size (crossfadeModes); ++i)
        blendBox.addItem (getDisplayName (crossfadeModes[i]), (int) i + 1);

    algorithmBox.setTooltip ("Pad: pads and evolving sounds. Tonal: static tones with a clear fundamental. Universal: everything without a clear fundamental."_u);
    blendBox.setTooltip ("Shape of the crossfade before E. Spectral blends per frequency band and matches the level by correlation."_u);
    crossfadeSlider.setTooltip ("Crossfade length (Universal; Tonal at most two periods; Pad at least this long)"_u);
    padSlider.setTooltip ("Pad: crossfade as a fraction of half the loop length"_u);
    searchAllButton.setTooltip ("Searches loops for all unlocked samples (one undo step)"_u);
    disableAllButton.setTooltip ("Turns off the loops of all unlocked samples (one undo step)"_u);
    disableAllButton.onClick = [this] { disableAllLoops(); };
    sizeSlider.setTooltip ("Height of the sample displays in the editor"_u);
    titleLabel.setTooltip ("Session, number of samples and whether there are unsaved changes"_u);

    crossfadeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    crossfadeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    crossfadeSlider.setRange (0.0, 1000.0, 1.0);
    crossfadeSlider.setSkewFactorFromMidPoint (100.0);
    crossfadeSlider.setTextValueSuffix (" ms");

    padSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    padSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    padSlider.setRange (0.0, 100.0, 1.0);
    padSlider.setTextValueSuffix (" %");

    algorithmBox.onChange = [this] { storeLoopSettings(); };
    blendBox.onChange = [this] { storeLoopSettings(); };
    crossfadeSlider.onDragEnd = [this] { storeLoopSettings(); };
    crossfadeSlider.onValueChange = [this] { if (! crossfadeSlider.isMouseButtonDown()) storeLoopSettings(); };
    padSlider.onDragEnd = [this] { storeLoopSettings(); };
    padSlider.onValueChange = [this] { if (! padSlider.isMouseButtonDown()) storeLoopSettings(); };

    searchAllButton.onClick = [this]
    {
        if (loopBatch.isSearching())
        {
            loopBatch.cancel();
            lastSearchMessage = "Search cancelled";
            updateSearchStatus();
            return;
        }

        findAllLoops();
    };

    theme::bindColour (searchStatus, juce::Label::textColourId, theme::secondaryText);
    searchStatus.setMinimumHorizontalScale (0.7f);

    statusBar.saveButton.onClick = [this] { if (onSave != nullptr) onSave(); };
    statusBar.continueButton.onClick = [this] { if (onContinueToExport != nullptr) onContinueToExport(); };

    loopBatch.onProgress = [this] { updateSearchStatus(); };
    loopBatch.onFinished = [this] (int numApplied, int numFailed, const juce::String& message)
    {
        lastSearchMessage = juce::String (numApplied) + (numApplied == 1 ? " loop found" : " loops found");

        if (numFailed > 0)
            lastSearchMessage << ", " << numFailed << " without result" << (message.isNotEmpty() ? ": " + message : juce::String());

        updateSearchStatus();
    };

    sessionState.addListener (this);
    session.addChangeListener (this);
    session.getUndoManager().addChangeListener (this);

    updateCards();
    updateHeader();
    updateLoopControls();
    updateSearchStatus();
}

EditorView::~EditorView()
{
    session.getUndoManager().removeChangeListener (this);
    session.removeChangeListener (this);
    sessionState.removeListener (this);
}

void EditorView::setAudition (SampleAudition* newAudition)
{
    audition = newAudition;

    for (auto& card : cards)
        card->setAudition (audition);
}

void EditorView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateHeader();
    updateLoopControls();
}

//==============================================================================
void EditorView::updateLoopControls()
{
    const auto settings = session.getLoopSearchSettings();
    algorithmBox.setSelectedId (indexOf (algorithms, settings.algorithm) + 1, juce::dontSendNotification);
    blendBox.setSelectedId (indexOf (crossfadeModes, settings.crossfadeMode) + 1, juce::dontSendNotification);

    if (! crossfadeSlider.isMouseButtonDown())
        crossfadeSlider.setValue (settings.crossfadeSeconds * 1000.0, juce::dontSendNotification);

    if (! padSlider.isMouseButtonDown())
        padSlider.setValue (settings.padAmount * 100.0, juce::dontSendNotification);

    padSlider.setEnabled (settings.algorithm == LoopAlgorithm::pad);
    padLabel.setEnabled (padSlider.isEnabled());
}

void EditorView::storeLoopSettings()
{
    auto settings = session.getLoopSearchSettings();

    if (const auto index = algorithmBox.getSelectedId() - 1; juce::isPositiveAndBelow (index, (int) std::size (algorithms)))
        settings.algorithm = algorithms[index];

    if (const auto index = blendBox.getSelectedId() - 1; juce::isPositiveAndBelow (index, (int) std::size (crossfadeModes)))
        settings.crossfadeMode = crossfadeModes[index];

    settings.crossfadeSeconds = crossfadeSlider.getValue() / 1000.0;
    settings.padAmount = (float) (padSlider.getValue() / 100.0);
    session.setLoopSearchSettings (settings);
    updateLoopControls();
}

void EditorView::findAllLoops()
{
    std::vector<juce::ValueTree> samples;

    for (const auto& sampleTree : session.getSamplesTree())
        samples.push_back (sampleTree);

    findLoops (samples);
}

void EditorView::disableAllLoops()
{
    loopBatch.cancel();
    session.getUndoManager().beginNewTransaction ("All loops off");

    for (const auto& sampleTree : session.getSamplesTree())
    {
        auto sample = session.wrap (sampleTree);

        if (! sample.isLocked() && sample.isLoopEnabled())
            sample.setLoopEnabled (false);
    }

    lastSearchMessage = "All loops turned off";
    updateSearchStatus();
}

void EditorView::findLoops (const std::vector<juce::ValueTree>& samples)
{
    lastSearchMessage.clear();
    loopBatch.search (samples, session.getLoopSearchSettings());
}

void EditorView::updateSearchStatus()
{
    const auto searching = loopBatch.isSearching();
    searchAllButton.setButtonText (searching ? "Cancel" : "Find all loops");
    searchAllButton.setEnabled (searching || session.getNumSamples() > 0);
    disableAllButton.setEnabled (session.getNumSamples() > 0);

    searchStatus.setText (searching ? "Searching " + juce::String (loopBatch.getNumDone()) + " / " + juce::String (loopBatch.getNumTotal())
                          : lastSearchMessage.isNotEmpty() ? lastSearchMessage
                                                           : "Locked samples are skipped during the search."_u,
                          juce::dontSendNotification);
}

void EditorView::handleAsyncUpdate()
{
    updateCards();
    updateHeader();
    updateSearchStatus();
}

//==============================================================================
void EditorView::updateCards()
{
    const auto samples = session.getSamplesTree();

    // Bestehende Karten wiederverwenden, damit Wellenformen nicht neu geladen werden
    std::vector<std::unique_ptr<SampleCard>> newCards;

    for (const auto& sampleTree : samples)
    {
        auto existing = std::find_if (cards.begin(), cards.end(), [&] (const auto& card)
        {
            return card != nullptr && card->getSampleTree() == sampleTree;
        });

        if (existing != cards.end())
        {
            newCards.push_back (std::move (*existing));
        }
        else
        {
            newCards.push_back (std::make_unique<SampleCard> (session, sampleTree, formatManager, thumbnailCache));
            newCards.back()->onFindLoop = [this] (const juce::ValueTree& tree) { findLoops ({ tree }); };
            newCards.back()->setAudition (audition);
            newCards.back()->onSelected = [this] (SampleCard* card)
            {
                for (size_t i = 0; i < cards.size(); ++i)
                    if (cards[i].get() == card)
                        setSelectedIndex ((int) i);
            };
            content.addAndMakeVisible (*newCards.back());
        }
    }

    cards = std::move (newCards);

    if (selectedIndex >= (int) cards.size())
        selectedIndex = -1;

    for (size_t i = 0; i < cards.size(); ++i)
        cards[i]->setSelected ((int) i == selectedIndex);

    layoutPage();
    repaint();
}

void EditorView::setSelectedIndex (int index)
{
    index = juce::isPositiveAndBelow (index, (int) cards.size()) ? index : -1;

    if (index == selectedIndex)
        return;

    selectedIndex = index;

    for (size_t i = 0; i < cards.size(); ++i)
        cards[i]->setSelected ((int) i == selectedIndex);

    if (index >= 0)
    {
        // Karte ins Bild holen
        const auto bounds = cards[(size_t) index]->getBounds();
        const auto visible = viewport.getViewArea();

        if (bounds.getY() < visible.getY() || bounds.getBottom() > visible.getBottom())
            viewport.setViewPosition (0, juce::jmax (0, bounds.getY() - 16));
    }

    if (onSelectionChanged != nullptr)
        onSelectionChanged (selectedIndex);
}

void EditorView::updateHeader()
{
    const auto numSamples = session.getNumSamples();
    titleLabel.setText (session.getName().isNotEmpty() ? session.getName() : juce::String ("New session"), juce::dontSendNotification);
    countLabel.setText (juce::String (numSamples) + (numSamples == 1 ? " sample" : " samples"), juce::dontSendNotification);
    unsavedChip.setText ("Unsaved", findColour (theme::running));
    unsavedChip.setVisible (session.hasUnsavedChanges());

    auto& undoManager = session.getUndoManager();
    undoButton.setEnabled (undoManager.canUndo());
    redoButton.setEnabled (undoManager.canRedo());
    undoButton.setTooltip (undoManager.canUndo() ? "Undo: "_u + undoManager.getUndoDescription() + " (⌘Z)"_u : juce::String());
    redoButton.setTooltip (undoManager.canRedo() ? "Redo: " + undoManager.getRedoDescription() + " (⇧⌘Z)"_u : juce::String());

    int numLoops = 0;

    for (const auto& tree : session.getSamplesTree())
        if (session.wrap (tree).isLoopEnabled())
            ++numLoops;

    statusBar.hasSamples = numSamples > 0;
    statusBar.titleLabel.setText (numSamples > 0 ? "Samples ready"_u : "No samples", juce::dontSendNotification);
    statusBar.detailLabel.setText (numSamples > 0 ? juce::String (numSamples) + (numSamples == 1 ? " sample" : " samples") + " · "_u
                                                        + juce::String (numLoops) + " with loop"
                                                  : "Start a run under Autosample or open a session"_u,
                                   juce::dontSendNotification);
    statusBar.resized();
    statusBar.saveButton.setEnabled (numSamples > 0);
    statusBar.continueButton.setEnabled (numSamples > 0);
    statusBar.repaint();
    layoutPage();
}

//==============================================================================
void EditorView::paint (juce::Graphics& g)
{
    g.fillAll (findColour (theme::canvas));

    if (! cards.empty())
        return;

    g.setColour (findColour (theme::tertiaryText));
    g.setFont (juce::FontOptions (theme::fontSize (13.0f)));
    g.drawFittedText ("No samples yet.\nStart a run under Autosample or open a session (File > Open)."_u,
                      getLocalBounds().withTrimmedTop (220).reduced (20), juce::Justification::centredTop, 3);
}

void EditorView::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutPage();
}

int EditorView::layoutLoopSection (int x, int y, int width)
{
    struct Item { juce::Component* label; juce::Component* control; int labelWidth, controlWidth; };

    const auto measure = [] (const juce::Label& label)
    {
        return juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (theme::fontSize (13.0f))), label.getText()) + 12;
    };

    const Item items[] { { &algorithmLabel, &algorithmBox, measure (algorithmLabel), 104 },
                         { &blendLabel, &blendBox, measure (blendLabel), 132 },
                         { &crossfadeLabel, &crossfadeSlider, measure (crossfadeLabel), 196 },
                         { &padLabel, &padSlider, measure (padLabel), 176 } };

    constexpr int itemGapX = 28, itemGapY = 12, controlHeight = 28, innerPadding = 14;
    const auto available = width - 2 * innerPadding;

    // Zeilenumbruch wie flex-wrap
    std::vector<int> lineOf;
    int line = 0, lineWidth = 0;

    for (const auto& item : items)
    {
        const auto itemWidth = item.labelWidth + 10 + item.controlWidth;

        if (lineWidth > 0 && lineWidth + itemGapX + itemWidth > available)
        {
            ++line;
            lineWidth = 0;
        }

        lineWidth += (lineWidth > 0 ? itemGapX : 0) + itemWidth;
        lineOf.push_back (line);
    }

    const auto numLines = line + 1;
    const auto firstRow = 24 + numLines * controlHeight + (numLines - 1) * itemGapY;
    loopSection.setRows ({ firstRow, 52 });
    loopSection.setBounds (x, y, width, loopSection.getPreferredHeight());

    const auto rowBounds = loopSection.getRowBounds (0);
    int currentLine = -1, cursor = 0;

    for (size_t i = 0; i < std::size (items); ++i)
    {
        if (lineOf[i] != currentLine)
        {
            currentLine = lineOf[i];
            cursor = rowBounds.getX();
        }

        const auto top = rowBounds.getY() + 12 + currentLine * (controlHeight + itemGapY);
        items[i].label->setBounds (cursor, top, items[i].labelWidth, controlHeight);
        cursor += items[i].labelWidth + 10;
        items[i].control->setBounds (cursor, top, items[i].controlWidth, controlHeight);
        cursor += items[i].controlWidth + itemGapX;
    }

    auto buttons = loopSection.getRowBounds (1);
    searchAllButton.setBounds (buttons.removeFromLeft (150).withSizeKeepingCentre (150, controlHeight));
    buttons.removeFromLeft (10);
    disableAllButton.setBounds (buttons.removeFromLeft (130).withSizeKeepingCentre (130, controlHeight));
    buttons.removeFromLeft (10);
    searchStatus.setBounds (buttons.withSizeKeepingCentre (buttons.getWidth(), 20));

    return y + loopSection.getHeight();
}

void EditorView::layoutPage()
{
    constexpr int paddingX = 28, paddingTop = 20, paddingBottom = 28, maxWidth = 1180, pageGap = 18, cardGap = 14,
                  minCardWidth = 340, controlHeight = 28;

    const auto viewportWidth = viewport.getMaximumVisibleWidth();
    const auto innerWidth = juce::jmin (maxWidth, viewportWidth - 2 * paddingX);
    const auto x0 = (viewportWidth - innerWidth) / 2;
    auto y = paddingTop;

    // Titelzeile
    const juce::Font titleFont (juce::FontOptions (theme::fontSize (20.0f)));
    const auto titleWidth = juce::GlyphArrangement::getStringWidthInt (titleFont.boldened(), titleLabel.getText()) + 12;
    const auto countWidth = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (theme::fontSize (13.0f))), countLabel.getText()) + 12;
    const auto chipWidth = unsavedChip.getPreferredWidth();

    auto row = juce::Rectangle<int> (x0, y, innerWidth, controlHeight);
    titleLabel.setBounds (row.removeFromLeft (titleWidth));
    row.removeFromLeft (16);
    countLabel.setBounds (row.removeFromLeft (countWidth));
    row.removeFromLeft (10);
    unsavedChip.setBounds (row.removeFromLeft (chipWidth).withSizeKeepingCentre (chipWidth, 22));

    redoButton.setBounds (row.removeFromRight (34));
    row.removeFromRight (4);
    undoButton.setBounds (row.removeFromRight (34));
    row.removeFromRight (16);
    sizeSlider.setBounds (row.removeFromRight (120));
    row.removeFromRight (10);
    sizeLabel.setBounds (row.removeFromRight (juce::jmin (row.getWidth(), 100)));
    y += controlHeight + pageGap;

    // Loop-Einstellungen
    y = layoutLoopSection (x0, y, innerWidth) + pageGap;

    // Karten
    const auto columns = juce::jmax (1, (innerWidth + cardGap) / (minCardWidth + cardGap));
    const auto cardWidth = (innerWidth - (columns - 1) * cardGap) / columns;
    const auto cardHeight = SampleCard::getHeightForWaveform ((int) sizeSlider.getValue());

    for (size_t i = 0; i < cards.size(); ++i)
    {
        const auto column = (int) i % columns;
        const auto cardRow = (int) i / columns;
        cards[i]->setBounds (x0 + column * (cardWidth + cardGap), y + cardRow * (cardHeight + cardGap), cardWidth, cardHeight);
    }

    const auto rows = ((int) cards.size() + columns - 1) / columns;
    const auto bottom = rows > 0 ? y + rows * (cardHeight + cardGap) - cardGap : y;
    content.setSize (viewportWidth, bottom + paddingBottom);
    repaint();
}

//==============================================================================
EditorView::StatusBar::StatusBar()
{
    for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &detailLabel, &saveButton, &continueButton })
        addAndMakeVisible (c);

    titleLabel.setFont (theme::getFont (theme::Face::screen, 22.0f));
    detailLabel.setFont (theme::getFont (theme::Face::screen, 22.0f));
    theme::bindColour (titleLabel, juce::Label::textColourId, theme::phosphor);
    theme::bindColour (detailLabel, juce::Label::textColourId, theme::phosphor);
    detailLabel.setMinimumHorizontalScale (0.8f);
    theme::setPrimary (continueButton, true);
    theme::setIcon (continueButton, theme::Icon::chevronRight);
}

void EditorView::StatusBar::paint (juce::Graphics& g)
{
    theme::drawDeckBar (g, getLocalBounds(), getLookAndFeel());

    const auto display = theme::getBarDisplayBounds (getLocalBounds(), saveButton.getX());
    theme::drawScreen (g, display.toFloat(), 10.0f, findColour (theme::screen), findColour (theme::screenHighlight), findColour (theme::ink));
    theme::drawScreenLed (g, juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) display.getX() + 20.0f, (float) display.getCentreY() }),
                          findColour (theme::phosphor), hasSamples);
}

void EditorView::StatusBar::resized()
{
    auto area = getLocalBounds().withTrimmedTop (3).reduced (16, 0);
    continueButton.setBounds (area.removeFromRight (200).withSizeKeepingCentre (200, 40 + theme::keyDepth));
    area.removeFromRight (10);
    saveButton.setBounds (area.removeFromRight (110).withSizeKeepingCentre (110, 40 + theme::keyDepth));
    area = theme::getBarDisplayBounds (getLocalBounds(), saveButton.getX()).reduced (12, 0);
    area.removeFromLeft (24);

    const auto titleWidth = juce::GlyphArrangement::getStringWidthInt (titleLabel.getFont(), titleLabel.getText()) + 12;
    titleLabel.setBounds (area.removeFromLeft (titleWidth));
    area.removeFromLeft (4);
    detailLabel.setBounds (area);
}

} // namespace autosampler
