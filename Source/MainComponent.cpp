#include "MainComponent.h"
#include "Text.h"

namespace autosampler
{

//==============================================================================
/** Fenster mit der Plugin-Liste; über "Options" werden VST3/AU gescannt. */
class MainComponent::PluginListWindow final : public juce::DocumentWindow
{
public:
    PluginListWindow (PluginHost& pluginHost, juce::PropertiesFile* settings, std::function<void()> onCloseCallback)
        : DocumentWindow ("Plugins",
                          juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::closeButton),
          onClose (std::move (onCloseCallback))
    {
        auto list = std::make_unique<juce::PluginListComponent> (pluginHost.getFormatManager(),
                                                                 pluginHost.getKnownPluginList(),
                                                                 pluginHost.getDeadMansPedalFile(),
                                                                 settings,
                                                                 true);
        list->setScanDialogText ("Scan plugins", "Searching for plugins...");

        theme::bindColour (*this, juce::ResizableWindow::backgroundColourId, juce::ResizableWindow::backgroundColourId);
        setUsingNativeTitleBar (true);
        setContentOwned (list.release(), false);
        setResizable (true, false);
        setResizeLimits (400, 300, 1600, 1600);
        centreWithSize (720, 520);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (onClose != nullptr)
            onClose();
    }

private:
    std::function<void()> onClose;
};

//==============================================================================
MainComponent::MainComponent (PluginHost& hostToUse, Appearance& appearanceToUse)
    : host (hostToUse),
      appearance (appearanceToUse),
      keyboard (host.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard),
      autosampleView (host, session),
      editorView (session),
      livePlayView (host),
      exportView (session),
      samplerLoader (session)
{
    for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &subtitleLabel, &navControl, &pluginsButton,
                                                             &instrumentBox, &editorButton, &audioButton, &panicButton,
                                                             &sourceControl, &chordToggle, &sourceStatusLabel, &statusLabel,
                                                             &velocityLabel, &velocitySlider, &velocityValueLabel,
                                                             &keyboard, &tabs, &sidebar })
        addAndMakeVisible (c);

    addChildComponent (autosampleView.getBottomBar());
    addChildComponent (editorView.getBottomBar());
    addChildComponent (exportView.getBottomBar());

    const auto tabColour = juce::Colours::transparentBlack;
    tabs.addTab ("Autosample", tabColour, &autosampleView, false);
    tabs.addTab ("Editor", tabColour, &editorView, false);
    tabs.addTab ("Play", tabColour, &livePlayView, false);
    tabs.addTab ("Export", tabColour, &exportView, false);
    tabs.setOutline (0);
    tabs.setTabBarDepth (0);
    tabs.onChanged = [this] (int) { updateTab(); };
    navControl.setFlowMarkers (true);
    navControl.onChange = [this] (int index) { tabs.setCurrentTabIndex (index); };

    titleLabel.setFont (theme::getFont (theme::Face::screen, 22.0f));
    titleLabel.setMinimumHorizontalScale (0.8f);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    theme::bindColour (titleLabel, juce::Label::textColourId, theme::phosphor);
    subtitleLabel.setFont (juce::FontOptions (theme::fontSize (11.0f)));
    subtitleLabel.setText ("Autosampler", juce::dontSendNotification);
    theme::bindColour (subtitleLabel, juce::Label::textColourId, theme::secondaryText);

    autosampleView.onRunningChanged = [this] (bool running) { setSamplingActive (running); };
    autosampleView.onFindLoopsRequested = [this] { editorView.findAllLoops(); };
    autosampleView.onBeforeReplacingSession = [this] (std::function<void()> proceed)
    {
        confirmDiscardChanges (std::move (proceed));
    };
    autosampleView.onOverviewChanged = [this]
    {
        sidebar.setRunOverview (autosampleView.getRunOverview());
        updateKeyboardMarks();
    };

    sidebar.onPowerClicked = [this]
    {
        appearance.setMode (appearance.isDark() ? AppearanceMode::light : AppearanceMode::dark);
        commandManager.commandStatusChanged();
    };
    sidebar.onSampleSelected = [this] (int index) { editorView.setSelectedIndex (index); };
    editorView.onSelectionChanged = [this] (int index)
    {
        sidebar.setSelectedSample (index);
        updateKeyboardMarks();
    };

    pluginsButton.setTooltip ("Plugin list: scan VST3/AU (Options > Scan), remove or sort plugins"_u);
    instrumentBox.setTooltip ("Load an instrument; the plugin is played via keyboard and MIDI in and gets sampled"_u);
    editorButton.setTooltip ("Open the editor of the loaded plugin"_u);
    audioButton.setTooltip ("Audio device, sample rate, buffer size and MIDI inputs"_u);
    panicButton.setTooltip ("All notes off, stop arp and sequencer (⌘.)"_u);
    sidebar.setTooltip ("Data of the loaded plugin: channels, latency and tail as reported by the plugin"_u);

    theme::setIcon (editorButton, theme::Icon::plug);
    theme::setIcon (audioButton, theme::Icon::sliders);
    theme::setCartridge (instrumentBox, true);
    theme::setTint (panicButton, juce::Colour (0xffffc23d));

    instrumentBox.setTextWhenNothingSelected ("Load Plugin"_u);
    instrumentBox.setColour (juce::ComboBox::textColourId, juce::Colours::black);
    instrumentBox.setTextWhenNoChoicesAvailable ("No instruments - please scan");
    instrumentBox.onChange = [this] { loadSelectedInstrument(); };

    pluginsButton.onClick = [this] { showPluginList(); };
    editorButton.onClick  = [this] { openEditor(); };
    audioButton.onClick   = [this] { showAudioSettings(); };
    panicButton.onClick   = [this] { host.allNotesOff(); };

    theme::setKeyText (chordToggle, "CHORD");
    chordToggle.setTooltip ("Chord player on/off (settings in the Play tab)"_u);
    chordToggle.onClick = [this] { livePlayView.setChordEnabled (chordToggle.getToggleState()); };

    // A/B: Keyboard und MIDI-In spielen das Plugin oder die Samples der Session
    sourceControl.setTooltip ("Keyboard and MIDI in play the plugin (⌘1) or the samples of the session (⌘2); A/B with ⌘B"_u);
    sourceControl.onChange = [this] (int index)
    {
        livePlayView.setSource (index == 0 ? PlaySource::plugin : PlaySource::sampler);
    };

    livePlayView.onSettingsChanged = [this]
    {
        updateSourceButtons();
        commandManager.commandStatusChanged();
    };

    editorView.setAudition (&host.getLivePlay());

    exportView.getPlaySettings = [this] { return livePlayView.getSettings().sampler; };
    exportView.getDefaultFolder = [this] { return autosampleView.getBaseFolder(); };
    exportView.onStatus = [this] (const juce::String& text, bool isError) { setStatus (text, isError); };
    editorView.onContinueToExport = [this] { showTab ("export"); };
    editorView.onSave = [this] { saveCurrentSession(); };

    samplerLoader.onInstrumentReady = [this] (std::shared_ptr<const SamplerInstrument> instrument)
    {
        host.getLivePlay().setInstrument (std::move (instrument));
    };

    samplerLoader.onStatusChanged = [this]
    {
        livePlayView.setSamplerStatus (samplerLoader.getStatus());
        sourceStatusLabel.setText (samplerLoader.getStatus(), juce::dontSendNotification);
        sourceControl.setTooltip ("Keyboard and MIDI in play the plugin (⌘1) or the samples of the session (⌘2); A/B with ⌘B\n"_u
                                  + samplerLoader.getStatus());
    };

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setFont (juce::FontOptions (theme::fontSize (11.0f)));

    sourceStatusLabel.setJustificationType (juce::Justification::centredLeft);
    sourceStatusLabel.setMinimumHorizontalScale (0.7f);
    theme::bindColour (sourceStatusLabel, juce::Label::textColourId, theme::secondaryText);
    theme::bindColour (velocityLabel, juce::Label::textColourId, theme::secondaryText);
    velocityValueLabel.setJustificationType (juce::Justification::centred);
    velocityValueLabel.setFont (theme::getFont (theme::Face::screen, 21.0f));
    theme::bindColour (velocityValueLabel, juce::Label::textColourId, theme::phosphor);
    velocityLabel.setFont (theme::getKeyFont (12.0f));
    velocityLabel.setText ("VEL", juce::dontSendNotification);
    theme::bindColour (velocityLabel, juce::Label::textColourId, theme::primaryText);

    keyboard.setAvailableRange (24, 96);
    keyboard.setLowestVisibleKey (24);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setWantsKeyboardFocus (true);
    keyboard.addKeyListener (this);
    setKeyboardOctave (keyboardOctave);

    // Feste Velocity für Maus und Computertasten (am Regler einstellbar)
    velocitySlider.setRange (1.0, 127.0, 1.0);
    velocitySlider.setValue (100.0, juce::dontSendNotification);
    velocitySlider.setDoubleClickReturnValue (true, 100.0);
    velocitySlider.setTooltip ("Velocity of mouse clicks and computer keys (1-127, double-click: 100)"_u);
    velocitySlider.onValueChange = [this]
    {
        keyboard.setVelocity ((float) velocitySlider.getValue() / 127.0f, false);
        velocityValueLabel.setText (juce::String ((int) velocitySlider.getValue()), juce::dontSendNotification);
    };
    velocityValueLabel.setText ("100", juce::dontSendNotification);
    keyboard.setVelocity (100.0f / 127.0f, false);

    host.getKnownPluginList().addChangeListener (this);
    host.addChangeListener (this);
    host.onBeforePluginUnload = [this]
    {
        autosampleView.stopLearning();
        closeEditor();
    };

    session.addChangeListener (this);
    session.getUndoManager().addChangeListener (this);

    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());
    setApplicationCommandManagerToWatch (&commandManager);
    appMenu.addCommandItem (&commandManager, showAbout);
    juce::MenuBarModel::setMacMainMenu (this, &appMenu);

    refreshInstrumentList();
    updatePluginInfo();
    updateSourceButtons();
    updateTab();
    updateSidebarSamples();
    sidebar.setRunOverview (autosampleView.getRunOverview());
    setStatus (instruments.isEmpty() ? "No instruments known yet: Plugins... > Options > Scan"
                                     : juce::String (instruments.size()) + " instruments available"_u);

    setSize (1440, 940);
}

MainComponent::~MainComponent()
{
    keyboard.removeKeyListener (this);
    juce::MenuBarModel::setMacMainMenu (nullptr);
    removeKeyListener (commandManager.getKeyMappings());
    session.getUndoManager().removeChangeListener (this);
    session.removeChangeListener (this);

    host.onBeforePluginUnload = nullptr;
    host.removeChangeListener (this);
    host.getKnownPluginList().removeChangeListener (this);

    closeEditor();
    pluginListWindow = nullptr;
    aboutOverlay = nullptr;
}

namespace
{
    constexpr int caseMargin = 10, caseShadow = 8, caseBorder = 3, caseRadius = 16;
    constexpr int toolbarHeight = 60, sidebarWidth = 280, bottomBarHeight = 62, statusStripHeight = 22;
    constexpr int keyboardHeight = 112, keyboardFrame = 3, keyboardShadow = 4;

    /** Herz aus Pixeln (Logo): Pfad der Vorlage in einem Raster 7 x 6. */
    juce::Path makePixelHeart()
    {
        return juce::Drawable::parseSVGPath ("M1 0h2v1h1V0h2v1h1v2H6v1H5v1H4v1H3V5H2V4H1V3H0V1h1z");
    }
}

juce::Rectangle<int> MainComponent::getCaseBounds() const
{
    return getLocalBounds().withTrimmedBottom (caseShadow).reduced (caseMargin);
}

void MainComponent::paint (juce::Graphics& g)
{
    const auto ink = findColour (theme::ink);

    // Raum mit Raster
    g.fillAll (findColour (theme::room));
    g.setColour (findColour (theme::roomGrid));

    for (int x = 0; x < getWidth(); x += 32)
        g.fillRect (x, 0, 1, getHeight());

    for (int y = 0; y < getHeight(); y += 32)
        g.fillRect (0, y, getWidth(), 1);

    // Gehäuse mit hartem Schatten
    const auto caseBounds = getCaseBounds();
    const auto caseF = caseBounds.toFloat();
    g.setColour (ink);
    g.fillRoundedRectangle (caseF.translated (0.0f, (float) caseShadow), (float) caseRadius);
    g.setColour (findColour (theme::canvas));
    g.fillRoundedRectangle (caseF, (float) caseRadius);

    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (caseF, (float) caseRadius);
        g.reduceClipRegion (clip);

        // Deck oben und Keyboard-Feld unten
        const auto toolbar = caseBounds.withHeight (toolbarHeight + caseBorder);
        g.setColour (findColour (theme::toolbar));
        g.fillRect (toolbar);
        g.setColour (findColour (theme::caseHighlight));
        g.fillRect (toolbar.withTrimmedTop (caseBorder).withHeight (3));
        g.setColour (ink);
        g.fillRect (toolbar.getX(), toolbar.getBottom() - 3, toolbar.getWidth(), 3);

        g.setColour (ink);
        g.fillRect (caseBounds.getX(), keyboardPanelTop, caseBounds.getWidth(), 3);
    }

    // Logo: Pixelherzen und Balken, darunter der Schriftzug
    const auto toolbarArea = caseBounds.withHeight (toolbarHeight).reduced (caseBorder + 16, 0);
    const auto logoX = toolbarArea.getX();
    const auto logoY = (float) toolbarArea.getCentreY() - 15.0f;
    const auto heart = makePixelHeart();

    if (logoHovered)
    {
        const auto hover = getLogoBounds().toFloat().reduced (-6.0f, -4.0f).withSizeKeepingCentre (getLogoBounds().getWidth() + 4.0f, 44.0f);
        g.setColour (findColour (theme::pink).withAlpha (0.18f));
        g.fillRoundedRectangle (hover, 8.0f);
        g.setColour (findColour (theme::pink));
        g.drawRoundedRectangle (hover, 8.0f, 2.0f);
    }

    for (int i = 0; i < 3; ++i)
    {
        const auto transform = juce::AffineTransform::scale (2.0f).translated ((float) logoX + 17.0f * (float) i, logoY);
        auto path = heart;
        path.applyTransform (transform);
        g.setColour (findColour (theme::pink));

        if (i < 2)
            g.fillPath (path);
        else
            g.strokePath (path, juce::PathStrokeType (1.6f));
    }

    g.setColour (juce::Colour (0xff8b5cf6));
    g.fillRoundedRectangle ((float) logoX, logoY + 17.0f, 48.0f, 4.0f, 2.0f);
    g.fillRoundedRectangle ((float) logoX, logoY + 24.0f, 34.0f, 4.0f, 2.0f);

    if (showLogoText)
    {
        g.setColour (findColour (theme::primaryText));
        g.setFont (theme::getFont (theme::Face::logo, 21.0f));
        g.drawText ("AUTO", logoX + 62, (int) logoY - 2, 120, 22, juce::Justification::centredLeft);
        g.drawText ("SAMPLER", logoX + 62, (int) logoY + 18, 120, 22, juce::Justification::centredLeft);
    }

    // Name der Session auf einem kleinen Bildschirm
    if (titleLabel.isVisible())
    {
        const auto box = titleLabel.getBounds().expanded (10, 6);
        theme::drawScreen (g, box.toFloat(), 7.0f, findColour (theme::screen), findColour (theme::screenHighlight), ink);
    }

    // Keyboard-Feld: Schild, Wert der Velocity und Rahmen der Klaviatur
    const auto plateTop = keyboardPanelTop + 3 + 14;
    theme::drawPlate (g, { caseBounds.getX() + caseBorder + 22, plateTop + 5 }, "Keyboard & MIDI", getLookAndFeel());

    const auto velocityBox = velocityValueLabel.getBounds().expanded (6, 0);
    theme::drawScreen (g, velocityBox.toFloat(), 6.0f, findColour (theme::screen), findColour (theme::screenHighlight), ink);

    const auto frameBody = keyboard.getBounds().expanded (keyboardFrame).toFloat();
    g.fillRoundedRectangle (frameBody.translated (0.0f, (float) keyboardShadow), 7.0f);
    g.fillRoundedRectangle (frameBody, 7.0f);

    // Rahmen oben im Gehäuse
    g.setColour (ink);
    g.drawRoundedRectangle (caseF.reduced (1.5f), (float) caseRadius - 1.5f, (float) caseBorder);
}

void MainComponent::resized()
{
    if (aboutOverlay != nullptr)
        aboutOverlay->setBounds (getLocalBounds());

    auto area = getCaseBounds().reduced (caseBorder);

    // Werkzeugleiste: Logo | Name | Bereiche | Instrument und Knöpfe
    auto toolbar = area.removeFromTop (toolbarHeight).reduced (16, 0);
    constexpr int keyHeight = 30 + theme::keyDepth, cartridgeHeight = 38;

    const auto logoWidth = 62 + 108;
    const auto navWidth = navControl.getPreferredWidth(), navHeight = navControl.getPreferredHeight();
    const auto buttonsWidth = 92 + 122 + 108 + 3 * 8;
    const auto available = toolbar.getWidth();

    // Was nicht hineinpasst, entfällt: zuerst der Name, dann der Schriftzug
    const auto fixed = logoWidth + navWidth + buttonsWidth + 170 + 5 * 16;
    showLogoText = available >= fixed;
    titleLabel.setVisible (available >= fixed + 190);
    const auto instrumentWidth = juce::jlimit (150, 250, available - (showLogoText ? logoWidth : 62) - navWidth - buttonsWidth - 5 * 16
                                                          - (titleLabel.isVisible() ? 190 : 0));

    audioButton.setBounds (toolbar.removeFromRight (108).withSizeKeepingCentre (108, keyHeight));
    toolbar.removeFromRight (8);
    editorButton.setBounds (toolbar.removeFromRight (122).withSizeKeepingCentre (122, keyHeight));
    toolbar.removeFromRight (8);
    pluginsButton.setBounds (toolbar.removeFromRight (92).withSizeKeepingCentre (92, keyHeight));
    toolbar.removeFromRight (16);
    instrumentBox.setBounds (toolbar.removeFromRight (instrumentWidth).withSizeKeepingCentre (instrumentWidth, cartridgeHeight));
    toolbar.removeFromRight (16);

    toolbar.removeFromLeft (showLogoText ? logoWidth : 62);
    toolbar.removeFromLeft (16);

    if (titleLabel.isVisible())
    {
        titleLabel.setBounds (toolbar.removeFromLeft (170).withSizeKeepingCentre (170, 28));
        toolbar.removeFromLeft (26);
    }

    subtitleLabel.setVisible (false);
    navControl.setBounds (toolbar.withSizeKeepingCentre (navWidth, navHeight).withCentre (toolbar.getCentre()));

    // Unten: Keyboard-Feld mit Quelle, Velocity, Panic, Klaviatur und Statuszeile
    const auto panelHeight = 3 + 14 + 32 + 12 + keyboardHeight + 2 * keyboardFrame + keyboardShadow + 6 + statusStripHeight + 8;
    auto panel = area.removeFromBottom (panelHeight);
    keyboardPanelTop = panel.getY();
    panel.removeFromTop (3 + 14);
    auto sourceRow = panel.removeFromTop (32).reduced (22, 0);
    sourceRow.removeFromLeft (theme::getPlateWidth ("Keyboard & MIDI") + 14);
    panicButton.setBounds (sourceRow.removeFromRight (96).withSizeKeepingCentre (96, keyHeight));
    sourceRow.removeFromRight (16);
    velocityValueLabel.setBounds (sourceRow.removeFromRight (44).withSizeKeepingCentre (44, 28));
    sourceRow.removeFromRight (14);
    velocitySlider.setBounds (sourceRow.removeFromRight (150));
    sourceRow.removeFromRight (8);
    velocityLabel.setBounds (sourceRow.removeFromRight (40));
    sourceRow.removeFromRight (20);
    chordToggle.setBounds (sourceRow.removeFromRight (80));
    sourceRow.removeFromRight (8);
    sourceControl.setBounds (sourceRow.removeFromLeft (sourceControl.getPreferredWidth()).withSizeKeepingCentre (sourceControl.getPreferredWidth(), sourceControl.getPreferredHeight()));
    sourceRow.removeFromLeft (16);
    sourceStatusLabel.setBounds (sourceRow);

    panel.removeFromTop (12);
    keyboard.setBounds (panel.removeFromTop (keyboardHeight + 2 * keyboardFrame + keyboardShadow).reduced (22 + keyboardFrame, keyboardFrame)
                             .withTrimmedBottom (keyboardShadow));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 43.0f);
    panel.removeFromTop (6);
    statusLabel.setBounds (panel.removeFromTop (statusStripHeight).reduced (22, 0));

    // Darüber die Leiste des Bereichs (volle Breite), links die Seitenleiste
    for (auto* bar : { &autosampleView.getBottomBar(), &editorView.getBottomBar(), &exportView.getBottomBar() })
        bar->setBounds (area.removeFromBottom (0));

    if (auto* bar = getCurrentBottomBar())
        bar->setBounds (area.removeFromBottom (bottomBarHeight));

    sidebar.setBounds (area.removeFromLeft (sidebarWidth));
    tabs.setBounds (area);
}

juce::Component* MainComponent::getCurrentBottomBar()
{
    switch (tabs.getCurrentTabIndex())
    {
        case 0:  return &autosampleView.getBottomBar();
        case 1:  return &editorView.getBottomBar();
        case 3:  return &exportView.getBottomBar();
        default: break;
    }

    return nullptr;
}

void MainComponent::updateTab()
{
    const auto index = tabs.getCurrentTabIndex();
    navControl.setSelectedIndex (index, juce::dontSendNotification);

    for (auto* bar : { &autosampleView.getBottomBar(), &editorView.getBottomBar(), &exportView.getBottomBar() })
        bar->setVisible (bar == getCurrentBottomBar());

    sidebar.setContent (index == 0 ? SidebarView::Content::runOverview
                      : index == 1 ? SidebarView::Content::sampleList
                                   : SidebarView::Content::none);
    resized();
    updateKeyboardMarks();
}

void MainComponent::updateSidebarSamples()
{
    std::vector<SidebarView::SampleItem> items;

    for (const auto& tree : session.getSamplesTree())
    {
        const auto sample = session.wrap (tree);
        auto note = getNoteName (sample.getNote());

        if (tree.hasProperty (sessionIds::roundRobin))
            note << " rr" << (sample.getRoundRobin() + 1);

        items.push_back ({ note, "v" + juce::String (sample.getVelocity()), sample.isLoopEnabled(), sample.isLocked() });
    }

    sidebar.setSamples (std::move (items));
    sidebar.setSelectedSample (editorView.getSelectedIndex());
}

void MainComponent::updateKeyboardMarks()
{
    std::map<int, PianoKeyboard::Mark> marks;

    if (tabs.getCurrentTabIndex() == 0)
    {
        const auto overview = autosampleView.getRunOverview();

        for (size_t column = 0; column < overview.columnNotes.size(); ++column)
        {
            auto mark = PianoKeyboard::Mark::done;

            for (const auto& row : overview.cells)
            {
                if (row[column] == SidebarView::Cell::running)
                {
                    mark = PianoKeyboard::Mark::running;
                    break;
                }

                if (row[column] == SidebarView::Cell::open)
                    mark = PianoKeyboard::Mark::open;
            }

            marks[overview.columnNotes[column]] = mark;
        }
    }
    else
    {
        const auto selected = tabs.getCurrentTabIndex() == 1 ? editorView.getSelectedIndex() : -1;
        int index = 0;

        for (const auto& tree : session.getSamplesTree())
        {
            const auto note = session.wrap (tree).getNote();
            auto& mark = marks[note];

            if (index == selected)
                mark = PianoKeyboard::Mark::selected;
            else if (mark != PianoKeyboard::Mark::selected)
                mark = PianoKeyboard::Mark::done;

            ++index;
        }
    }

    keyboard.setMarks (std::move (marks));
}

void MainComponent::parentHierarchyChanged()
{
    updateWindowTitle();
}

void MainComponent::lookAndFeelChanged()
{
    for (int i = 0; i < tabs.getNumTabs(); ++i)
    {
        // Nicht angezeigte Tabs hängen nicht im Baum und bekommen den Wechsel sonst nicht mit
        if (auto* content = tabs.getTabContentComponent (i); content != nullptr && content->getParentComponent() == nullptr)
        {
            theme::refreshColourBindings (*content);
            content->sendLookAndFeelChange();
        }
    }

    // Leisten der nicht angezeigten Bereiche hängen ebenfalls nicht im Baum
    for (auto* bar : { &autosampleView.getBottomBar(), &editorView.getBottomBar(), &exportView.getBottomBar() })
    {
        if (bar->getParentComponent() == nullptr || ! bar->isVisible())
        {
            theme::refreshColourBindings (*bar);
            bar->sendLookAndFeelChange();
        }
    }

    commandManager.commandStatusChanged();
    repaint();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &host.getKnownPluginList())
    {
        refreshInstrumentList();
    }
    else if (source == &session || source == &session.getUndoManager())
    {
        updateWindowTitle();
        updateSidebarSamples();
        updateKeyboardMarks();
        commandManager.commandStatusChanged();
    }
    else
    {
        updatePluginInfo();
    }
}

void MainComponent::refreshInstrumentList()
{
    const auto* current = host.getPlugin();
    const auto currentId = current != nullptr ? current->getPluginDescription().createIdentifierString()
                                              : juce::String();

    instruments = host.getInstruments();
    instrumentBox.clear (juce::dontSendNotification);

    for (int i = 0; i < instruments.size(); ++i)
    {
        const auto& desc = instruments.getReference (i);
        instrumentBox.addItem (desc.name + "  (" + desc.manufacturerName + ", " + desc.pluginFormatName + ")", i + 1);

        if (desc.createIdentifierString() == currentId)
            instrumentBox.setSelectedId (i + 1, juce::dontSendNotification);
    }
}

void MainComponent::loadSelectedInstrument()
{
    const auto index = instrumentBox.getSelectedId() - 1;

    if (! juce::isPositiveAndBelow (index, instruments.size()))
        return;

    const auto desc = instruments[index];
    setStatus ("Loading " + desc.name + "...");

    juce::Component::SafePointer<MainComponent> safeThis (this);

    host.loadPlugin (desc, [safeThis, name = desc.name] (const juce::String& error)
    {
        if (safeThis == nullptr)
            return;

        if (error.isNotEmpty())
        {
            safeThis->setStatus (name + ": " + error, true);
            return;
        }

        safeThis->setStatus (name + " loaded");
        safeThis->openEditor();
        safeThis->keyboard.grabKeyboardFocus();
    });
}

void MainComponent::showPluginList()
{
    if (pluginListWindow == nullptr)
    {
        pluginListWindow = std::make_unique<PluginListWindow> (host, &host.getSettings(), [this]
        {
            // Asynchron schließen, weil der Aufruf aus dem Fenster selbst kommt
            juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
            {
                if (safeThis != nullptr)
                    safeThis->pluginListWindow = nullptr;
            });
        });
    }

    pluginListWindow->toFront (true);
}

void MainComponent::showAboutWindow()
{
    if (aboutOverlay != nullptr)
        return;

    aboutOverlay = std::make_unique<AboutOverlay> ([this]
    {
        // Asynchron schließen, weil der Aufruf aus dem Overlay selbst kommt
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safeThis != nullptr)
                safeThis->aboutOverlay = nullptr;
        });
    });

    addAndMakeVisible (*aboutOverlay);
    aboutOverlay->setBounds (getLocalBounds());
    aboutOverlay->toFront (true);
}

juce::Rectangle<int> MainComponent::getLogoBounds() const
{
    const auto toolbarArea = getCaseBounds().withHeight (toolbarHeight).reduced (caseBorder + 16, 0);
    return juce::Rectangle<int> (toolbarArea.getX(), toolbarArea.getY(), showLogoText ? 62 + 108 : 62, toolbarArea.getHeight());
}

void MainComponent::setLogoHovered (bool hovered)
{
    if (logoHovered == hovered)
        return;

    logoHovered = hovered;
    setMouseCursor (hovered ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint (getLogoBounds().expanded (8));
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    setLogoHovered (aboutOverlay == nullptr && getLogoBounds().contains (e.getPosition()));
}

void MainComponent::mouseExit (const juce::MouseEvent&)
{
    setLogoHovered (false);
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    if (getLogoBounds().contains (e.getPosition()))
    {
        setLogoHovered (false);
        showAboutWindow();
    }
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (host.getDeviceManager(),
                                                                          0, 2,     // Eingänge
                                                                          0, 2,     // Ausgänge
                                                                          true,     // MIDI-Eingänge
                                                                          true,     // MIDI-Ausgang
                                                                          true,     // Stereo-Paare
                                                                          false);
    selector->setSize (520, 560);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio/MIDI settings";
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    if (auto* window = options.launchAsync())
        theme::bindColour (*window, juce::ResizableWindow::backgroundColourId, juce::ResizableWindow::backgroundColourId);
}

void MainComponent::openEditor()
{
    auto* plugin = host.getPlugin();

    if (plugin == nullptr)
    {
        setStatus ("No plugin loaded", true);
        return;
    }

    if (editorWindow == nullptr)
    {
        editorWindow = std::make_unique<PluginWindow> (*plugin, [this]
        {
            juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
            {
                if (safeThis != nullptr)
                    safeThis->closeEditor();
            });
        });
    }

    editorWindow->toFront (true);
}

void MainComponent::closeEditor()
{
    editorWindow = nullptr;
}

void MainComponent::updatePluginInfo()
{
    auto* plugin = host.getPlugin();
    editorButton.setEnabled (plugin != nullptr);

    SidebarView::PluginInfo info;

    if (plugin != nullptr)
    {
        const auto desc = plugin->getPluginDescription();
        info.name = desc.name;
        info.subtitle = desc.manufacturerName + " · "_u + desc.pluginFormatName + (desc.version.isNotEmpty() ? " " + desc.version : juce::String());

        // Tausendertrennung mit schmalem Leerzeichen wie in der Vorlage
        const auto spaced = [] (double value)
        {
            auto text = juce::String (juce::roundToInt (value));
            return text.length() > 3 ? text.dropLastCharacters (3) + juce::String::fromUTF8 ("\xe2\x80\xaf") + text.getLastCharacters (3) : text;
        };

        info.rows = { { "Inputs/Outputs"_u, juce::String (plugin->getTotalNumInputChannels()) + " / " + juce::String (plugin->getTotalNumOutputChannels()) },
                      { "Latency", juce::String (plugin->getLatencySamples()) + " Samples" },
                      { "Tail", juce::String (plugin->getTailLengthSeconds(), 2) + " s" },
                      { "Sample rate", spaced (plugin->getSampleRate()) + " Hz" },
                      { "Block size"_u, juce::String (plugin->getBlockSize()) } };
    }

    sidebar.setPluginInfo (std::move (info));
}

void MainComponent::setSamplingActive (bool active)
{
    // Während eines Laufs darf das Plugin weder gewechselt noch live gespielt werden
    pluginsButton.setEnabled (! active);
    instrumentBox.setEnabled (! active);
    panicButton.setEnabled (! active);
    keyboard.setEnabled (! active);
    sourceControl.setEnabled (! active);

    exportView.setExportAllowed (! active);

    if (active)
        pluginListWindow = nullptr;

    commandManager.commandStatusChanged();
    setStatus (active ? "Sampling in progress ..."_u : "Sampling finished");
}

void MainComponent::updateSourceButtons()
{
    sourceControl.setSelectedIndex (livePlayView.getSource() == PlaySource::plugin ? 0 : 1, juce::dontSendNotification);
    chordToggle.setToggleState (livePlayView.isChordEnabled(), juce::dontSendNotification);
}

//==============================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.addArray ({ newSession, openSession, saveSession, saveSessionAs, exportInstrument, undo, redo,
                         playPlugin, playSamples, toggleSource, panic,
                         appearanceSystem, appearanceLight, appearanceDark });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto running = autosampleView.isRunning();
    auto& undoManager = session.getUndoManager();

    switch (id)
    {
        case newSession:
            info.setInfo ("New session", "Clears the current session", "File", 0);
            info.addDefaultKeypress ('n', juce::ModifierKeys::commandModifier);
            info.setActive (! running);
            break;

        case openSession:
            info.setInfo ("Open..."_u, "Opens a session (.asession)"_u, "File", 0);
            info.addDefaultKeypress ('o', juce::ModifierKeys::commandModifier);
            info.setActive (! running);
            break;

        case saveSession:
            info.setInfo ("Save", "Saves the session", "File", 0);
            info.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier);
            info.setActive (! running && ! session.isEmpty());
            break;

        case saveSessionAs:
            info.setInfo ("Save as...", "Saves the session in a new location", "File", 0);
            info.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            info.setActive (! running && ! session.isEmpty());
            break;

        case exportInstrument:
            info.setInfo ("Export...", "Exports the samples as WAV, SFZ or Elektron Tonverk", "File", 0);
            info.addDefaultKeypress ('e', juce::ModifierKeys::commandModifier);
            info.setActive (! running && ! session.isEmpty() && ! exportView.isExporting());
            break;

        case undo:
            info.setInfo (undoManager.canUndo() ? "Undo: "_u + undoManager.getUndoDescription() : "Undo"_u,
                          "Undoes the last edit"_u, "Edit", 0);
            info.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier);
            info.setActive (undoManager.canUndo());
            break;

        case redo:
            info.setInfo (undoManager.canRedo() ? "Wiederholen: " + undoManager.getRedoDescription() : juce::String ("Wiederholen"),
                          "Redoes the last undone edit"_u, "Edit", 0);
            info.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            info.setActive (undoManager.canRedo());
            break;

        case playPlugin:
            info.setInfo ("Play plugin", "Keyboard and MIDI in play the plugin", "Playback", 0);
            info.addDefaultKeypress ('1', juce::ModifierKeys::commandModifier);
            info.setTicked (livePlayView.getSource() == PlaySource::plugin);
            break;

        case playSamples:
            info.setInfo ("Play samples", "Keyboard and MIDI in play the samples of the session", "Playback", 0);
            info.addDefaultKeypress ('2', juce::ModifierKeys::commandModifier);
            info.setTicked (livePlayView.getSource() == PlaySource::sampler);
            break;

        case toggleSource:
            info.setInfo ("Toggle A/B", "Switches between plugin and samples", "Playback", 0);
            info.addDefaultKeypress ('b', juce::ModifierKeys::commandModifier);
            break;

        case panic:
            info.setInfo ("All notes off", "Panic: all notes off, stop arp and sequencer", "Playback", 0);
            info.addDefaultKeypress ('.', juce::ModifierKeys::commandModifier);
            info.setActive (! running);
            break;

        case appearanceSystem:
            info.setInfo ("Like system", "Light or dark according to the system setting", "View", 0);
            info.setTicked (appearance.getMode() == AppearanceMode::system);
            break;

        case appearanceLight:
            info.setInfo ("Light", "Always light appearance", "View", 0);
            info.setTicked (appearance.getMode() == AppearanceMode::light);
            break;

        case appearanceDark:
            info.setInfo ("Dark", "Always dark appearance", "View", 0);
            info.setTicked (appearance.getMode() == AppearanceMode::dark);
            break;

        case showAbout:
            info.setInfo ("About Autosampler"_u, "Version, author and support"_u, "Help", 0);
            break;

        default:
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case newSession:    confirmDiscardChanges ([this] { newSessionNow(); }); return true;
        case openSession:   confirmDiscardChanges ([this] { chooseSessionToOpen(); }); return true;
        case saveSession:   saveCurrentSession(); return true;
        case saveSessionAs: saveCurrentSessionAs(); return true;
        case exportInstrument: showTab ("export"); exportView.startExport(); return true;
        case undo:          session.getUndoManager().undo(); return true;
        case redo:          session.getUndoManager().redo(); return true;
        case playPlugin:    livePlayView.setSource (PlaySource::plugin); return true;
        case playSamples:   livePlayView.setSource (PlaySource::sampler); return true;
        case panic:         host.allNotesOff(); return true;
        case showAbout:     showAboutWindow(); return true;
        case appearanceSystem: appearance.setMode (AppearanceMode::system); commandManager.commandStatusChanged(); return true;
        case appearanceLight:  appearance.setMode (AppearanceMode::light); commandManager.commandStatusChanged(); return true;
        case appearanceDark:   appearance.setMode (AppearanceMode::dark); commandManager.commandStatusChanged(); return true;

        case toggleSource:
            livePlayView.setSource (livePlayView.getSource() == PlaySource::plugin ? PlaySource::sampler : PlaySource::plugin);
            setStatus (livePlayView.getSource() == PlaySource::plugin ? "Playback: plugin" : "Playback: samples");
            return true;

        default:            return false;
    }
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Playback", "View" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;

    if (index == 0)
    {
        menu.addCommandItem (&commandManager, newSession);
        menu.addCommandItem (&commandManager, openSession);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, saveSession);
        menu.addCommandItem (&commandManager, saveSessionAs);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, exportInstrument);
    }
    else if (index == 1)
    {
        menu.addCommandItem (&commandManager, undo);
        menu.addCommandItem (&commandManager, redo);
    }
    else if (index == 2)
    {
        menu.addCommandItem (&commandManager, playPlugin);
        menu.addCommandItem (&commandManager, playSamples);
        menu.addCommandItem (&commandManager, toggleSource);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, panic);
    }
    else if (index == 3)
    {
        juce::PopupMenu appearanceMenu;
        appearanceMenu.addCommandItem (&commandManager, appearanceSystem);
        appearanceMenu.addCommandItem (&commandManager, appearanceLight);
        appearanceMenu.addCommandItem (&commandManager, appearanceDark);
        menu.addSubMenu ("Appearance", appearanceMenu);
    }

    return menu;
}

//==============================================================================
void MainComponent::confirmDiscardChanges (std::function<void()> proceed)
{
    if (! session.hasUnsavedChanges())
    {
        proceed();
        return;
    }

    const auto name = session.getName().isNotEmpty() ? session.getName() : juce::String ("New session");

    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle ("Unsaved changes"_u)
                             .withMessage ("Save changes to \""_u + name + "\"?")
                             .withButton ("Save")
                             .withButton ("Discard")
                             .withButton ("Cancel")
                             .withAssociatedComponent (this);

    // Drei Knöpfe: 1 = erster, 2 = zweiter, 0 = letzter (Abbrechen)
    juce::AlertWindow::showAsync (options, [safeThis = juce::Component::SafePointer<MainComponent> (this),
                                            then = std::move (proceed)] (int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == 1)
            safeThis->saveCurrentSession (then);
        else if (result == 2)
            then();
    });
}

void MainComponent::newSessionNow()
{
    session.clear();
    setStatus ("New session");
}

void MainComponent::chooseSessionToOpen()
{
    const auto start = session.getPackageFolder() != juce::File()
                           ? session.getPackageFolder().getParentDirectory()
                           : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Autosampler");

    fileChooser = std::make_unique<juce::FileChooser> ("Open session"_u, start, "*.asession;session.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  if (const auto result = chooser.getResult(); result != juce::File())
                                      openSessionFile (result);
                              });
}

void MainComponent::openSessionFile (const juce::File& file)
{
    if (const auto error = session.load (file); error.isNotEmpty())
    {
        setStatus (error, true);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                              "Cannot open session"_u, error),
                                      nullptr);
        return;
    }

    if (const auto settings = session.getSamplingSettings())
        autosampleView.applySettings (*settings);
    tabs.setCurrentTabIndex (1);
    setStatus (session.getName() + " opened ("_u + juce::String (session.getNumSamples()) + " Samples)");
    restoreSessionPlugin();
}

void MainComponent::showTab (const juce::String& name)
{
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (tabs.getTabNames()[i].equalsIgnoreCase (name))
            tabs.setCurrentTabIndex (i);
}

void MainComponent::restoreSessionPlugin()
{
    const auto stored = session.getPluginDescription();

    if (! stored.has_value())
        return;

    // Bevorzugt den Eintrag aus der Plugin-Liste (aktueller Pfad und Version)
    auto desc = *stored;

    if (auto known = host.getKnownPluginList().getTypeForIdentifierString (desc.createIdentifierString()))
        desc = *known;

    const auto applyState = [this, state = session.getPluginState()]
    {
        if (auto* plugin = host.getPlugin(); plugin != nullptr && ! state.isEmpty())
            plugin->setStateInformation (state.getData(), (int) state.getSize());
    };

    if (auto* current = host.getPlugin();
        current != nullptr && current->getPluginDescription().createIdentifierString() == desc.createIdentifierString())
    {
        applyState();
        setStatus (desc.name + ": plugin state of the session restored");
        return;
    }

    setStatus ("Loading " + desc.name + " from the session ...");
    juce::Component::SafePointer<MainComponent> safeThis (this);

    host.loadPlugin (desc, [safeThis, applyState, name = desc.name] (const juce::String& error)
    {
        if (safeThis == nullptr)
            return;

        safeThis->refreshInstrumentList();

        if (error.isNotEmpty())
        {
            safeThis->setStatus (name + " cannot be loaded from the session: " + error, true);
            return;
        }

        applyState();
        safeThis->setStatus (name + " loaded with the plugin state of the session");
    });
}

void MainComponent::saveCurrentSession (std::function<void()> onSaved)
{
    if (session.getPackageFolder() == juce::File())
    {
        saveCurrentSessionAs (std::move (onSaved));
        return;
    }

    if (const auto error = session.save(); error.isNotEmpty())
    {
        setStatus ("Saving failed: " + error, true);
        return;
    }

    setStatus (session.getName() + " saved");

    if (onSaved != nullptr)
        onSaved();
}

void MainComponent::saveCurrentSessionAs (std::function<void()> onSaved)
{
    const auto current = session.getPackageFolder();
    const auto name = session.getName().isNotEmpty() ? session.getName() : juce::String ("Session");
    const auto parent = current != juce::File() ? current.getParentDirectory()
                                                : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Autosampler");

    fileChooser = std::make_unique<juce::FileChooser> ("Save session as", parent.getChildFile (name + Session::packageExtension),
                                                       "*.asession");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, then = std::move (onSaved)] (const juce::FileChooser& chooser)
                              {
                                  const auto result = chooser.getResult();

                                  if (result == juce::File())
                                      return;

                                  if (const auto error = session.saveAs (result); error.isNotEmpty())
                                  {
                                      setStatus ("Saving failed: " + error, true);
                                      return;
                                  }

                                  setStatus (session.getName() + " saved to "
                                             + session.getPackageFolder().getFullPathName());

                                  if (then != nullptr)
                                      then();
                              });
}

void MainComponent::updateWindowTitle()
{
    auto* window = findParentComponentOfClass<juce::DocumentWindow>();

    if (window == nullptr)
        return;

    const auto name = session.getName();
    titleLabel.setText (name.isNotEmpty() ? name : juce::String ("New session"), juce::dontSendNotification);
    window->setName (name.isNotEmpty() ? name + " - " + JUCE_APPLICATION_NAME_STRING
                                       : juce::String (JUCE_APPLICATION_NAME_STRING));

    if (auto* peer = window->getPeer())
    {
        peer->setDocumentEditedStatus (session.hasUnsavedChanges());
        peer->setRepresentedFile (session.getPackageFolder());
    }
}

void MainComponent::setStatus (const juce::String& text, bool isError)
{
    statusLabel.setText (text, juce::dontSendNotification);
    theme::bindColour (statusLabel, juce::Label::textColourId, isError ? theme::error : theme::secondaryText);
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;

    // X = Oktave hoch, Y = Oktave runter (QWERTZ)
    if (key.getTextCharacter() == 'x' || key.getTextCharacter() == 'X')  { setKeyboardOctave (keyboardOctave + 1); return true; }
    if (key.getTextCharacter() == 'y' || key.getTextCharacter() == 'Y')  { setKeyboardOctave (keyboardOctave - 1); return true; }

    return false;
}

void MainComponent::setKeyboardOctave (int octave)
{
    keyboardOctave = juce::jlimit (0, 8, octave);

    // clearKeyMappings lässt gehaltene Noten los, damit beim Wechsel keine hängen bleiben
    keyboard.clearKeyMappings();

    const std::string_view keys { "awsedftgzhujkolp;" };   // G# liegt auf Z, Y steuert die Oktave
    int offset = 0;

    for (const char c : keys)
        keyboard.setKeyPressForNote ({ c, 0, 0 }, offset++);

    keyboard.setKeyPressBaseOctave (keyboardOctave);
    setStatus ("Keyboard octave " + juce::String (keyboardOctave) + " (C" + juce::String (keyboardOctave - 1) + "), X up, Y down");
}

} // namespace autosampler
