#include "AutosampleView.h"
#include "Theme.h"
#include "engine/SampleWriter.h"
#include "Text.h"

namespace autosampler
{

namespace
{
    constexpr const char* settingsKey = "samplingSettings";
    constexpr double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr int bitDepths[] = { 16, 24, 32 };

    void setUpSlider (juce::Slider& slider, double min, double max, double interval, const juce::String& suffix)
    {
        slider.setRange (min, max, interval);
        slider.setTextValueSuffix (suffix);
    }

    /** Legt Bedienelemente einer Zeile von links nach rechts nebeneinander, mittig in der Zeilenhöhe. */
    struct RowLayout
    {
        explicit RowLayout (juce::Rectangle<int> rowArea, int gapToUse = 8) : area (rowArea), gap (gapToUse) {}

        void add (juce::Component& c, int width, int height = 28)
        {
            c.setBounds (area.removeFromLeft (width).withSizeKeepingCentre (width, height));
            area.removeFromLeft (gap);
        }

        void addRight (juce::Component& c, int width, int height = 28)
        {
            c.setBounds (area.removeFromRight (width).withSizeKeepingCentre (width, height));
            area.removeFromRight (gap);
        }

        void fill (juce::Component& c, int height = 28)
        {
            c.setBounds (area.withSizeKeepingCentre (area.getWidth(), height));
            area = {};
        }

        juce::Rectangle<int> area;
        int gap;
    };

    constexpr int labelColumn = 96;
    constexpr int sectionGap = 24;

    juce::String formatPan (double value)
    {
        const auto percent = juce::roundToInt (value);
        return percent == 0 ? juce::String ("C") : (percent < 0 ? "L" : "R") + juce::String (std::abs (percent));
    }
}

AutosampleView::AutosampleView (PluginHost& hostToUse, Session& sessionToUse)
    : host (hostToUse), session (sessionToUse)
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.getVerticalScrollBar().addListener (this);

    addAndMakeVisible (scrollHint);
    scrollHint.setTooltip ("More settings below - scroll down"_u);
    scrollHint.onClick = [this]
    {
        viewport.setViewPosition (viewport.getViewPositionX(),
                                  viewport.getViewPositionY() + viewport.getViewHeight() * 4 / 5);
    };

    for (auto* section : { &notesSection, &dynamicsSection, &levelSection, &renderSection, &outputSection, &logSection })
        content.addAndMakeVisible (*section);

    for (auto* c : std::initializer_list<juce::Component*> {
             &rangeLabel, &toLabel, &stepLabel, &noteCountLabel, &lowNoteBox, &highNoteBox, &stepBox,
             &customNotesToggle, &customNotesEditor, &velocitiesLabel, &velocitiesEditor,
             &roundRobinLabel, &roundRobinSourceBox, &modeLabel, &roundRobinModeBox, &stepsLabel, &stepsSlider,
             &variationLabel, &variationTargetBox, &controllerSlider, &parameterNameLabel, &learnButton,
             &valueRangeLabel, &fromSlider, &toValueLabel, &toSlider, &gainLabel, &gainSlider, &panLabel, &panSlider,
             &holdLabel, &tailLabel, &thresholdLabel, &holdSlider, &tailSlider, &thresholdSlider,
             &renderLabel, &renderModeControl, &formatLabel, &sampleRateBox, &bitDepthBox,
             &restoreStateToggle, &trimStartToggle, &autoFindLoopsToggle,
             &nameLabel, &nameEditor, &folderLabel, &folderField, &folderButton, &revealButton, &logEditor })
        content.addAndMakeVisible (c);

    for (auto* editor : { &customNotesEditor, &velocitiesEditor, &nameEditor })
        theme::prepareTextEditor (*editor);

    for (auto* label : { &toLabel, &toValueLabel, &stepsLabel })
        theme::bindColour (*label, juce::Label::textColourId, theme::secondaryText);

    noteCountLabel.setFont (juce::FontOptions (theme::fontSize (12.0f)));
    theme::bindColour (noteCountLabel, juce::Label::textColourId, theme::secondaryText);

    for (int note = 0; note < 128; ++note)
    {
        lowNoteBox.addItem (getNoteName (note) + "  (" + juce::String (note) + ")", note + 1);
        highNoteBox.addItem (getNoteName (note) + "  (" + juce::String (note) + ")", note + 1);
    }

    stepBox.addItem ("every note", 1);

    for (int step = 2; step <= 12; ++step)
        stepBox.addItem ("every " + juce::String (step) + " st", step);

    customNotesEditor.setTextToShowWhenEmpty ("z. B. C2, E2, G2, 60", findColour (theme::tertiaryText));
    velocitiesEditor.setTextToShowWhenEmpty ("z. B. 30, 60, 90, 127", findColour (theme::tertiaryText));

    lowNoteBox.setTooltip ("Lowest note of the range (C3 = MIDI 60)");
    highNoteBox.setTooltip ("Highest note of the range"_u);
    stepBox.setTooltip ("Distance between notes in the range in semitones; the sampler spreads each sample over the keys up to the next note"_u);
    customNotesToggle.setTooltip ("Record a custom note list instead of the range"_u);
    customNotesEditor.setTooltip ("Notes separated by commas, as name (C2, F#3) or MIDI number (60)"_u);
    velocitiesEditor.setTooltip ("Velocity layers separated by commas (1–127); every note is recorded at every velocity"_u);
    stepsSlider.setTooltip ("Number of round-robin variations per note and velocity"_u);
    variationTargetBox.setTooltip ("What changes between variations: a MIDI controller or a plugin parameter"_u);
    parameterNameLabel.setTooltip ("Plugin parameter that is varied; choose with Learn"_u);
    holdSlider.setTooltip ("How long the note is held (note-on to note-off)"_u);
    tailSlider.setTooltip ("Recording continues after note-off for at most this long"_u);
    thresholdSlider.setTooltip ("Recording ends when the level stays below this threshold for 300 ms; it also determines trimming at the end"_u);
    sampleRateBox.setTooltip ("Sample rate of the WAV files; the plugin renders at this rate"_u);
    bitDepthBox.setTooltip ("Bit depth of the WAV files"_u);
    restoreStateToggle.setTooltip ("Resets the plugin to its state at the start before every sample (against drift from LFOs, "
                                   "modulation or arpeggiator); costs some time"_u);
    trimStartToggle.setTooltip ("Cuts silence before the attack (first sample above −60 dBFS, 2 ms pre-roll)"_u);
    nameEditor.setTooltip ("Name of the session and prefix of the file names; empty: name of the plugin"_u);
    folderField.setTooltip ("Every run creates a <Name>.asession folder here"_u);
    folderButton.setTooltip ("Choose destination folder"_u);
    revealButton.setTooltip ("Show the folder of the last run in Finder"_u);
    runBar.startButton.setTooltip ("Starts a run with the loaded plugin; every run begins a new session"_u);
    logEditor.setTooltip ("Log of the last run"_u);
    autoFindLoopsToggle.setTooltip ("After the run, loops are searched automatically for all samples in the editor (like “Find all loops”)"_u);

    theme::setHint (restoreStateToggle, "Load the saved state before every sample"_u);
    theme::setHint (trimStartToggle, "Samples start exactly at the attack");
    theme::setHint (autoFindLoopsToggle, "Determine loop points for all samples"_u);

    roundRobinSourceBox.addItem ("Off", 1 + (int) RoundRobinSource::off);
    roundRobinSourceBox.addItem ("Real RR", 1 + (int) RoundRobinSource::real);
    roundRobinSourceBox.addItem ("MIDI CC", 1 + (int) RoundRobinSource::midiCC);
    roundRobinSourceBox.setTooltip ("Real RR: the instrument varies every hit itself, every note is recorded several times "
                                    "in a row. MIDI CC: a controller or plugin parameter is "
                                    "changed slightly across the steps.");
    roundRobinModeBox.addItem ("Independent", 1 + (int) RoundRobinMode::independent);
    roundRobinModeBox.addItem ("Linked", 1 + (int) RoundRobinMode::linked);
    roundRobinModeBox.setTooltip ("Independent: every velocity with every variation. Linked: variation n belongs to "
                                  "velocity layer n (for samplers without round robin; choose close velocities like 90, 95, 100, 110), "
                                  "no rr tags in the file names.");
    setUpSlider (stepsSlider, 2.0, SamplingSettings::maxRoundRobinSteps, 1.0, {});

    variationTargetBox.addItem ("MIDI CC", 1 + (int) VariationTarget::midiCC);
    variationTargetBox.addItem ("Plugin parameter", 1 + (int) VariationTarget::parameter);
    setUpSlider (controllerSlider, 0.0, 127.0, 1.0, {});
    controllerSlider.setTextValueSuffix ({});
    controllerSlider.setTooltip ("Controller number; the value is sent right before every note-on"_u);
    setUpSlider (fromSlider, 0.0, 127.0, 1.0, {});
    setUpSlider (toSlider, 0.0, 127.0, 1.0, {});
    fromSlider.setTooltip ("Value of the first variation (0–127; for a parameter 0–100 % of its range)"_u);
    toSlider.setTooltip ("Value of the last variation; a small range gives subtle differences");
    theme::bindColour (parameterNameLabel, juce::Label::outlineColourId, theme::controlOutline);
    theme::bindColour (parameterNameLabel, juce::Label::backgroundColourId, theme::field);
    parameterNameLabel.setBorderSize ({ 0, 10, 0, 10 });
    parameterNameLabel.setMinimumHorizontalScale (0.6f);
    learnButton.setClickingTogglesState (true);
    theme::setIcon (learnButton, theme::Icon::learn);
    theme::bindColour (learnButton, juce::TextButton::textColourOffId, theme::accentText);
    learnButton.setTooltip ("Learn: move a control in the plugin editor or send a controller to the MIDI input");
    learnButton.onClick = [this] { learnButton.getToggleState() ? startLearning() : stopLearning(); };

    setUpSlider (gainSlider, -24.0, 12.0, 0.5, " dB");
    gainSlider.setTooltip ("Gain of the recording, applied directly to the WAV files"_u);
    setUpSlider (panSlider, -100.0, 100.0, 1.0, {});
    panSlider.textFromValueFunction = formatPan;
    panSlider.valueFromTextFunction = [] (const juce::String& text)
    {
        const auto value = text.retainCharacters ("0123456789").getDoubleValue();
        return text.trimStart().startsWithIgnoreCase ("L") ? -value : value;
    };
    panSlider.updateText();
    panSlider.setTooltip ("Panorama of the recording: mono becomes stereo (constant power), stereo uses the balance"_u);

    setUpSlider (holdSlider, 0.05, 60.0, 0.05, " s");
    setUpSlider (tailSlider, 0.0, 60.0, 0.5, " s");
    setUpSlider (thresholdSlider, -100.0, -30.0, 1.0, " dB");

    // Reihenfolge wie RenderMode: automatisch, offline, Echtzeit
    renderModeControl.setTooltip ("Automatic: render offline; if the plugin delivers silence, "
                                  "it switches to realtime and the plugin is remembered.");

    for (int i = 0; i < (int) std::size (sampleRates); ++i)
        sampleRateBox.addItem (juce::String (sampleRates[i] / 1000.0, 1) + " kHz", i + 1);

    bitDepthBox.addItem ("16 Bit", 1);
    bitDepthBox.addItem ("24 Bit", 2);
    bitDepthBox.addItem ("32 Bit Float", 3);

    folderButton.onClick = [this] { chooseFolder(); };
    revealButton.onClick = [this] { lastRunFolder.revealToUser(); };

    runBar.startButton.onClick = [this] { isRunning() ? cancel() : start(); };

    logEditor.setMultiLine (true);
    logEditor.setReadOnly (true);
    logEditor.setScrollbarsShown (true);
    logEditor.setCaretVisible (false);
    logEditor.setOpaque (false);
    logEditor.setIndents (10, 8);
    logEditor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));

    loadSettings();

    // Jede Änderung speichern und die Zusammenfassung aktualisieren
    const auto changed = [this] { settingsChanged(); };

    for (auto* box : { &lowNoteBox, &highNoteBox, &stepBox, &sampleRateBox, &bitDepthBox,
                       &roundRobinSourceBox, &roundRobinModeBox, &variationTargetBox })
        box->onChange = changed;

    renderModeControl.onChange = [changed] (int) { changed(); };

    for (auto* toggle : { &customNotesToggle, &restoreStateToggle, &trimStartToggle, &autoFindLoopsToggle })
        toggle->onClick = changed;

    for (auto* editor : { &customNotesEditor, &velocitiesEditor, &nameEditor })
        editor->onTextChange = changed;

    for (auto* slider : { &holdSlider, &tailSlider, &thresholdSlider, &stepsSlider, &controllerSlider,
                          &fromSlider, &toSlider, &gainSlider, &panSlider })
        slider->onValueChange = changed;

    host.addChangeListener (this);
    updateSummary();
    updateControls();
}

AutosampleView::~AutosampleView()
{
    viewport.getVerticalScrollBar().removeListener (this);
    stopLearning();
    host.removeChangeListener (this);

    if (job != nullptr)
    {
        job = nullptr;
        host.setLivePlaybackEnabled (true);
    }
}

void AutosampleView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (learningPlugin != nullptr && host.getPlugin() != learningPlugin)
        stopLearning();

    updateControls();
}

void AutosampleView::settingsChanged()
{
    if (loadingSettings)
        return;

    saveSettings();
    updateSummary();
    updateControls();
}

//==============================================================================
void AutosampleView::startLearning()
{
    stopLearning();

    learningPlugin = host.getPlugin();

    if (learningPlugin != nullptr)
        learningPlugin->addListener (this);

    host.setMidiLearnCallback ([safeThis = juce::Component::SafePointer<AutosampleView> (this)] (int controller)
    {
        if (safeThis != nullptr)
            safeThis->learnController (controller);
    });

    learnButton.setToggleState (true, juce::dontSendNotification);
    learnButton.setButtonText ("Learning...");
}

void AutosampleView::stopLearning()
{
    if (learningPlugin != nullptr)
    {
        learningPlugin->removeListener (this);
        learningPlugin = nullptr;
    }

    host.setMidiLearnCallback (nullptr);
    learnButton.setToggleState (false, juce::dontSendNotification);
    learnButton.setButtonText ("Learn");
}

void AutosampleView::audioProcessorParameterChanged (juce::AudioProcessor*, int parameterIndex, float)
{
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<AutosampleView> (this), parameterIndex]
    {
        if (safeThis != nullptr)
            safeThis->learnParameter (parameterIndex);
    });
}

void AutosampleView::learnParameter (int parameterIndex)
{
    // Nach stopLearning() eintreffende Meldungen ignorieren
    if (learningPlugin == nullptr || learningPlugin != host.getPlugin())
        return;

    const auto& parameters = learningPlugin->getParameters();

    if (! juce::isPositiveAndBelow (parameterIndex, parameters.size()))
        return;

    auto* parameter = parameters[parameterIndex];
    parameterId = getParameterIdentifier (*parameter);
    parameterNameLabel.setText (parameter->getName (64), juce::dontSendNotification);
    variationTargetBox.setSelectedId (1 + (int) VariationTarget::parameter, juce::dontSendNotification);
    stopLearning();
    settingsChanged();
    log ("Variation: parameter " + parameter->getName (64));
}

void AutosampleView::learnController (int controller)
{
    if (! learnButton.getToggleState())
        return;

    variationTargetBox.setSelectedId (1 + (int) VariationTarget::midiCC, juce::dontSendNotification);
    controllerSlider.setValue (controller, juce::dontSendNotification);
    stopLearning();
    settingsChanged();
    log ("Variation: CC " + juce::String (controller));
}

//==============================================================================
void AutosampleView::loadSettings()
{
    SamplingSettings s;

    if (auto xml = host.getSettings().getXmlValue (settingsKey))
        s = SamplingSettings::fromValueTree (juce::ValueTree::fromXml (*xml));

    applySettings (s);
}

void AutosampleView::applySettings (const SamplingSettings& s)
{
    if (isRunning())
        return;

    {
        const juce::ScopedValueSetter<bool> guard (loadingSettings, true);

        lowNoteBox.setSelectedId (s.lowNote + 1);
        highNoteBox.setSelectedId (s.highNote + 1);
        stepBox.setSelectedId (s.noteStep);
        customNotesToggle.setToggleState (s.useCustomNotes, juce::dontSendNotification);
        customNotesEditor.setText (s.customNotes, false);
        velocitiesEditor.setText (s.velocities, false);
        roundRobinSourceBox.setSelectedId (1 + (int) s.roundRobinSource);
        roundRobinModeBox.setSelectedId (1 + (int) s.roundRobinMode);
        stepsSlider.setValue (s.roundRobinSteps, juce::dontSendNotification);
        variationTargetBox.setSelectedId (1 + (int) s.variationTarget);
        controllerSlider.setValue (s.variationController, juce::dontSendNotification);
        parameterId = s.variationParameterId;
        parameterNameLabel.setText (s.variationParameterName, juce::dontSendNotification);
        fromSlider.setValue (s.variationFrom, juce::dontSendNotification);
        toSlider.setValue (s.variationTo, juce::dontSendNotification);
        gainSlider.setValue (s.gainDb, juce::dontSendNotification);
        panSlider.setValue (s.pan * 100.0, juce::dontSendNotification);
        holdSlider.setValue (s.holdSeconds, juce::dontSendNotification);
        tailSlider.setValue (s.maxTailSeconds, juce::dontSendNotification);
        thresholdSlider.setValue (s.tailThresholdDb, juce::dontSendNotification);
        renderModeControl.setSelectedIndex ((int) s.renderMode, juce::dontSendNotification);
        restoreStateToggle.setToggleState (s.restoreStatePerSample, juce::dontSendNotification);
        trimStartToggle.setToggleState (s.trimStart, juce::dontSendNotification);
        autoFindLoopsToggle.setToggleState (s.autoFindLoops, juce::dontSendNotification);
        nameEditor.setText (s.name, false);

        // Ordner aus einer fremden Session nur übernehmen, wenn es ihn hier gibt
        if (s.outputFolder == juce::File() || s.outputFolder.isDirectory())
            outputFolder = s.outputFolder;

        sampleRateBox.setSelectedId (2);

        for (int i = 0; i < (int) std::size (sampleRates); ++i)
            if (juce::approximatelyEqual (sampleRates[i], s.sampleRate))
                sampleRateBox.setSelectedId (i + 1);

        bitDepthBox.setSelectedId (2);

        for (int i = 0; i < (int) std::size (bitDepths); ++i)
            if (bitDepths[i] == s.bitDepth)
                bitDepthBox.setSelectedId (i + 1);

        folderField.setPath (getBaseFolder().getFullPathName());
    }

    saveSettings();
    updateSummary();
    updateControls();
}

void AutosampleView::saveSettings()
{
    if (auto xml = collectSettings().toValueTree().createXml())
        host.getSettings().setValue (settingsKey, xml.get());
}

SamplingSettings AutosampleView::collectSettings() const
{
    SamplingSettings s;
    s.lowNote = lowNoteBox.getSelectedId() - 1;
    s.highNote = highNoteBox.getSelectedId() - 1;
    s.noteStep = juce::jmax (1, stepBox.getSelectedId());
    s.useCustomNotes = customNotesToggle.getToggleState();
    s.customNotes = customNotesEditor.getText();
    s.velocities = velocitiesEditor.getText();
    s.roundRobinSource = (RoundRobinSource) juce::jmax (0, roundRobinSourceBox.getSelectedId() - 1);
    s.roundRobinMode = (RoundRobinMode) juce::jmax (0, roundRobinModeBox.getSelectedId() - 1);
    s.roundRobinSteps = (int) stepsSlider.getValue();
    s.variationTarget = (VariationTarget) juce::jmax (0, variationTargetBox.getSelectedId() - 1);
    s.variationController = (int) controllerSlider.getValue();
    s.variationParameterId = parameterId;
    s.variationParameterName = parameterNameLabel.getText();
    s.variationFrom = (int) fromSlider.getValue();
    s.variationTo = (int) toSlider.getValue();
    s.gainDb = (float) gainSlider.getValue();
    s.pan = (float) panSlider.getValue() / 100.0f;
    s.holdSeconds = holdSlider.getValue();
    s.maxTailSeconds = tailSlider.getValue();
    s.tailThresholdDb = (float) thresholdSlider.getValue();
    s.renderMode = (RenderMode) renderModeControl.getSelectedIndex();
    s.sampleRate = sampleRates[juce::jlimit (0, (int) std::size (sampleRates) - 1, sampleRateBox.getSelectedId() - 1)];
    s.bitDepth = bitDepths[juce::jlimit (0, (int) std::size (bitDepths) - 1, bitDepthBox.getSelectedId() - 1)];
    s.restoreStatePerSample = restoreStateToggle.getToggleState();
    s.trimStart = trimStartToggle.getToggleState();
    s.autoFindLoops = autoFindLoopsToggle.getToggleState();
    s.name = nameEditor.getText().trim();
    s.outputFolder = outputFolder;
    return s;
}

juce::File AutosampleView::getBaseFolder() const
{
    if (outputFolder != juce::File())
        return outputFolder;

    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Autosampler");
}

void AutosampleView::updateSummary()
{
    const auto settings = collectSettings();
    juce::String error;
    const auto requests = buildSampleList (settings, error);

    if (error.isNotEmpty())
    {
        summaryError = error;
        summaryBreakdown.clear();
        summaryLimit.clear();
        noteCountLabel.setText ({}, juce::dontSendNotification);

        if (! isRunning())
        {
            plannedRequests.clear();
            numStarted = 0;
        }

        runBar.detailLabel.setText (error, juce::dontSendNotification);
        theme::bindColour (runBar.detailLabel, juce::Label::textColourId, theme::error);

        if (onOverviewChanged != nullptr)
            onOverviewChanged();

        return;
    }

    juce::String velocityError;
    const auto numNotes = getNotes (settings, error).size();
    const auto numVelocities = parseVelocityList (settings.velocities, velocityError).size();

    const auto numVariants = getRoundRobinCount (settings);

    // Obergrenze in Echtzeit: Haltezeit + maximaler Tail pro Sample
    const auto maxMinutes = requests.size() * (settings.holdSeconds + settings.maxTailSeconds) / 60.0;

    auto text = juce::String (numNotes) + " notes × "_u + juce::String (numVelocities) + " Velocities";

    if (numVariants > 1)
        text << " × "_u << numVariants << " RR";
    else if (settings.roundRobinSource == RoundRobinSource::midiCC && settings.roundRobinMode == RoundRobinMode::linked)
        text << " (Linked)";

    summaryError.clear();
    summaryBreakdown = text;
    summaryLimit = "Realtime at most "_u + juce::String (maxMinutes, 1) + " min";
    noteCountLabel.setText (juce::String (numNotes) + (numNotes == 1 ? " note" : " notes"), juce::dontSendNotification);

    if (! isRunning())
    {
        plannedRequests = requests;
        numStarted = 0;
        runBar.detailLabel.setText (juce::String (requests.size()) + " Samples · "_u + summaryLimit, juce::dontSendNotification);
        theme::bindColour (runBar.detailLabel, juce::Label::textColourId, theme::secondaryText);
    }

    if (onOverviewChanged != nullptr)
        onOverviewChanged();
}

SidebarView::RunOverview AutosampleView::getRunOverview() const
{
    SidebarView::RunOverview overview;
    overview.numSamples = plannedRequests.size();
    overview.description = summaryBreakdown.isNotEmpty() ? summaryBreakdown + " · "_u + summaryLimit : juce::String();
    overview.error = summaryError;

    // Zeilen: Velocities in der Reihenfolge der Liste; Spalten: Noten aufsteigend
    juce::Array<int> velocities;

    for (const auto& request : plannedRequests)
    {
        velocities.addIfNotAlreadyThere (request.velocity);
        if (! std::count (overview.columnNotes.begin(), overview.columnNotes.end(), request.note))
            overview.columnNotes.push_back (request.note);
    }

    std::sort (overview.columnNotes.begin(), overview.columnNotes.end());
    velocities.sort();

    for (int i = velocities.size(); --i >= 0;)
        overview.rowLabels.push_back ("v" + juce::String (velocities[i]));

    const auto numRows = (int) overview.rowLabels.size();
    const auto numColumns = (int) overview.columnNotes.size();
    overview.cells.assign ((size_t) numRows, std::vector<SidebarView::Cell> ((size_t) numColumns, SidebarView::Cell::done));
    std::vector<std::vector<bool>> seen ((size_t) numRows, std::vector<bool> ((size_t) numColumns, false));

    for (int i = 0; i < plannedRequests.size(); ++i)
    {
        const auto& request = plannedRequests.getReference (i);
        const auto row = numRows - 1 - velocities.indexOf (request.velocity);
        const auto column = (int) (std::find (overview.columnNotes.begin(), overview.columnNotes.end(), request.note)
                                   - overview.columnNotes.begin());
        const auto state = i < numStarted ? SidebarView::Cell::done
                         : (i == numStarted && isRunning()) ? SidebarView::Cell::running
                                                            : SidebarView::Cell::open;
        auto& cell = overview.cells[(size_t) row][(size_t) column];

        // Mehrere Varianten in einer Zelle: läuft, wenn eine läuft; fertig nur, wenn alle fertig sind
        if (! seen[(size_t) row][(size_t) column])
            cell = state;
        else if (state == SidebarView::Cell::running || cell == SidebarView::Cell::running)
            cell = SidebarView::Cell::running;
        else if (state == SidebarView::Cell::open || cell == SidebarView::Cell::open)
            cell = SidebarView::Cell::open;

        seen[(size_t) row][(size_t) column] = true;
    }

    return overview;
}

void AutosampleView::updateControls()
{
    const auto running = isRunning();
    const auto useCustom = customNotesToggle.getToggleState();

    for (auto* c : std::initializer_list<juce::Component*> {
             &lowNoteBox, &highNoteBox, &stepBox, &customNotesToggle, &customNotesEditor, &velocitiesEditor,
             &holdSlider, &tailSlider, &thresholdSlider, &renderModeControl, &sampleRateBox, &bitDepthBox,
             &restoreStateToggle, &trimStartToggle, &autoFindLoopsToggle, &nameEditor, &folderButton, &roundRobinSourceBox,
             &roundRobinModeBox, &stepsSlider, &variationTargetBox, &controllerSlider, &learnButton, &fromSlider,
             &toSlider, &gainSlider, &panSlider })
        c->setEnabled (! running);

    // Round Robin: Linked nur mit Variation, Steps nur bei Independent
    const auto source = (RoundRobinSource) juce::jmax (0, roundRobinSourceBox.getSelectedId() - 1);
    const auto varies = source == RoundRobinSource::midiCC;
    const auto linked = varies && roundRobinModeBox.getSelectedId() - 1 == (int) RoundRobinMode::linked;
    const auto toParameter = variationTargetBox.getSelectedId() - 1 == (int) VariationTarget::parameter;

    controllerSlider.setVisible (! toParameter);
    parameterNameLabel.setVisible (toParameter);

    if (! running)
    {
        roundRobinModeBox.setEnabled (varies);
        stepsSlider.setEnabled (source != RoundRobinSource::off && ! linked);

        for (auto* c : std::initializer_list<juce::Component*> { &variationTargetBox, &controllerSlider,
                                                                 &learnButton, &fromSlider, &toSlider })
            c->setEnabled (varies);

        lowNoteBox.setEnabled (! useCustom);
        highNoteBox.setEnabled (! useCustom);
        stepBox.setEnabled (! useCustom);
        customNotesEditor.setEnabled (useCustom);
    }

    auto* plugin = host.getPlugin();
    nameEditor.setTextToShowWhenEmpty (plugin != nullptr ? plugin->getName() : juce::String ("Plugin name"),
                                       findColour (theme::tertiaryText));

    juce::String error;
    const auto valid = ! buildSampleList (collectSettings(), error).isEmpty();

    runBar.setRunning (running);
    runBar.startButton.setButtonText (running ? "Cancel" : "Start sampling");
    theme::setPrimary (runBar.startButton, ! running);
    theme::setTint (runBar.startButton, running ? juce::Colour (0xffff6b5e) : juce::Colour());
    theme::setIcon (runBar.startButton, running ? theme::Icon::none : theme::Icon::play);
    runBar.startButton.setEnabled (running || (plugin != nullptr && valid));
    revealButton.setEnabled (lastRunFolder.isDirectory());
}

void AutosampleView::chooseFolder()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose destination folder"_u, getBaseFolder());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto result = chooser.getResult();

                                  if (result == juce::File())
                                      return;

                                  outputFolder = result;
                                  folderField.setPath (outputFolder.getFullPathName());
                                  saveSettings();
                              });
}

//==============================================================================
void AutosampleView::start()
{
    if (host.getPlugin() == nullptr || isRunning())
        return;

    if (onBeforeReplacingSession == nullptr)
    {
        startNow();
        return;
    }

    onBeforeReplacingSession ([safeThis = juce::Component::SafePointer<AutosampleView> (this)]
    {
        if (safeThis != nullptr)
            safeThis->startNow();
    });
}

void AutosampleView::startNow()
{
    auto* plugin = host.getPlugin();

    if (plugin == nullptr || isRunning())
        return;

    stopLearning();
    const auto settings = collectSettings();
    juce::String error;
    auto requests = buildSampleList (settings, error);

    if (error.isNotEmpty())
    {
        log (error, LogLevel::error);
        return;
    }

    const auto name = settings.name.isNotEmpty() ? settings.name : plugin->getName();
    const auto baseFolder = getBaseFolder();

    if (const auto result = baseFolder.createDirectory(); result.failed())
    {
        log ("Cannot create destination folder: " + result.getErrorMessage(), LogLevel::error);
        return;
    }

    const auto folder = createUniqueFolder (baseFolder, name, Session::packageExtension);

    if (folder == juce::File())
    {
        log ("Cannot create run folder in "_u + baseFolder.getFullPathName(), LogLevel::error);
        return;
    }

    runningPlugin = plugin->getPluginDescription();

    SamplingContext context { *plugin };
    context.deviceManager = &host.getDeviceManager();
    context.settings = settings;
    context.requests = std::move (requests);
    context.filePrefix = name;
    context.preferRealtime = host.prefersRealtimeRendering (runningPlugin);
    plugin->getStateInformation (context.pluginState);

    if (const auto sessionError = session.beginRecording (folder, name, runningPlugin, context.pluginState, settings);
        sessionError.isNotEmpty())
    {
        log (sessionError, LogLevel::error);
        return;
    }

    context.outputFolder = session.getSamplesFolder();

    host.allNotesOff();
    host.setLivePlaybackEnabled (false);

    lastRunFolder = folder;
    runBar.progress = 0.0;
    numStarted = 0;
    plannedRequests = context.requests;
    runStartTime = juce::Time::getMillisecondCounter();
    runBar.remainingLabel.setText ({}, juce::dontSendNotification);
    logEditor.clear();
    logLines.clear();

    job = std::make_unique<SamplingJob> (std::move (context));

    job->onSampleStarted = [this] (int index, int total, const SampleRequest& request)
    {
        runBar.progress = (double) index / (double) juce::jmax (1, total);
        numStarted = index;
        auto text = juce::String (index + 1) + " / " + juce::String (total) + " · "_u
                  + getNoteName (request.note) + " v" + juce::String (request.velocity);

        if (request.roundRobinCount > 1)
            text << " rr" << (request.roundRobin + 1);

        runBar.detailLabel.setText (text, juce::dontSendNotification);
        theme::bindColour (runBar.detailLabel, juce::Label::textColourId, theme::secondaryText);

        // Restzeit aus dem bisherigen Tempo
        if (index > 0)
        {
            const auto elapsed = (juce::Time::getMillisecondCounter() - runStartTime) / 1000.0;
            const auto remaining = juce::roundToInt (elapsed / index * (total - index));
            runBar.remainingLabel.setText ("approx. " + juce::String (remaining / 60) + ":"
                                               + juce::String (remaining % 60).paddedLeft ('0', 2),
                                           juce::dontSendNotification);
        }

        if (onOverviewChanged != nullptr)
            onOverviewChanged();
    };

    job->onLog = [this] (const juce::String& message, LogLevel level) { log (message, level); };

    job->onRealtimeFallback = [this] { host.setPrefersRealtimeRendering (runningPlugin, true); };

    job->onSampleWritten = [this] (const RecordedSample& sample) { session.addRecordedSample (sample); };

    job->onFinished = [this] (const SamplingResult& result)
    {
        // Nicht innerhalb eines Callbacks des Jobs löschen
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<AutosampleView> (this), result]
        {
            if (safeThis != nullptr)
                safeThis->finish (result);
        });
    };

    job->start();
    updateControls();

    if (onOverviewChanged != nullptr)
        onOverviewChanged();

    if (onRunningChanged != nullptr)
        onRunningChanged (true);
}

void AutosampleView::cancel()
{
    if (job == nullptr)
        return;

    job->cancel();
    runBar.startButton.setEnabled (false);
    runBar.startButton.setButtonText ("Cancelling...");
}

void AutosampleView::finish (const SamplingResult& result)
{
    job = nullptr;
    host.setLivePlaybackEnabled (true);

    const auto completed = ! result.aborted && result.error.isEmpty();

    if (completed)
    {
        runBar.progress = 1.0;
        numStarted = plannedRequests.size();
    }

    runBar.remainingLabel.setText ({}, juce::dontSendNotification);

    juce::String summary;
    summary << result.numWritten << " samples written";

    if (result.numSilent > 0)
        summary << ", " << result.numSilent << " without signal";

    if (result.numClipped > 0)
        summary << ", " << result.numClipped << " clipped"_u;

    if (result.numIdenticalGroups > 0)
        summary << ", " << result.numIdenticalGroups << " RR groups without difference";

    summary << " (" << (result.modeUsed == RenderMode::realtime ? "Realtime" : "Offline") << ")";

    if (const auto saveError = session.save(); saveError.isNotEmpty())
        log ("Session not saved: " + saveError, LogLevel::error);

    if (result.error.isNotEmpty())
        log ("Error: " + result.error, LogLevel::error);
    else if (result.aborted)
        log ("Cancelled. " + summary, LogLevel::warning);
    else
        log ("Done. " + summary);

    updateControls();
    updateSummary();

    if (completed)
        numStarted = plannedRequests.size();

    if (onOverviewChanged != nullptr)
        onOverviewChanged();

    if (onRunningChanged != nullptr)
        onRunningChanged (false);

    if (result.numWritten > 0 && autoFindLoopsToggle.getToggleState() && onFindLoopsRequested != nullptr)
        onFindLoopsRequested();
}

void AutosampleView::log (const juce::String& message, LogLevel level)
{
    logLines.emplace_back (message, level);
    appendToLog (message, level);
}

void AutosampleView::appendToLog (const juce::String& message, LogLevel level)
{
    const auto colour = findColour (level == LogLevel::error   ? theme::error
                                  : level == LogLevel::warning ? theme::warning
                                                               : theme::secondaryText);

    logEditor.moveCaretToEnd();
    logEditor.setColour (juce::TextEditor::textColourId, colour);
    logEditor.insertTextAtCaret (message + "\n");
}

void AutosampleView::lookAndFeelChanged()
{
    // Der TextEditor behält die Farbe jeder Zeile, daher neu schreiben
    logEditor.clear();

    for (const auto& [message, level] : logLines)
        appendToLog (message, level);

    customNotesEditor.setTextToShowWhenEmpty ("z. B. C2, E2, G2, 60", findColour (theme::tertiaryText));
    velocitiesEditor.setTextToShowWhenEmpty ("z. B. 30, 60, 90, 127", findColour (theme::tertiaryText));
}

//==============================================================================
AutosampleView::RunBar::RunBar()
{
    for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &detailLabel, &remainingLabel, &progressBar, &startButton })
        addAndMakeVisible (c);

    titleLabel.setFont (theme::getFont (theme::Face::screen, 22.0f));
    detailLabel.setFont (theme::getFont (theme::Face::screen, 22.0f));
    remainingLabel.setFont (theme::getFont (theme::Face::screen, 22.0f));
    titleLabel.setMinimumHorizontalScale (1.0f);
    detailLabel.setMinimumHorizontalScale (0.8f);
    remainingLabel.setJustificationType (juce::Justification::centredRight);

    for (auto* label : { &titleLabel, &detailLabel, &remainingLabel })
        theme::bindColour (*label, juce::Label::textColourId, theme::phosphor);

    theme::setPrimary (startButton, true);
    theme::setIcon (startButton, theme::Icon::play);
    progressBar.setPercentageDisplay (false);
    setRunning (false);
}

void AutosampleView::RunBar::setRunning (bool shouldRun)
{
    running = shouldRun;
    titleLabel.setText (running ? "Sampling in progress"_u : "Ready", juce::dontSendNotification);
    progressBar.setVisible (running);
    remainingLabel.setVisible (running);
    resized();
    repaint();
}

void AutosampleView::RunBar::paint (juce::Graphics& g)
{
    theme::drawDeckBar (g, getLocalBounds(), getLookAndFeel());

    const auto display = theme::getBarDisplayBounds (getLocalBounds(), startButton.getX());
    theme::drawScreen (g, display.toFloat(), 10.0f, findColour (theme::screen), findColour (theme::screenHighlight), findColour (theme::ink));
    theme::drawScreenLed (g, juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) display.getX() + 20.0f, (float) display.getCentreY() }),
                          findColour (running ? theme::pink : theme::phosphor), true);
}

void AutosampleView::RunBar::resized()
{
    auto area = getLocalBounds().withTrimmedTop (3).reduced (16, 0);
    const auto buttonWidth = running ? 150 : 210;
    startButton.setBounds (area.removeFromRight (buttonWidth).withSizeKeepingCentre (buttonWidth, 40 + theme::keyDepth));
    area = theme::getBarDisplayBounds (getLocalBounds(), startButton.getX()).reduced (12, 0);
    area.removeFromLeft (24);

    const auto titleWidth = juce::GlyphArrangement::getStringWidthInt (titleLabel.getFont(), titleLabel.getText()) + 12;
    titleLabel.setBounds (area.removeFromLeft (titleWidth));
    area.removeFromLeft (4);

    if (running)
    {
        remainingLabel.setBounds (area.removeFromRight (110));
        area.removeFromRight (12);
        detailLabel.setBounds (area.removeFromLeft (juce::jmin (230, area.getWidth() / 2)));
        area.removeFromLeft (12);
        progressBar.setBounds (area.withSizeKeepingCentre (area.getWidth(), 8));
    }
    else
    {
        detailLabel.setBounds (area);
    }
}

//==============================================================================
void AutosampleView::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutContent();
    updateScrollHint();
}

AutosampleView::ScrollHint::ScrollHint()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void AutosampleView::ScrollHint::paint (juce::Graphics& g)
{
    const auto centre = getLocalBounds().toFloat().getCentre();
    juce::Path chevron;
    chevron.startNewSubPath (centre.x - 9.0f, centre.y - 4.5f);
    chevron.lineTo (centre.x, centre.y + 4.5f);
    chevron.lineTo (centre.x + 9.0f, centre.y - 4.5f);

    g.setColour (findColour (theme::secondaryText));
    g.strokePath (chevron, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AutosampleView::ScrollHint::mouseUp (const juce::MouseEvent& event)
{
    if (event.mouseWasClicked() && onClick != nullptr)
        onClick();
}

void AutosampleView::updateScrollHint()
{
    constexpr int hintWidth = 56, hintHeight = 28;
    const auto moreBelow = viewport.getViewPositionY() + viewport.getViewHeight() < content.getHeight() - 2;

    scrollHint.setBounds (getLocalBounds().withSizeKeepingCentre (hintWidth, hintHeight)
                              .withBottomY (getHeight() - 6));
    scrollHint.setVisible (moreBelow);
    scrollHint.toFront (false);
}

void AutosampleView::layoutContent()
{
    constexpr int pagePaddingX = 28, pagePaddingTop = 24, pagePaddingBottom = 28, maxWidth = 1040, columnGap = 24,
                  minColumn = 440;

    const auto viewportWidth = viewport.getMaximumVisibleWidth();
    const auto innerWidth = juce::jmin (maxWidth, viewportWidth - 2 * pagePaddingX);
    const auto x0 = (viewportWidth - innerWidth) / 2;
    const auto twoColumns = innerWidth >= 2 * minColumn + columnGap;
    const auto columnWidth = twoColumns ? (innerWidth - columnGap) / 2 : innerWidth;
    const auto xB = x0 + columnWidth + columnGap;

    // Platziert einen Abschnitt und gibt die Oberkante des nächsten zurück
    const auto place = [] (FormSection& section, int x, int y, int width, std::vector<int> rows)
    {
        section.setRows (std::move (rows));
        section.setBounds (x, y, width, section.getPreferredHeight());
        return y + section.getHeight() + sectionGap;
    };

    const auto rowH = FormSection::rowHeight;
    auto yA = pagePaddingTop, yB = pagePaddingTop;

    // Noten
    yA = place (notesSection, x0, yA, columnWidth, { rowH, rowH, rowH, 40 });
    {
        RowLayout row (notesSection.getRowBounds (0));
        row.add (rangeLabel, labelColumn);
        const auto comboWidth = juce::jmin (120, (row.area.getWidth() - 60) / 2);
        row.add (lowNoteBox, comboWidth);
        row.add (toLabel, 34);
        row.add (highNoteBox, comboWidth);

        row = RowLayout (notesSection.getRowBounds (1));
        row.add (stepLabel, labelColumn);
        row.add (stepBox, 150);
        row.fill (noteCountLabel, 20);

        customNotesToggle.setBounds (notesSection.getRowBounds (2));
        customNotesEditor.setBounds (notesSection.getRowBounds (3).withTrimmedBottom (12).withHeight (28));
    }

    // Dynamik und Variation
    yA = place (dynamicsSection, x0, yA, columnWidth, { rowH, rowH, rowH, rowH, rowH });
    {
        RowLayout row (dynamicsSection.getRowBounds (0));
        row.add (velocitiesLabel, labelColumn);
        row.fill (velocitiesEditor);

        row = RowLayout (dynamicsSection.getRowBounds (1));
        row.add (roundRobinLabel, labelColumn);
        row.add (roundRobinSourceBox, 150);

        row = RowLayout (dynamicsSection.getRowBounds (2));
        row.add (modeLabel, labelColumn);
        row.add (roundRobinModeBox, 130);
        row.add (stepsLabel, 40);
        row.add (stepsSlider, 110);

        row = RowLayout (dynamicsSection.getRowBounds (3));
        row.add (variationLabel, labelColumn);
        row.add (variationTargetBox, 130);
        row.addRight (learnButton, 72);

        // Controller-Zahlenfeld und Parametername teilen sich die Stelle
        controllerSlider.setBounds (row.area.withWidth (110).withSizeKeepingCentre (110, 28));
        parameterNameLabel.setBounds (row.area.withSizeKeepingCentre (row.area.getWidth(), 28));

        row = RowLayout (dynamicsSection.getRowBounds (4));
        row.add (valueRangeLabel, labelColumn);
        row.add (fromSlider, 110);
        row.add (toValueLabel, 24);
        row.add (toSlider, 110);
    }

    // Pegel und Timing
    yB = place (levelSection, twoColumns ? xB : x0, twoColumns ? yB : yA, columnWidth, { rowH, rowH, rowH, rowH, rowH });
    {
        const std::pair<juce::Label*, StepperSlider*> pairs[] { { &gainLabel, &gainSlider }, { &panLabel, &panSlider },
                                                                { &holdLabel, &holdSlider }, { &tailLabel, &tailSlider },
                                                                { &thresholdLabel, &thresholdSlider } };

        for (int i = 0; i < 5; ++i)
        {
            RowLayout row (levelSection.getRowBounds (i));
            row.addRight (*pairs[i].second, 150);
            row.fill (*pairs[i].first, 20);
        }
    }

    if (! twoColumns)
        yA = yB;

    // Rendern
    constexpr int switchRowH = 52;
    yB = place (renderSection, twoColumns ? xB : x0, twoColumns ? yB : yA, columnWidth,
                { rowH, rowH, switchRowH, switchRowH, switchRowH });
    {
        RowLayout row (renderSection.getRowBounds (0));
        row.addRight (renderModeControl, renderModeControl.getPreferredWidth(), renderModeControl.getPreferredHeight());
        row.fill (renderLabel, 20);

        row = RowLayout (renderSection.getRowBounds (1));
        row.addRight (bitDepthBox, 120);
        row.addRight (sampleRateBox, 100);
        row.fill (formatLabel, 20);

        restoreStateToggle.setBounds (renderSection.getRowBounds (2));
        trimStartToggle.setBounds (renderSection.getRowBounds (3));
        autoFindLoopsToggle.setBounds (renderSection.getRowBounds (4));
    }

    auto y = twoColumns ? juce::jmax (yA, yB) : yB;

    // Ausgabe über die ganze Breite
    y = place (outputSection, x0, y, innerWidth, { rowH, rowH });
    {
        RowLayout row (outputSection.getRowBounds (0));
        row.add (nameLabel, labelColumn);
        row.fill (nameEditor);

        row = RowLayout (outputSection.getRowBounds (1));
        row.add (folderLabel, labelColumn);
        row.addRight (revealButton, 128);
        row.addRight (folderButton, 80);
        row.fill (folderField);
    }

    // Protokoll
    y = place (logSection, x0, y, innerWidth, { 190 });
    logEditor.setBounds (logSection.getRowBounds (0).reduced (-6, 0).reduced (0, 12));

    content.setSize (viewportWidth, y - sectionGap + pagePaddingBottom);
}

} // namespace autosampler
