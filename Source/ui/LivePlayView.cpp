#include "LivePlayView.h"
#include "Theme.h"

namespace autosampler
{

namespace
{
    constexpr const char* settingsKey = "livePlay";
    constexpr int knobWidth = 72, knobHeight = 74, comboHeight = 24, rowGap = 6;

    constexpr ChordType chordTypes[] { ChordType::off, ChordType::major, ChordType::minor, ChordType::dominant7,
                                       ChordType::major7, ChordType::minor7, ChordType::sus2, ChordType::sus4,
                                       ChordType::diminished, ChordType::augmented, ChordType::power, ChordType::octave };
    constexpr PatternMode patternModes[] { PatternMode::off, PatternMode::arp, PatternMode::sequencer };
    constexpr ArpMode arpModes[] { ArpMode::up, ArpMode::down, ArpMode::upDown, ArpMode::random, ArpMode::asPlayed };
    constexpr StepRate stepRates[] { StepRate::quarter, StepRate::eighth, StepRate::eighthTriplet,
                                     StepRate::sixteenth, StepRate::sixteenthTriplet, StepRate::thirtySecond };

    template <typename Array, typename Value>
    int indexOf (const Array& array, Value value)
    {
        return (int) std::distance (std::begin (array), std::find (std::begin (array), std::end (array), value));
    }

    template <typename Array>
    void fillBox (juce::ComboBox& box, const Array& values)
    {
        for (size_t i = 0; i < std::size (values); ++i)
            box.addItem (getDisplayName (values[i]), (int) i + 1);
    }

    template <typename Array, typename Value>
    void readBox (const juce::ComboBox& box, const Array& values, Value& target)
    {
        if (const auto index = box.getSelectedId() - 1; juce::isPositiveAndBelow (index, (int) std::size (values)))
            target = values[(size_t) index];
    }
}

//==============================================================================
/** Raster der Sequencer-Schritte: Klick schaltet einen Schritt an/aus, Ziehen
    nach oben/unten ändert die Transposition, mit Umschalt die Velocity,
    Doppelklick setzt zurück. Der laufende Schritt ist hervorgehoben. */
class LivePlayView::StepGrid final : public juce::Component,
                                     public juce::SettableTooltipClient
{
public:
    StepGrid (PerformSettings& settingsToEdit, std::function<void()> onChangeCallback)
        : settings (settingsToEdit), onChange (std::move (onChangeCallback))
    {
        setTooltip ("Click: step on/off. Drag: transposition in semitones, with Shift: velocity. Double-click: reset"_u);
    }

    void setCurrentStep (int step)
    {
        if (step != currentStep)
        {
            currentStep = step;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < PerformSettings::maxSteps; ++i)
        {
            const auto cell = getCell (i).toFloat().reduced (2.0f);
            const auto& step = settings.steps[(size_t) i];
            const auto inRange = i < settings.sequencerLength;
            const auto alpha = inRange ? 1.0f : 0.3f;

            g.setColour (findColour (theme::card).withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (cell, 4.0f);
            g.setColour (findColour (theme::cardOutline).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (cell, 4.0f, 1.0f);

            if (step.active)
            {
                // Velocity als Füllhöhe
                const auto height = cell.getHeight() * (float) step.velocity / 127.0f;
                g.setColour (findColour (theme::accent).withAlpha (0.75f * alpha));
                g.fillRoundedRectangle (cell.withTop (cell.getBottom() - height), 4.0f);
            }

            if (i == currentStep && settings.pattern == PatternMode::sequencer)
            {
                g.setColour (findColour (theme::playhead));
                g.drawRoundedRectangle (cell, 4.0f, 2.0f);
            }

            g.setColour (findColour (theme::primaryText).withMultipliedAlpha (alpha * (step.active ? 1.0f : 0.5f)));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (step.offset > 0 ? "+" + juce::String (step.offset) : juce::String (step.offset),
                        cell.withTrimmedBottom (cell.getHeight() * 0.5f), juce::Justification::centred);

            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (i + 1), cell.withTrimmedTop (cell.getHeight() - 16.0f), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragIndex = getIndexAt (e.getPosition());
        dragged = false;

        if (dragIndex >= 0)
        {
            startOffset = settings.steps[(size_t) dragIndex].offset;
            startVelocity = settings.steps[(size_t) dragIndex].velocity;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragIndex < 0 || std::abs (e.getDistanceFromDragStartY()) < 3)
            return;

        dragged = true;
        auto& step = settings.steps[(size_t) dragIndex];
        const auto dy = -e.getDistanceFromDragStartY();

        if (e.mods.isShiftDown())
            step.velocity = juce::jlimit (1, 127, startVelocity + dy);
        else
            step.offset = juce::jlimit (-24, 24, startOffset + dy / 6);

        changed();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragIndex >= 0 && ! dragged)
        {
            auto& step = settings.steps[(size_t) dragIndex];
            step.active = ! step.active;
            changed();
        }

        dragIndex = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (const auto index = getIndexAt (e.getPosition()); index >= 0)
        {
            settings.steps[(size_t) index] = {};
            changed();
        }
    }

private:
    juce::Rectangle<int> getCell (int index) const
    {
        const auto width = getWidth() / PerformSettings::maxSteps;
        return { index * width, 0, width, getHeight() };
    }

    int getIndexAt (juce::Point<int> position) const
    {
        const auto width = juce::jmax (1, getWidth() / PerformSettings::maxSteps);
        const auto index = position.x / width;
        return juce::isPositiveAndBelow (index, PerformSettings::maxSteps) ? index : -1;
    }

    void changed()
    {
        repaint();

        if (onChange != nullptr)
            onChange();
    }

    PerformSettings& settings;
    std::function<void()> onChange;
    int currentStep = -1, dragIndex = -1, startOffset = 0, startVelocity = 100;
    bool dragged = false;
};

//==============================================================================
LivePlayView::LivePlayView (PluginHost& hostToUse)
    : host (hostToUse)
{
    if (auto xml = host.getSettings().getXmlValue (settingsKey))
        settings = LivePlaySettings::fromValueTree (juce::ValueTree::fromXml (*xml));

    for (auto* c : std::initializer_list<juce::Component*> { &samplerGroup, &chordGroup, &reverbGroup, &patternGroup,
                                                             &oneShotLabel, &oneShotBox, &samplerStatus, &voicesLabel, &midiLabel,
                                                             &chordLabel, &chordBox, &inversionLabel, &inversionBox, &reverbButton,
                                                             &patternLabel, &patternBox, &rateLabel, &rateBox, &arpModeLabel,
                                                             &arpModeBox, &octavesLabel, &octavesBox, &latchButton,
                                                             &resetStepsButton, &sequencerHint })
        addAndMakeVisible (c);

    setupKnob (attackKnob, "Attack", 0.0, 2000.0, 1.0, " ms", "Attack time of the envelope"_u);
    setupKnob (releaseKnob, "Release", 1.0, 5000.0, 1.0, " ms", "Release time after note-off (the loop keeps running)"_u);
    setupKnob (velocityKnob, "Velocity", 0.0, 100.0, 1.0, " %", "How strongly the volume follows the velocity within a velocity layer"_u);
    setupKnob (gainKnob, "Level", -24.0, 12.0, 0.1, " dB", "Volume of the samples"_u);
    setupKnob (voicesKnob, "Voices", 1.0, 128.0, 1.0, {}, "Polyphony; beyond it the oldest voice fades out in 5 ms"_u);
    setupKnob (mixKnob, "Mix", 0.0, 100.0, 1.0, " %", "Amount of reverb (for monitoring only, never in the export)"_u);
    setupKnob (sizeKnob, "Size"_u, 0.0, 100.0, 1.0, " %", "Room size"_u);
    setupKnob (dampingKnob, "Damping"_u, 0.0, 100.0, 1.0, " %", "High-frequency damping of the reverb"_u);
    setupKnob (bpmKnob, "Tempo", 20.0, 300.0, 1.0, " BPM", "Tempo of arp and sequencer (also the plugin's playhead)"_u);
    setupKnob (gateKnob, "Gate", 5.0, 100.0, 1.0, " %", "Note length as a fraction of a step"_u);
    setupKnob (lengthKnob, "Steps", 1.0, PerformSettings::maxSteps, 1.0, {}, "Length of the sequence"_u);
    attackKnob.slider.setSkewFactorFromMidPoint (100.0);
    releaseKnob.slider.setSkewFactorFromMidPoint (500.0);

    oneShotBox.addItem ("One Shot", 1);
    oneShotBox.addItem ("Release on note-off", 2);
    oneShotBox.setTooltip ("Samples without loop: play to the end (note-off is ignored) or fade out on release"_u);

    fillBox (chordBox, chordTypes);
    inversionBox.addItemList ({ "Root position", "1st inversion", "2nd inversion", "3rd inversion" }, 1);
    chordBox.setTooltip ("Every key plays the chord on this root note (also for arp and sequencer)"_u);
    inversionBox.setTooltip ("Inversion of the chord: the lowest notes move up an octave"_u);
    reverbButton.setTooltip ("Reverb for monitoring plugin and samples only; not part of recording or export"_u);
    rateBox.setTooltip ("Note value of a step or arp note"_u);
    arpModeBox.setTooltip ("Order in which the arpeggiator plays the held notes"_u);
    octavesBox.setTooltip ("Over how many octaves the arpeggiator runs"_u);
    resetStepsButton.setTooltip ("All steps on, no transposition, velocity 100"_u);
    midiLabel.setTooltip ("Active MIDI inputs; select under Audio/MIDI..."_u);

    fillBox (patternBox, patternModes);
    fillBox (rateBox, stepRates);
    fillBox (arpModeBox, arpModes);
    octavesBox.addItemList ({ "1", "2", "3", "4" }, 1);
    latchButton.setTooltip ("Notes keep sounding after release until a new key is pressed"_u);
    patternBox.setTooltip ("Arpeggiator: plays the held notes one after another. Step sequencer: plays the steps, "
                           "transposed by the pressed key (C3 = as written)."_u);

    for (auto* label : { &oneShotLabel, &chordLabel, &inversionLabel, &patternLabel, &rateLabel, &arpModeLabel, &octavesLabel })
        label->setJustificationType (juce::Justification::centredLeft);

    for (auto* box : { &oneShotBox, &chordBox, &inversionBox, &patternBox, &rateBox, &arpModeBox, &octavesBox })
        box->onChange = [this] { storeSettings(); };

    for (auto* button : { &reverbButton, &latchButton })
        button->onClick = [this] { storeSettings(); };

    theme::bindColour (samplerStatus, juce::Label::textColourId, theme::secondaryText);
    samplerStatus.setMinimumHorizontalScale (0.7f);
    theme::bindColour (voicesLabel, juce::Label::textColourId, theme::tertiaryText);
    theme::bindColour (midiLabel, juce::Label::textColourId, theme::tertiaryText);
    midiLabel.setMinimumHorizontalScale (0.7f);
    theme::bindColour (sequencerHint, juce::Label::textColourId, theme::tertiaryText);
    sequencerHint.setText ("Sequencer: hold a key (or Latch), C3 plays the steps as written", juce::dontSendNotification);

    resetStepsButton.onClick = [this]
    {
        settings.perform.steps = {};
        stepGrid->repaint();
        storeSettings();
    };

    stepGrid = std::make_unique<StepGrid> (settings.perform, [this] { storeSettings(); });
    addAndMakeVisible (*stepGrid);

    updateControls();
    host.getLivePlay().setSettings (settings);
    midiText = getMidiInputText();
    midiLabel.setText (midiText, juce::dontSendNotification);
    startTimerHz (15);
}

LivePlayView::~LivePlayView()
{
    stopTimer();
}

void LivePlayView::setupKnob (Knob& knob, const juce::String& name, double min, double max, double interval,
                              const juce::String& suffix, const juce::String& tooltip)
{
    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (12.0f));
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth - 4, 16);
    knob.slider.setRange (min, max, interval);
    knob.slider.setTextValueSuffix (suffix);
    knob.slider.setTooltip (tooltip);
    knob.slider.onValueChange = [this] { storeSettings(); };
    addAndMakeVisible (knob.label);
    addAndMakeVisible (knob.slider);
}

//==============================================================================
void LivePlayView::setSource (PlaySource source)
{
    if (settings.source == source)
        return;

    settings.source = source;
    storeSettings();
}

void LivePlayView::setChordEnabled (bool enabled)
{
    if (enabled == isChordEnabled())
        return;

    chordBox.setSelectedId (indexOf (chordTypes, enabled ? lastChord : ChordType::off) + 1, juce::dontSendNotification);
    storeSettings();
}

void LivePlayView::setSamplerStatus (const juce::String& text)
{
    samplerStatus.setText (text, juce::dontSendNotification);
}

void LivePlayView::updateControls()
{
    const auto& s = settings.sampler;
    attackKnob.slider.setValue (s.attackMs, juce::dontSendNotification);
    releaseKnob.slider.setValue (s.releaseMs, juce::dontSendNotification);
    velocityKnob.slider.setValue (s.velocitySensitivity * 100.0, juce::dontSendNotification);
    gainKnob.slider.setValue (s.gainDb, juce::dontSendNotification);
    voicesKnob.slider.setValue (s.maxVoices, juce::dontSendNotification);
    oneShotBox.setSelectedId (s.oneShotWithoutLoop ? 1 : 2, juce::dontSendNotification);

    const auto& p = settings.perform;
    chordBox.setSelectedId (indexOf (chordTypes, p.chord) + 1, juce::dontSendNotification);
    inversionBox.setSelectedId (p.inversion + 1, juce::dontSendNotification);
    patternBox.setSelectedId (indexOf (patternModes, p.pattern) + 1, juce::dontSendNotification);
    rateBox.setSelectedId (indexOf (stepRates, p.rate) + 1, juce::dontSendNotification);
    arpModeBox.setSelectedId (indexOf (arpModes, p.arpMode) + 1, juce::dontSendNotification);
    octavesBox.setSelectedId (p.arpOctaves, juce::dontSendNotification);
    bpmKnob.slider.setValue (p.bpm, juce::dontSendNotification);
    gateKnob.slider.setValue (p.gate * 100.0, juce::dontSendNotification);
    lengthKnob.slider.setValue (p.sequencerLength, juce::dontSendNotification);
    latchButton.setToggleState (p.latch, juce::dontSendNotification);

    const auto& r = settings.reverb;
    reverbButton.setToggleState (r.enabled, juce::dontSendNotification);
    mixKnob.slider.setValue (r.mix * 100.0, juce::dontSendNotification);
    sizeKnob.slider.setValue (r.size * 100.0, juce::dontSendNotification);
    dampingKnob.slider.setValue (r.damping * 100.0, juce::dontSendNotification);

    // Nur zeigen, was zum Modus gehört
    const auto isArp = p.pattern == PatternMode::arp;
    const auto isSequencer = p.pattern == PatternMode::sequencer;
    const auto patternOn = p.pattern != PatternMode::off;

    for (auto* c : std::initializer_list<juce::Component*> { &arpModeLabel, &arpModeBox, &octavesLabel, &octavesBox })
        c->setEnabled (isArp);

    for (auto* c : std::initializer_list<juce::Component*> { &lengthKnob.label, &lengthKnob.slider, &resetStepsButton, stepGrid.get(),
                                                             &sequencerHint })
        if (c != nullptr)
            c->setEnabled (isSequencer);

    for (auto* c : std::initializer_list<juce::Component*> { &rateLabel, &rateBox, &bpmKnob.label, &bpmKnob.slider,
                                                             &gateKnob.label, &gateKnob.slider, &latchButton })
        c->setEnabled (patternOn);

    for (auto* c : std::initializer_list<juce::Component*> { &mixKnob.slider, &sizeKnob.slider, &dampingKnob.slider })
        c->setEnabled (r.enabled);

    inversionBox.setEnabled (p.chord != ChordType::off && p.chord != ChordType::octave);

    if (stepGrid != nullptr)
        stepGrid->repaint();
}

void LivePlayView::storeSettings()
{
    auto& s = settings.sampler;
    s.attackMs = (float) attackKnob.slider.getValue();
    s.releaseMs = (float) releaseKnob.slider.getValue();
    s.velocitySensitivity = (float) (velocityKnob.slider.getValue() / 100.0);
    s.gainDb = (float) gainKnob.slider.getValue();
    s.maxVoices = (int) voicesKnob.slider.getValue();
    s.oneShotWithoutLoop = oneShotBox.getSelectedId() != 2;

    auto& p = settings.perform;
    readBox (chordBox, chordTypes, p.chord);

    if (p.chord != ChordType::off)
        lastChord = p.chord;

    readBox (patternBox, patternModes, p.pattern);
    readBox (rateBox, stepRates, p.rate);
    readBox (arpModeBox, arpModes, p.arpMode);
    p.inversion = juce::jlimit (0, 3, inversionBox.getSelectedId() - 1);
    p.arpOctaves = juce::jlimit (1, 4, octavesBox.getSelectedId());
    p.bpm = bpmKnob.slider.getValue();
    p.gate = (float) (gateKnob.slider.getValue() / 100.0);
    p.sequencerLength = (int) lengthKnob.slider.getValue();
    p.latch = latchButton.getToggleState();

    auto& r = settings.reverb;
    r.enabled = reverbButton.getToggleState();
    r.mix = (float) (mixKnob.slider.getValue() / 100.0);
    r.size = (float) (sizeKnob.slider.getValue() / 100.0);
    r.damping = (float) (dampingKnob.slider.getValue() / 100.0);

    host.getLivePlay().setSettings (settings);

    // PropertiesFile speichert verzögert, häufige Änderungen sind unkritisch
    if (auto xml = settings.toValueTree().createXml())
        host.getSettings().setValue (settingsKey, xml.get());

    updateControls();

    if (onSettingsChanged != nullptr)
        onSettingsChanged();
}

//==============================================================================
juce::String LivePlayView::getMidiInputText() const
{
    juce::StringArray names;

    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (host.getDeviceManager().isMidiInputDeviceEnabled (device.identifier))
            names.add (device.name);

    return names.isEmpty() ? "MIDI in: none active (Audio/MIDI...)"_u
                           : "MIDI in: " + names.joinIntoString (", ");
}

void LivePlayView::timerCallback()
{
    stepGrid->setCurrentStep (host.getLivePlay().getCurrentStep());

    const auto voices = host.getLivePlay().getNumActiveVoices();
    voicesLabel.setText (voices > 0 ? juce::String (voices) + (voices == 1 ? " voice" : " voices") : juce::String(),
                         juce::dontSendNotification);

    // Geräteliste nur selten abfragen
    if (++timerTicks % 30 == 0)
        if (const auto text = getMidiInputText(); text != midiText)
        {
            midiText = text;
            midiLabel.setText (midiText, juce::dontSendNotification);
        }
}

void LivePlayView::paint (juce::Graphics& g)
{
    g.fillAll (findColour (theme::canvas));
}

void LivePlayView::layoutKnobs (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs)
{
    for (auto* knob : knobs)
    {
        auto column = area.removeFromLeft (knobWidth);
        knob->label.setBounds (column.removeFromTop (16));
        knob->slider.setBounds (column.removeFromTop (knobHeight - 16));
        area.removeFromLeft (4);
    }
}

void LivePlayView::resized()
{
    auto area = getLocalBounds().reduced (28, 20);

    auto top = area.removeFromTop (214);
    area.removeFromTop (rowGap);

    // Sampler links, Chord und Reverb rechts
    auto samplerArea = top.removeFromLeft (juce::jmax (420, top.getWidth() - 340));
    top.removeFromLeft (rowGap);
    samplerGroup.setBounds (samplerArea);
    samplerArea = samplerArea.reduced (14, 0).withTrimmedTop (30).withTrimmedBottom (8);

    layoutKnobs (samplerArea.removeFromTop (knobHeight), { &attackKnob, &releaseKnob, &velocityKnob, &gainKnob, &voicesKnob });

    samplerArea.removeFromTop (4);
    auto oneShotRow = samplerArea.removeFromTop (comboHeight);
    oneShotLabel.setBounds (oneShotRow.removeFromLeft (72));
    oneShotBox.setBounds (oneShotRow.removeFromLeft (180));
    oneShotRow.removeFromLeft (12);
    voicesLabel.setBounds (oneShotRow);

    samplerArea.removeFromTop (4);
    samplerStatus.setBounds (samplerArea.removeFromTop (18));
    midiLabel.setBounds (samplerArea.removeFromTop (18));

    auto chordArea = top.removeFromTop (76);
    top.removeFromTop (4);
    chordGroup.setBounds (chordArea);
    chordArea = chordArea.reduced (14, 0).withTrimmedTop (24).withTrimmedBottom (4);
    chordArea = chordArea.withSizeKeepingCentre (chordArea.getWidth(), comboHeight);
    chordLabel.setBounds (chordArea.removeFromLeft (52));
    chordBox.setBounds (chordArea.removeFromLeft (120));
    chordArea.removeFromLeft (8);
    inversionBox.setBounds (chordArea);
    inversionLabel.setVisible (false);

    reverbGroup.setBounds (top);
    auto reverbArea = top.reduced (14, 0).withTrimmedTop (28).withTrimmedBottom (6);
    reverbButton.setBounds (reverbArea.removeFromLeft (78).withSizeKeepingCentre (78, 24));
    layoutKnobs (reverbArea, { &mixKnob, &sizeKnob, &dampingKnob });

    // Pattern unten über die ganze Breite
    patternGroup.setBounds (area);
    auto patternArea = area.reduced (14, 0).withTrimmedTop (30).withTrimmedBottom (8);
    auto controls = patternArea.removeFromTop (knobHeight);

    const auto placeCombo = [&controls] (juce::Label& label, juce::ComboBox& box, int width)
    {
        auto column = controls.removeFromLeft (width);
        label.setBounds (column.removeFromTop (18));
        box.setBounds (column.removeFromTop (comboHeight));
        controls.removeFromLeft (8);
    };

    placeCombo (patternLabel, patternBox, 140);
    placeCombo (rateLabel, rateBox, 80);
    layoutKnobs (controls.removeFromLeft (2 * (knobWidth + 4)), { &bpmKnob, &gateKnob });
    controls.removeFromLeft (4);
    latchButton.setBounds (controls.removeFromLeft (70).removeFromTop (18 + comboHeight).withTrimmedTop (18));
    placeCombo (arpModeLabel, arpModeBox, 92);
    placeCombo (octavesLabel, octavesBox, 64);
    layoutKnobs (controls.removeFromLeft (knobWidth + 4), { &lengthKnob });

    patternArea.removeFromTop (4);
    auto hintRow = patternArea.removeFromBottom (24);
    resetStepsButton.setBounds (hintRow.removeFromRight (170).reduced (0, 1));
    sequencerHint.setBounds (hintRow);
    patternArea.removeFromBottom (4);
    stepGrid->setBounds (patternArea.withHeight (juce::jmin (patternArea.getHeight(), 120)));
}

} // namespace autosampler
