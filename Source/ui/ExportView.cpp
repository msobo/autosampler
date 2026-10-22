#include "ExportView.h"
#include "Theme.h"

namespace autosampler
{

namespace
{
    constexpr ExportTarget targets[] { ExportTarget::wav, ExportTarget::sfz, ExportTarget::tonverk };
    constexpr TailMode tailModes[] { TailMode::original, TailMode::cutAfterLoop };
    constexpr double sampleRates[] { 0.0, 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr int bitDepths[] { 16, 24, 32 };
    constexpr int rowHeight = FormSection::rowHeight, controlHeight = 28;

    template <typename Array, typename Value>
    int indexOf (const Array& array, Value value)
    {
        return (int) std::distance (std::begin (array), std::find (std::begin (array), std::end (array), value));
    }

    juce::String getTargetHint (ExportTarget target)
    {
        switch (target)
        {
            case ExportTarget::wav:
                return "Individual WAVs with smpl chunk (root note, loop) and inst chunk (keys, velocities)."_u;
            case ExportTarget::sfz:
                return "<Name>.sfz with the WAVs in samples/, round robin via seq_position. Can be checked with sfizz."_u;
            case ExportTarget::tonverk:
                return "<Name>.elmulti with WAVs (48 kHz, 16/24 bit) for SD card or Elektron Transfer. "
                       "Format derived, please check on the device."_u;
        }

        return {};
    }
}

//==============================================================================
class ExportView::ExportJob final : public juce::ThreadWithProgressWindow
{
public:
    ExportJob (ExportView& ownerToNotify, std::vector<SamplerZoneSource> sourcesToExport, const ExportSettings& settingsToUse,
               const juce::String& instrumentName, const juce::File& folder)
        : ThreadWithProgressWindow ("Exporting " + instrumentName + " ...", true, true, 10000, "Cancel", &ownerToNotify),
          owner (&ownerToNotify),
          sources (std::move (sourcesToExport)),
          settings (settingsToUse),
          name (instrumentName),
          destination (folder)
    {
    }

    void run() override
    {
        ExportCallbacks callbacks;
        callbacks.onProgress = [this] (double progress) { setProgress (progress); };
        callbacks.shouldCancel = [this] { return threadShouldExit(); };
        result = runExport (sources, settings, name, destination, callbacks);
    }

    void threadComplete (bool userPressedCancel) override
    {
        if (userPressedCancel)
            result.cancelled = true;

        // Asynchron, weil der Besitzer den Job im Callback löscht
        juce::MessageManager::callAsync ([safeOwner = owner, finished = result]
        {
            if (safeOwner != nullptr)
                safeOwner->exportFinished (finished);
        });
    }

private:
    juce::Component::SafePointer<ExportView> owner;
    std::vector<SamplerZoneSource> sources;
    ExportSettings settings;
    juce::String name;
    juce::File destination;
    ExportResult result;
};

//==============================================================================
ExportView::ExportView (Session& sessionToUse)
    : session (sessionToUse),
      sessionState (session.getState())
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    for (auto* section : { &targetSection, &audioSection, &playbackSection, &samplesSection })
        content.addAndMakeVisible (*section);

    for (auto* c : std::initializer_list<juce::Component*> {
             &targetLabel, &targetBox, &nameLabel, &nameEditor, &outputLabel, &outputBox, &targetHint,
             &rateLabel, &rateBox, &bitsLabel, &bitsBox, &tailLabel, &tailBox,
             &normalizeButton, &peakSlider, &monoButton, &loopOnlyButton, &smoothingLabel, &smoothingSlider,
             &loopOnReleaseButton, &oneShotButton, &attackLabel, &attackSlider, &releaseLabel, &releaseSlider,
             &velocityLabel, &velocitySlider, &fromPlayButton,
             &sampleList, &allButton, &noneButton })
        content.addAndMakeVisible (c);

    theme::prepareTextEditor (nameEditor);
    theme::setPrimary (exportButton, true);

    theme::setHint (monoButton, "Sum channels to mono"_u);
    theme::setHint (loopOnlyButton, "Only the loop region with rendered crossfade");
    theme::setHint (loopOnReleaseButton, "Keep the loop running in the release");
    theme::setHint (oneShotButton, "Samples without loop play to the end");
    normalizeButton.setButtonText ("Normalize to");
    loopOnReleaseButton.setButtonText ("Loop on release");
    oneShotButton.setButtonText ("No loop: one shot");

    sampleList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    sampleList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);

    for (size_t i = 0; i < std::size (targets); ++i)
        targetBox.addItem (getDisplayName (targets[i]), (int) i + 1);

    for (size_t i = 0; i < std::size (tailModes); ++i)
        tailBox.addItem (getDisplayName (tailModes[i]), (int) i + 1);

    rateBox.addItem ("As recorded", 1);

    for (size_t i = 1; i < std::size (sampleRates); ++i)
        rateBox.addItem (juce::String (sampleRates[i] / 1000.0, 1) + " kHz", (int) i + 1);

    bitsBox.addItem ("16 Bit", 1);
    bitsBox.addItem ("24 Bit", 2);
    bitsBox.addItem ("32 Bit Float", 3);

    outputBox.addItem ("Folder", 1);
    outputBox.addItem ("Zip archive", 2);

    targetHint.setJustificationType (juce::Justification::topLeft);
    theme::bindColour (targetHint, juce::Label::textColourId, theme::secondaryText);
    targetHint.setFont (juce::FontOptions (theme::fontSize (12.0f)));
    targetHint.setMinimumHorizontalScale (1.0f);

    nameEditor.setTextToShowWhenEmpty ("Session name", findColour (theme::tertiaryText));
    nameEditor.setTooltip ("Name of the instrument and prefix of the file names"_u);
    tailBox.setTooltip ("With loop: cut the sample at the loop end + 32 frames (nothing after E is read anyway)"_u);
    normalizeButton.setTooltip ("One common gain for all samples: the loudest peak reaches the target level, "
                                "the layers keep their relative loudness"_u);
    loopOnlyButton.setTooltip ("Export only the loop region [S, E), with rendered crossfade"_u);
    monoButton.setTooltip ("Sum channels to mono"_u);
    loopOnReleaseButton.setTooltip ("SFZ loop_continuous / Tonverk keep-looping-on-release; off: loop only until note-off"_u);
    oneShotButton.setTooltip ("Samples without loop play to the end (SFZ loop_mode=one_shot)");
    fromPlayButton.setTooltip ("Take envelope, velocity and one-shot behaviour from the Play tab"_u);
    exportButton.setTooltip ("Choose destination folder and export (⌘E)"_u);
    targetBox.setTooltip ("WAV: individual files with metadata. SFZ: instrument for sfizz and other samplers. "
                          "Elektron Tonverk: Multisample (.elmulti)"_u);
    outputBox.setTooltip ("Result as a folder or as a single zip archive"_u);
    rateBox.setTooltip ("Sample rate of the exported files (windowed-sinc resampling); Tonverk always 48 kHz"_u);
    bitsBox.setTooltip ("Bit depth of the exported files"_u);
    peakSlider.setTooltip ("Target level of the loudest peak when normalizing"_u);
    attackSlider.setTooltip ("Attack of the envelope in the SFZ (ampeg_attack)"_u);
    releaseSlider.setTooltip ("Release of the envelope in the SFZ (ampeg_release)"_u);
    velocitySlider.setTooltip ("How strongly the volume follows the velocity (SFZ amp_veltrack)"_u);
    sampleList.setTooltip ("Click a row to include the sample in the export or exclude it (can be undone)"_u);
    allButton.setTooltip ("Export all samples"_u);
    noneButton.setTooltip ("Export no samples"_u);

    peakSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    peakSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    peakSlider.setRange (-6.0, -1.0, 0.5);
    peakSlider.setTextValueSuffix (" dBFS");

    const auto setupSlider = [] (juce::Slider& slider, double min, double max, double interval, const juce::String& suffix)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        slider.setRange (min, max, interval);
        slider.setTextValueSuffix (suffix);
    };

    setupSlider (attackSlider, 0.0, 2000.0, 1.0, " ms");
    attackSlider.setSkewFactorFromMidPoint (50.0);
    setupSlider (releaseSlider, 0.0, 10000.0, 1.0, " ms");
    releaseSlider.setSkewFactorFromMidPoint (500.0);
    setupSlider (velocitySlider, 0.0, 100.0, 1.0, " %");
    setupSlider (smoothingSlider, 0.0, 100.0, 1.0, " %");
    smoothingSlider.setTooltip ("Evens out level jumps between neighbouring notes of a velocity layer "
                                "(at most ±6 dB, a smooth progression is kept). Also applies to auditioning in the Play tab."_u);

    for (auto* box : { &targetBox, &rateBox, &bitsBox, &tailBox, &outputBox })
        box->onChange = [this] { storeSettings(); };

    for (auto* button : { &normalizeButton, &monoButton, &loopOnlyButton, &loopOnReleaseButton, &oneShotButton })
        button->onClick = [this] { storeSettings(); };

    for (auto* slider : { &peakSlider, &attackSlider, &releaseSlider, &velocitySlider, &smoothingSlider })
        slider->onValueChange = [this] { storeSettings(); };

    nameEditor.onTextChange = [this] { storeSettings(); };

    fromPlayButton.onClick = [this]
    {
        if (getPlaySettings == nullptr)
            return;

        const auto play = getPlaySettings();
        settings.attackMs = play.attackMs;
        settings.releaseMs = play.releaseMs;
        settings.velocitySensitivity = play.velocitySensitivity;
        settings.oneShotWithoutLoop = play.oneShotWithoutLoop;
        session.setExportSettings (settings);
        updateControls();
    };

    allButton.onClick = [this] { setAllExported (true); };
    noneButton.onClick = [this] { setAllExported (false); };
    exportButton.onClick = [this] { startExport(); };

    sampleList.setRowHeight (28);

    sessionState.addListener (this);
    session.addChangeListener (this);

    updateControls();
}

ExportView::~ExportView()
{
    session.removeChangeListener (this);
    sessionState.removeListener (this);
    job = nullptr;
}

void ExportView::paint (juce::Graphics& g)
{
    g.fillAll (findColour (theme::canvas));
}

void ExportView::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutContent();
}

void ExportView::layoutContent()
{
    constexpr int pagePaddingX = 28, pagePaddingTop = 24, pagePaddingBottom = 28, maxWidth = 1040, columnGap = 24,
                  minColumn = 440, sectionGap = 24, switchRowHeight = 52, labelColumn = 110;

    const auto viewportWidth = viewport.getMaximumVisibleWidth();
    const auto innerWidth = juce::jmin (maxWidth, viewportWidth - 2 * pagePaddingX);
    const auto x0 = (viewportWidth - innerWidth) / 2;
    const auto twoColumns = innerWidth >= 2 * minColumn + columnGap;
    const auto columnWidth = twoColumns ? (innerWidth - columnGap) / 2 : innerWidth;
    const auto xB = x0 + columnWidth + columnGap;

    const auto place = [] (FormSection& section, int x, int y, int width, std::vector<int> rows)
    {
        section.setRows (std::move (rows));
        section.setBounds (x, y, width, section.getPreferredHeight());
        return y + section.getHeight() + sectionGap;
    };

    const auto centred = [] (juce::Component& c, juce::Rectangle<int> area, int width, int height = controlHeight)
    {
        c.setBounds (area.withSizeKeepingCentre (juce::jmin (width, area.getWidth()), height));
    };

    auto yA = pagePaddingTop, yB = pagePaddingTop;

    // Ziel
    yA = place (targetSection, x0, yA, columnWidth, { rowHeight, rowHeight, rowHeight, 52 });
    {
        const auto labelled = [&] (juce::Label& label, juce::Component& control, int row, int controlWidth)
        {
            auto area = targetSection.getRowBounds (row);
            label.setBounds (area.removeFromLeft (labelColumn).withSizeKeepingCentre (labelColumn, 20));
            centred (control, controlWidth > 0 ? area.removeFromRight (controlWidth) : area, controlWidth > 0 ? controlWidth : area.getWidth());
        };

        labelled (targetLabel, targetBox, 0, 200);
        labelled (nameLabel, nameEditor, 1, 0);
        labelled (outputLabel, outputBox, 2, 140);
        targetHint.setBounds (targetSection.getRowBounds (3).withSizeKeepingCentre (targetSection.getRowBounds (3).getWidth(), 40));
    }

    // Audio
    yA = place (audioSection, x0, yA, columnWidth, { rowHeight, rowHeight, rowHeight, rowHeight, switchRowHeight, switchRowHeight, switchRowHeight });
    {
        const auto labelled = [&] (juce::Label& label, juce::Component& control, int row, int controlWidth)
        {
            auto area = audioSection.getRowBounds (row);
            label.setBounds (area.removeFromLeft (labelColumn + 20).withSizeKeepingCentre (labelColumn + 20, 20));
            centred (control, area.removeFromRight (controlWidth), controlWidth);
        };

        labelled (rateLabel, rateBox, 0, 170);
        labelled (bitsLabel, bitsBox, 1, 170);
        labelled (tailLabel, tailBox, 2, 200);
        labelled (smoothingLabel, smoothingSlider, 3, 230);

        normalizeButton.setBounds (audioSection.getRowBounds (4));
        centred (peakSlider, audioSection.getRowBounds (4).withTrimmedRight (74 + 14), 190);
        peakSlider.setBounds (peakSlider.getBounds().withRightX (audioSection.getRowBounds (4).getRight() - 74 - 14));
        monoButton.setBounds (audioSection.getRowBounds (5));
        loopOnlyButton.setBounds (audioSection.getRowBounds (6));
    }

    if (! twoColumns)
        yB = yA;

    // Wiedergabe
    const auto bx = twoColumns ? xB : x0;
    yB = place (playbackSection, bx, yB, columnWidth, { switchRowHeight, switchRowHeight, rowHeight, rowHeight, rowHeight, rowHeight });
    {
        loopOnReleaseButton.setBounds (playbackSection.getRowBounds (0));
        oneShotButton.setBounds (playbackSection.getRowBounds (1));

        const std::pair<juce::Label*, juce::Slider*> pairs[] { { &attackLabel, &attackSlider }, { &releaseLabel, &releaseSlider },
                                                               { &velocityLabel, &velocitySlider } };

        for (int i = 0; i < 3; ++i)
        {
            auto area = playbackSection.getRowBounds (2 + i);
            pairs[i].first->setBounds (area.removeFromLeft (labelColumn).withSizeKeepingCentre (labelColumn, 20));
            centred (*pairs[i].second, area.removeFromRight (240), 240);
        }

        auto area = playbackSection.getRowBounds (5);
        centred (fromPlayButton, area.removeFromRight (180), 180);
    }

    // Samples
    constexpr int listHeight = 260;
    yB = place (samplesSection, bx, yB, columnWidth, { rowHeight, listHeight });
    {
        auto area = samplesSection.getRowBounds (0);
        centred (allButton, area.removeFromLeft (70), 70);
        area.removeFromLeft (8);
        centred (noneButton, area.removeFromLeft (70), 70);
        sampleList.setBounds (samplesSection.getRowBounds (1).reduced (-8, 6));
    }

    const auto bottom = juce::jmax (yA, yB);
    content.setSize (viewportWidth, bottom - sectionGap + pagePaddingBottom);
}

//==============================================================================
ExportView::ExportBar::ExportBar()
{
    for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &summaryLabel, &exportButton })
        addAndMakeVisible (c);

    titleLabel.setFont (theme::getFont (theme::Face::screen, 22.0f));
    summaryLabel.setFont (theme::getFont (theme::Face::screen, 22.0f));
    titleLabel.setText ("Export", juce::dontSendNotification);
    theme::bindColour (titleLabel, juce::Label::textColourId, theme::phosphor);
    theme::bindColour (summaryLabel, juce::Label::textColourId, theme::phosphor);
    summaryLabel.setMinimumHorizontalScale (0.8f);
}

void ExportView::ExportBar::paint (juce::Graphics& g)
{
    theme::drawDeckBar (g, getLocalBounds(), getLookAndFeel());

    const auto display = theme::getBarDisplayBounds (getLocalBounds(), exportButton.getX());
    theme::drawScreen (g, display.toFloat(), 10.0f, findColour (theme::screen), findColour (theme::screenHighlight), findColour (theme::ink));
    theme::drawScreenLed (g, juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) display.getX() + 20.0f, (float) display.getCentreY() }),
                          findColour (theme::phosphor), ready);
}

void ExportView::ExportBar::resized()
{
    auto area = getLocalBounds().withTrimmedTop (3).reduced (16, 0);
    exportButton.setBounds (area.removeFromRight (190).withSizeKeepingCentre (190, 40 + theme::keyDepth));
    area = theme::getBarDisplayBounds (getLocalBounds(), exportButton.getX()).reduced (12, 0);
    area.removeFromLeft (24);
    titleLabel.setBounds (area.removeFromLeft (70));
    summaryLabel.setBounds (area);
}

//==============================================================================
void ExportView::updateControls()
{
    settings = session.getExportSettings();

    targetBox.setSelectedId (indexOf (targets, settings.target) + 1, juce::dontSendNotification);
    tailBox.setSelectedId (indexOf (tailModes, settings.tail) + 1, juce::dontSendNotification);
    bitsBox.setSelectedId (juce::jmax (0, indexOf (bitDepths, settings.bitDepth)) + 1, juce::dontSendNotification);
    outputBox.setSelectedId (settings.zip ? 2 : 1, juce::dontSendNotification);

    const auto rateIndex = indexOf (sampleRates, settings.sampleRate);
    rateBox.setSelectedId ((rateIndex < (int) std::size (sampleRates) ? rateIndex : 0) + 1, juce::dontSendNotification);

    if (nameEditor.getText() != settings.name)
        nameEditor.setText (settings.name, juce::dontSendNotification);

    nameEditor.setTextToShowWhenEmpty (session.getName().isNotEmpty() ? session.getName() : juce::String ("Instrument"),
                                       juce::Colours::grey);

    normalizeButton.setToggleState (settings.normalize, juce::dontSendNotification);
    peakSlider.setValue (settings.peakDb, juce::dontSendNotification);
    monoButton.setToggleState (settings.mono, juce::dontSendNotification);
    loopOnlyButton.setToggleState (settings.loopOnly, juce::dontSendNotification);
    loopOnReleaseButton.setToggleState (settings.loopOnRelease, juce::dontSendNotification);
    oneShotButton.setToggleState (settings.oneShotWithoutLoop, juce::dontSendNotification);
    attackSlider.setValue (settings.attackMs, juce::dontSendNotification);
    releaseSlider.setValue (settings.releaseMs, juce::dontSendNotification);
    velocitySlider.setValue (settings.velocitySensitivity * 100.0, juce::dontSendNotification);
    smoothingSlider.setValue (settings.zoneSmoothing * 100.0, juce::dontSendNotification);

    // Was das Ziel nicht kennt, ist ausgegraut
    const auto isTonverk = settings.target == ExportTarget::tonverk;
    const auto hasMapping = settings.target != ExportTarget::wav;
    rateBox.setEnabled (! isTonverk);
    peakSlider.setEnabled (settings.normalize);
    tailBox.setEnabled (! settings.loopOnly);
    loopOnReleaseButton.setEnabled (hasMapping);
    oneShotButton.setEnabled (settings.target == ExportTarget::sfz);

    for (auto* c : std::initializer_list<juce::Component*> { &attackLabel, &attackSlider, &releaseLabel, &releaseSlider,
                                                             &velocityLabel, &velocitySlider, &fromPlayButton })
        c->setEnabled (settings.target == ExportTarget::sfz);

    if (isTonverk)
        rateBox.setText ("48.0 kHz", juce::dontSendNotification);

    targetHint.setText (getTargetHint (settings.target), juce::dontSendNotification);

    sampleList.updateContent();
    sampleList.repaint();
    updateSummary();
}

void ExportView::storeSettings()
{
    const auto readIndex = [] (const juce::ComboBox& box) { return box.getSelectedId() - 1; };

    if (const auto i = readIndex (targetBox); juce::isPositiveAndBelow (i, (int) std::size (targets)))
        settings.target = targets[(size_t) i];

    if (const auto i = readIndex (tailBox); juce::isPositiveAndBelow (i, (int) std::size (tailModes)))
        settings.tail = tailModes[(size_t) i];

    if (const auto i = readIndex (bitsBox); juce::isPositiveAndBelow (i, (int) std::size (bitDepths)))
        settings.bitDepth = bitDepths[(size_t) i];

    if (const auto i = readIndex (rateBox); juce::isPositiveAndBelow (i, (int) std::size (sampleRates)))
        settings.sampleRate = sampleRates[(size_t) i];

    settings.zip = outputBox.getSelectedId() == 2;
    settings.name = nameEditor.getText().trim();
    settings.normalize = normalizeButton.getToggleState();
    settings.peakDb = (float) peakSlider.getValue();
    settings.mono = monoButton.getToggleState();
    settings.loopOnly = loopOnlyButton.getToggleState();
    settings.loopOnRelease = loopOnReleaseButton.getToggleState();
    settings.oneShotWithoutLoop = oneShotButton.getToggleState();
    settings.attackMs = (float) attackSlider.getValue();
    settings.releaseMs = (float) releaseSlider.getValue();
    settings.velocitySensitivity = (float) velocitySlider.getValue() / 100.0f;
    settings.zoneSmoothing = (float) smoothingSlider.getValue() / 100.0f;

    session.setExportSettings (settings);
    updateControls();
}

void ExportView::updateSummary()
{
    int numSelected = 0, numLoops = 0;

    for (const auto& tree : session.getSamplesTree())
    {
        const auto sample = SessionSample (tree, nullptr);

        if (sample.isExported())
        {
            ++numSelected;
            numLoops += sample.isLoopEnabled() ? 1 : 0;
        }
    }

    const auto total = session.getNumSamples();
    summaryLabel.setText (total == 0 ? juce::String ("No samples in the session")
                                     : juce::String (numSelected) + " of " + juce::String (total) + " samples selected, "
                                           + juce::String (numLoops) + " with loop",
                          juce::dontSendNotification);

    exportButton.setEnabled (exportAllowed && numSelected > 0 && job == nullptr);
    exportBar.ready = numSelected > 0;
    exportBar.repaint();
}

void ExportView::setExportAllowed (bool allowed)
{
    exportAllowed = allowed;
    updateSummary();
}

juce::String ExportView::getInstrumentName() const
{
    if (settings.name.isNotEmpty())
        return settings.name;

    return session.getName().isNotEmpty() ? session.getName() : juce::String ("Instrument");
}

//==============================================================================
int ExportView::getNumRows()
{
    return session.getNumSamples();
}

void ExportView::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto tree = session.getSamplesTree().getChild (row);

    if (! tree.isValid())
        return;

    const auto sample = SessionSample (tree, nullptr);
    const auto exported = sample.isExported();

    if (selected)
    {
        g.setColour (sampleList.findColour (theme::listSelection));
        g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 1.0f, (float) width, (float) height - 2.0f).reduced (4.0f, 0.0f), 7.0f);
    }

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (14, 0);
    const auto box = area.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f);

    if (exported)
    {
        g.setColour (sampleList.findColour (theme::accent));
        g.fillRoundedRectangle (box, 4.0f);
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 4.0f, box.getCentreY());
        tick.lineTo (box.getX() + 7.0f, box.getBottom() - 4.5f);
        tick.lineTo (box.getRight() - 3.5f, box.getY() + 4.5f);
        g.setColour (juce::Colours::white);
        g.strokePath (tick, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour (sampleList.findColour (theme::pending));
        g.drawRoundedRectangle (box.reduced (0.75f), 4.0f, 1.5f);
    }

    area.removeFromLeft (10);
    g.setFont (juce::FontOptions (theme::fontSize (13.0f)));

    auto name = getNoteName (sample.getNote());
    const auto velocity = "v" + juce::String (sample.getVelocity());
    g.setColour (exported ? sampleList.findColour (theme::primaryText) : sampleList.findColour (theme::tertiaryText));
    g.drawText (name, area.removeFromLeft (44), juce::Justification::centredLeft);
    g.setColour (sampleList.findColour (theme::secondaryText));
    g.drawText (velocity, area.removeFromLeft (46), juce::Justification::centredLeft);

    if (sample.isLoopEnabled())
    {
        const auto chip = area.removeFromLeft (46).toFloat().withSizeKeepingCentre (38.0f, 16.0f);
        g.setColour (sampleList.findColour (theme::chip));
        g.fillRoundedRectangle (chip, 4.0f);
        g.setColour (sampleList.findColour (theme::secondaryText));
        g.setFont (juce::Font (juce::FontOptions (theme::fontSize (10.0f))).boldened());
        g.drawText ("LOOP", chip, juce::Justification::centred);
        g.setFont (juce::FontOptions (theme::fontSize (13.0f)));
    }
    else
    {
        area.removeFromLeft (46);
    }

    g.setColour (sampleList.findColour (theme::tertiaryText));
    g.drawText (sample.getFileName(), area, juce::Justification::centredLeft, true);
}

void ExportView::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    auto sample = session.wrap (session.getSamplesTree().getChild (row));

    if (! sample.isValid())
        return;

    session.getUndoManager().beginNewTransaction (sample.isExported() ? "Exclude from export" : "Export");
    sample.setExported (! sample.isExported());
}

void ExportView::setAllExported (bool shouldBeExported)
{
    session.getUndoManager().beginNewTransaction (shouldBeExported ? "Export all" : "Export none");

    for (const auto& tree : session.getSamplesTree())
        session.wrap (tree).setExported (shouldBeExported);
}

void ExportView::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id)
{
    if (tree.hasType (sessionIds::sample) && (id == sessionIds::exported || id == sessionIds::loopEnabled || id == sessionIds::file))
        triggerAsyncUpdate();
}

void ExportView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Neue oder geladene Session: Einstellungen übernehmen, sofern sie sich unterscheiden
    if (! session.getExportSettings().toValueTree().isEquivalentTo (settings.toValueTree()))
        updateControls();
    else
        nameEditor.setTextToShowWhenEmpty (session.getName().isNotEmpty() ? session.getName() : juce::String ("Instrument"),
                                           findColour (theme::tertiaryText));
}

void ExportView::handleAsyncUpdate()
{
    if (! session.getExportSettings().toValueTree().isEquivalentTo (settings.toValueTree()))
    {
        updateControls();
        return;
    }

    sampleList.updateContent();
    sampleList.repaint();
    updateSummary();
}

//==============================================================================
void ExportView::startExport()
{
    if (job != nullptr || ! exportAllowed)
        return;

    if (getExportSources (session).empty())
    {
        if (onStatus != nullptr)
            onStatus ("No samples selected for export"_u, true);
        return;
    }

    // Nächster vorhandener Ordner, sonst öffnet macOS irgendwo
    // Ohne eigene Wahl im Export gilt der Zielordner des Autosamplings als Vorgabe
    auto start = lastFolder;

    if (start == juce::File())
        start = getDefaultFolder != nullptr ? getDefaultFolder() : juce::File();

    if (start == juce::File())
        start = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Autosampler");

    while (! start.isDirectory() && start.getParentDirectory() != start)
        start = start.getParentDirectory();

    fileChooser = std::make_unique<juce::FileChooser> ("Choose destination folder for the export"_u, start);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis = juce::Component::SafePointer<ExportView> (this)] (const juce::FileChooser& chooser)
                              {
                                  auto folder = chooser.getResult();

                                  if (safeThis == nullptr || folder == juce::File())
                                      return;

                                  if (! folder.isDirectory())
                                      folder = folder.getParentDirectory();

                                  safeThis->runExportTo (folder);
                              });
}

void ExportView::runExportTo (const juce::File& folder)
{
    lastFolder = folder;
    job = std::make_unique<ExportJob> (*this, getExportSources (session), settings, getInstrumentName(), folder);
    updateSummary();

    if (onStatus != nullptr)
        onStatus ("Exporting " + getInstrumentName() + " as " + getDisplayName (settings.target) + " ...", false);

    job->launchThread();
}

void ExportView::exportFinished (const ExportResult& result)
{
    job = nullptr;
    updateSummary();

    if (result.cancelled)
    {
        if (onStatus != nullptr)
            onStatus ("Export cancelled", false);
        return;
    }

    if (result.error.isNotEmpty())
    {
        if (onStatus != nullptr)
            onStatus ("Export failed: " + result.error, true);

        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                              "Export failed", result.error),
                                      nullptr);
        return;
    }

    juce::String message;
    message << result.numWritten << (result.numWritten == 1 ? " sample" : " samples") << " exported to\n"
            << result.output.getFullPathName();

    if (! result.warnings.isEmpty())
        message << "\n\nNotes:\n" << result.warnings.joinIntoString ("\n");

    if (onStatus != nullptr)
        onStatus (juce::String (result.numWritten) + " samples exported: " + result.output.getFullPathName(), false);

    const auto options = juce::MessageBoxOptions()
                             .withIconType (result.warnings.isEmpty() ? juce::MessageBoxIconType::InfoIcon
                                                                      : juce::MessageBoxIconType::WarningIcon)
                             .withTitle ("Export complete")
                             .withMessage (message)
                             .withButton ("Show in Finder")
                             .withButton ("OK")
                             .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [output = result.output] (int button)
    {
        if (button == 1)
            output.revealToUser();
    });
}

} // namespace autosampler
