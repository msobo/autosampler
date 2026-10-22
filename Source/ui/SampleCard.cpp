#include "SampleCard.h"
#include "Text.h"
#include "Theme.h"

namespace autosampler
{

namespace
{
    constexpr int headerHeight = 40, footerHeight = 32, markerHitDistance = 6, flagHeight = 14;
    constexpr juce::uint32 nudgeGroupMs = 1000;

    juce::String formatSeconds (int frames, double sampleRate)
    {
        return juce::String (sampleRate > 0.0 ? frames / sampleRate : 0.0, 3) + " s";
    }

    juce::Colour getScoreColour (float score)
    {
        if (score >= 80.0f) return juce::Colour (0xff30d158);
        if (score >= 60.0f) return juce::Colour (0xffffd60a);
        return juce::Colour (0xffff6961);
    }

    juce::Font makeFont (float cssSize, bool bold = false)
    {
        juce::Font font { juce::FontOptions (theme::fontSize (cssSize)) };
        return bold ? font.boldened() : font;
    }
}

SampleCard::SampleCard (Session& sessionToUse, const juce::ValueTree& sampleTree,
                        juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache& cache)
    : session (sessionToUse),
      sample (session.wrap (sampleTree)),
      listenedTree (sampleTree),
      thumbnail (256, formatManager, cache)
{
    addAndMakeVisible (playButton);
    addAndMakeVisible (autoButton);
    addAndMakeVisible (loopButton);
    addAndMakeVisible (lockButton);

    setWantsKeyboardFocus (true);

    theme::setIcon (playButton, theme::Icon::play);
    theme::setIcon (loopButton, theme::Icon::loop);
    theme::setIcon (lockButton, theme::Icon::lock);
    theme::setChipToggle (lockButton, true);
    loopButton.setClickingTogglesState (true);
    lockButton.setClickingTogglesState (true);

    for (auto* button : { &playButton, &autoButton, &loopButton, &lockButton })
        theme::setCornerSize (*button, 6.0f);

    playButton.setTooltip ("Hold down (or space bar): listen to the sample with loop and crossfade"_u);
    playButton.onStateChange = [this]
    {
        if (playButton.isDown() && ! auditioning)
            startAudition();
        else if (! playButton.isDown() && auditioning && ! auditionBySpace)
            stopAudition();
    };

    autoButton.setTooltip ("Find a loop with the settings above");
    loopButton.setTooltip ("Loop between markers S and E");
    lockButton.setTooltip ("Locked samples cannot be edited and stay unchanged by the loop search"_u);

    autoButton.onClick = [this]
    {
        if (onFindLoop != nullptr)
            onFindLoop (sample.getTree());
    };

    loopButton.onClick = [this]
    {
        const auto enable = loopButton.getToggleState();
        session.getUndoManager().beginNewTransaction (enable ? "Loop on" : "Loop off");
        sample.setLoopEnabled (enable);
        updateButtons();

        // Beim ersten Einschalten gleich einen Loop suchen
        if (enable && ! sample.getLoopAlgorithm().has_value() && onFindLoop != nullptr)
            onFindLoop (sample.getTree());
    };

    lockButton.onClick = [this]
    {
        session.getUndoManager().beginNewTransaction (lockButton.getToggleState() ? "Lock" : "Unlock");
        sample.setLocked (lockButton.getToggleState());
    };

    if (const auto file = session.getSampleFile (sampleTree); file.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (file));
    else
        fileMissing = true;

    thumbnail.addChangeListener (this);
    listenedTree.addListener (this);
    updateButtons();

    setTooltip (sample.getFileName() + "\n"
                + "Click a marker, then move it with the arrow keys: 1 frame, with Shift 1 ms, with Option 10 ms"_u);
}

SampleCard::~SampleCard()
{
    if (auditioning)
        stopAudition();

    listenedTree.removeListener (this);
    thumbnail.removeChangeListener (this);
}

int SampleCard::getHeightForWaveform (int waveformHeight) noexcept
{
    return headerHeight + waveformHeight + footerHeight;
}

void SampleCard::setSelected (bool shouldBeSelected)
{
    if (selectedCard == shouldBeSelected)
        return;

    selectedCard = shouldBeSelected;
    repaint();
}

void SampleCard::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree == listenedTree)
    {
        updateButtons();
        repaint();
    }
}

void SampleCard::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}

void SampleCard::setAudition (SampleAudition* newAudition)
{
    if (auditioning)
        stopAudition();

    audition = newAudition;
    updateButtons();
}

void SampleCard::startAudition()
{
    if (audition == nullptr || fileMissing)
        return;

    audition->startAudition (makeSampleId (sample.getFileName()), sample.getVelocity());
    auditioning = true;
    startTimerHz (30);
}

void SampleCard::stopAudition()
{
    auditioning = auditionBySpace = false;

    if (audition != nullptr)
        audition->stopAudition();
}

void SampleCard::timerCallback()
{
    const auto position = audition != nullptr ? audition->getAuditionPosition (makeSampleId (sample.getFileName())) : -1;

    if (position != playPosition)
    {
        playPosition = position;
        repaint (getWaveformArea());
    }

    // Weiterlaufen, bis das Release ausgeklungen ist
    if (! auditioning && position < 0)
        stopTimer();
}

void SampleCard::updateButtons()
{
    const auto locked = sample.isLocked();
    const auto loopable = sample.getNumFrames() >= SessionSample::minLoopLength;

    loopButton.setToggleState (sample.isLoopEnabled(), juce::dontSendNotification);
    lockButton.setToggleState (locked, juce::dontSendNotification);
    loopButton.setEnabled (! locked && loopable);
    autoButton.setEnabled (! locked && loopable && ! fileMissing);
    playButton.setEnabled (audition != nullptr && ! fileMissing);

    if (! canEdit())
        selected = Marker::none;
}

bool SampleCard::canEdit() const
{
    return sample.isLoopEnabled() && ! sample.isLocked();
}

//==============================================================================
juce::Rectangle<int> SampleCard::getWaveformArea() const
{
    return getLocalBounds().withTrimmedTop (headerHeight).withTrimmedBottom (footerHeight);
}

float SampleCard::frameToX (int frame) const
{
    const auto area = getWaveformArea().toFloat();
    const auto numFrames = juce::jmax (1, sample.getNumFrames());
    return area.getX() + area.getWidth() * (float) frame / (float) numFrames;
}

int SampleCard::xToFrame (float x) const
{
    const auto area = getWaveformArea().toFloat();
    const auto proportion = juce::jlimit (0.0f, 1.0f, (x - area.getX()) / juce::jmax (1.0f, area.getWidth()));
    return juce::roundToInt (proportion * (float) sample.getNumFrames());
}

int SampleCard::getMarkerFrame (Marker marker) const
{
    switch (marker)
    {
        case Marker::start:     return sample.getLoopStart();
        case Marker::end:       return sample.getLoopEnd();
        case Marker::crossfade: return sample.getLoopEnd() - sample.getCrossfade();
        case Marker::none:      break;
    }

    return 0;
}

void SampleCard::moveMarker (Marker marker, int frame)
{
    switch (marker)
    {
        case Marker::start:     sample.setLoopStart (frame); break;
        case Marker::end:       sample.setLoopEnd (frame); break;
        case Marker::crossfade: sample.setCrossfade (sample.getLoopEnd() - frame); break;
        case Marker::none:      break;
    }
}

SampleCard::Marker SampleCard::getMarkerAt (juce::Point<float> position) const
{
    const auto area = getWaveformArea().toFloat();

    if (! canEdit() || ! area.expanded (markerHitDistance, 0).contains (position))
        return Marker::none;

    // X sitzt in der unteren Hälfte und gewinnt dort bei Gleichstand (auch bei Crossfade 0 auf E)
    const auto inLowerHalf = position.y > area.getCentreY();
    auto best = Marker::none;
    auto bestDistance = (float) markerHitDistance;

    for (const auto marker : { Marker::start, Marker::end, Marker::crossfade })
    {
        if (marker == Marker::crossfade && ! inLowerHalf)
            continue;

        const auto distance = std::abs (position.x - frameToX (getMarkerFrame (marker)));

        if (distance < bestDistance || (marker == Marker::crossfade && distance <= bestDistance))
        {
            best = marker;
            bestDistance = distance;
        }
    }

    return best;
}

//==============================================================================
void SampleCard::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto locked = sample.isLocked();

    const auto loopColour = findColour (theme::accent);
    const auto crossfadeColour = findColour (theme::crossfade);
    const auto waveColour = findColour (theme::waveform);
    const auto disabledColour = findColour (theme::tertiaryText);

    juce::Path cardShape;
    cardShape.addRoundedRectangle (bounds, 10.0f);
    g.setColour (findColour (theme::card));
    g.fillPath (cardShape);

    g.saveState();
    g.reduceClipRegion (cardShape);

    // Kopfzeile: Note, Velocity und Score
    auto header = getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (12);
    header.setRight (playButton.getX() - 8);
    auto title = getNoteName (sample.getNote());
    auto detail = " v" + juce::String (sample.getVelocity());

    if (sample.getTree().hasProperty (sessionIds::roundRobin))
        detail << "  rr" << (sample.getRoundRobin() + 1);

    if (const auto variation = sample.getVariation(); variation >= 0)
        detail << "  (" << variation << ")";

    const auto titleFont = makeFont (14.0f, true);
    const auto titleWidth = juce::GlyphArrangement::getStringWidthInt (titleFont, title);
    g.setColour (findColour (theme::primaryText));
    g.setFont (titleFont);
    g.drawText (title, header.removeFromLeft (titleWidth + 2), juce::Justification::centredLeft);

    const auto detailFont = makeFont (12.0f);
    const auto detailWidth = juce::GlyphArrangement::getStringWidthInt (detailFont, detail);
    g.setColour (findColour (theme::secondaryText));
    g.setFont (detailFont);
    g.drawText (detail, header.removeFromLeft (detailWidth + 4), juce::Justification::centredLeft);

    if (const auto score = sample.getLoopScore(); sample.isLoopEnabled() && score >= 0.0f)
    {
        const auto badge = header.withTrimmedLeft (6).withWidth (30).reduced (0, 11).toFloat();

        if (badge.getRight() <= (float) header.getRight())
        {
            g.setColour (getScoreColour (score));
            g.fillRoundedRectangle (badge, 4.0f);
            g.setColour (juce::Colour (0xff1d1d1f));
            g.setFont (makeFont (11.0f, true));
            g.drawText (juce::String (juce::roundToInt (score)), badge, juce::Justification::centred);
        }
    }

    const auto area = getWaveformArea();
    g.setColour (findColour (theme::waveformBackground));
    g.fillRect (area);
    g.setColour (findColour (theme::separator));
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
    g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);

    if (fileMissing)
    {
        g.setColour (findColour (theme::error));
        g.setFont (makeFont (13.0f));
        g.drawText ("File missing: " + sample.getFileName(), area.reduced (4), juce::Justification::centred);
    }
    else if (thumbnail.getTotalLength() > 0.0)
    {
        g.setColour (locked ? waveColour.withAlpha (0.6f) : waveColour);
        thumbnail.drawChannels (g, area.reduced (6, 16), 0.0, thumbnail.getTotalLength(), 1.0f);
    }
    else
    {
        g.setColour (disabledColour);
        g.setFont (makeFont (13.0f));
        g.drawText ("Loading ...", area, juce::Justification::centred);
    }

    // Loop-Bereich mit Markern S, E und X
    if (sample.isLoopEnabled())
    {
        const auto colour = locked ? disabledColour : loopColour;
        const auto xColour = locked ? disabledColour : crossfadeColour;
        const auto selectedColour = findColour (theme::playhead);
        const auto startX = frameToX (sample.getLoopStart());
        const auto endX = frameToX (sample.getLoopEnd());
        const auto crossfade = sample.getCrossfade();
        const auto top = (float) area.getY(), bottom = (float) area.getBottom();

        g.setColour (locked ? disabledColour.withAlpha (0.12f) : findColour (theme::listSelection));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (startX, top, endX, bottom));

        if (crossfade > 0)
        {
            // Crossfade vor E und das eingeblendete Material vor S
            const auto fadeX = frameToX (sample.getLoopEnd() - crossfade);
            const auto sourceX = frameToX (sample.getLoopStart() - crossfade);
            g.setGradientFill (juce::ColourGradient (xColour.withAlpha (0.0f), fadeX, 0.0f, xColour.withAlpha (0.32f), endX, 0.0f, false));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (fadeX, top, endX, bottom));
            g.setColour (xColour.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (sourceX, top, startX, bottom));
        }

        const auto flagFont = makeFont (9.0f, true);

        const auto drawMarker = [&] (Marker marker, float x, const char* text, juce::Colour markerColour, juce::Colour textColour,
                                     bool flagRight, bool flagBottom)
        {
            const auto isSelected = marker == selected && hasKeyboardFocus (false);
            g.setColour (isSelected ? selectedColour : markerColour);
            g.drawLine (x, top, x, bottom, isSelected ? 2.0f : 1.5f);

            const auto flagWidth = (float) juce::GlyphArrangement::getStringWidthInt (flagFont, text) + 8.0f;
            const auto flag = juce::Rectangle<float> (flagRight ? x : x - flagWidth, flagBottom ? bottom - (float) flagHeight : top,
                                                      flagWidth, (float) flagHeight);
            juce::Path flagShape;
            flagShape.addRoundedRectangle (flag.getX(), flag.getY(), flag.getWidth(), flag.getHeight(), 3.0f, 3.0f,
                                           ! flagBottom && ! flagRight, ! flagBottom && flagRight, flagBottom && ! flagRight, flagBottom && flagRight);
            g.fillPath (flagShape);
            g.setColour (isSelected ? findColour (theme::card) : textColour);
            g.setFont (flagFont);
            g.drawText (text, flag, juce::Justification::centred);
        };

        const auto flagTextColour = findColour (theme::onAccent);
        drawMarker (Marker::crossfade, frameToX (sample.getLoopEnd() - crossfade), "X", xColour, juce::Colour (0xff1d1d1f), false, true);
        drawMarker (Marker::start, startX, "S", colour, flagTextColour, true, false);
        drawMarker (Marker::end, endX, "E", colour, flagTextColour, false, false);
    }

    if (playPosition >= 0)
    {
        const auto x = frameToX (playPosition);
        g.setColour (findColour (theme::playhead));
        g.drawLine (x, (float) area.getY(), x, (float) area.getBottom(), 1.5f);
    }

    // Fußzeile: Loop-Punkte bzw. Hinweis
    const auto rate = sample.getSampleRate();
    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (12, 0);
    const auto labelFont = makeFont (11.0f);
    g.setFont (labelFont);

    const auto drawPair = [&] (const juce::String& label, const juce::String& value)
    {
        const auto labelWidth = juce::GlyphArrangement::getStringWidthInt (labelFont, label);
        const auto valueWidth = juce::GlyphArrangement::getStringWidthInt (labelFont, value);

        g.setColour (findColour (theme::secondaryText));
        g.drawText (label, footer.removeFromLeft (labelWidth + 2), juce::Justification::centredLeft);
        g.setColour (findColour (theme::primaryText));
        g.drawText (value, footer.removeFromLeft (valueWidth + 2), juce::Justification::centredLeft);
        footer.removeFromLeft (12);
    };

    juce::String chipText;

    if (sample.isLoopEnabled())
    {
        drawPair ("Start ", formatSeconds (sample.getLoopStart(), rate));
        drawPair ("End ", formatSeconds (sample.getLoopEnd(), rate));
        drawPair ("Crossfade ", juce::String (juce::roundToInt (sample.getCrossfade() * 1000.0 / juce::jmax (1.0, rate))) + " ms");

        if (const auto algorithm = sample.getLoopAlgorithm())
            chipText = getDisplayName (*algorithm);
    }
    else
    {
        g.setColour (findColour (theme::secondaryText));
        g.drawText ("No loop · sample plays once"_u, footer.removeFromLeft (230), juce::Justification::centredLeft);
        chipText = formatSeconds (sample.getNumFrames(), rate);
    }

    if (chipText.isNotEmpty())
    {
        const auto chipWidth = juce::GlyphArrangement::getStringWidthInt (labelFont, chipText) + 12;

        if (footer.getWidth() >= chipWidth)
        {
            const auto chip = footer.removeFromRight (chipWidth).toFloat().withSizeKeepingCentre ((float) chipWidth, 18.0f);
            g.setColour (findColour (theme::chip));
            g.fillRoundedRectangle (chip, 4.0f);
            g.setColour (findColour (theme::secondaryText));
            g.drawText (chipText, chip, juce::Justification::centred);
        }
    }

    g.restoreState();

    // Rahmen: gesperrt, gewählt (Akzent mit Ring) oder normal
    if (selectedCard)
    {
        g.setColour (loopColour.withAlpha (0.2f));
        g.drawRoundedRectangle (bounds.reduced (1.5f), 8.5f, 3.0f);
    }

    g.setColour (selectedCard ? loopColour
                              : locked ? findColour (theme::locked)
                                       : hasKeyboardFocus (false) ? loopColour.withAlpha (0.7f) : findColour (theme::cardOutline));
    g.drawRoundedRectangle (bounds, 10.0f, 1.0f);
}

void SampleCard::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).withTrimmedRight (8);
    constexpr int height = 26;
    const auto place = [&header] (juce::Component& c, int width)
    {
        c.setBounds (header.removeFromRight (width).withSizeKeepingCentre (width, height));
        header.removeFromRight (6);
    };

    place (lockButton, 28);
    place (loopButton, 66);
    place (autoButton, 46);
    place (playButton, 28);
}

//==============================================================================
void SampleCard::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (getMarkerAt (e.position) != Marker::none ? juce::MouseCursor::LeftRightResizeCursor
                                                             : juce::MouseCursor::NormalCursor);
}

void SampleCard::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    if (onSelected != nullptr)
        onSelected (this);

    dragging = getMarkerAt (e.position);

    if (dragging != Marker::none)
        selected = dragging;

    repaint();

    // Ein Ziehvorgang ist ein Undo-Schritt (SetProperty-Aktionen werden zusammengefasst)
    if (dragging != Marker::none)
        session.getUndoManager().beginNewTransaction (dragging == Marker::start ? "Move loop start"
                                                    : dragging == Marker::end   ? "Move loop end"
                                                                                : "Change crossfade"_u);
}

void SampleCard::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging != Marker::none && canEdit())
        moveMarker (dragging, xToFrame (e.position.x));
}

void SampleCard::mouseUp (const juce::MouseEvent&)
{
    dragging = Marker::none;
}

void SampleCard::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Doppelklick in die Wellenform schaltet den Loop an
    if (! sample.isLoopEnabled() && ! sample.isLocked() && getWaveformArea().contains (e.getPosition()))
        loopButton.triggerClick();
}

bool SampleCard::keyPressed (const juce::KeyPress& key)
{
    const auto left = key.getKeyCode() == juce::KeyPress::leftKey;
    const auto right = key.getKeyCode() == juce::KeyPress::rightKey;

    // Leertaste: anhören, solange sie gehalten wird
    if (key == juce::KeyPress::spaceKey)
    {
        if (! auditioning && playButton.isEnabled())
        {
            startAudition();
            auditionBySpace = auditioning;
        }

        return true;
    }

    if (key == juce::KeyPress::escapeKey && selected != Marker::none)
    {
        selected = Marker::none;
        repaint();
        return true;
    }

    if ((! left && ! right) || selected == Marker::none || ! canEdit())
        return false;

    // Nudge: 1 Frame, mit Umschalt 1 ms, mit Wahl 10 ms
    const auto modifiers = key.getModifiers();
    const auto rate = sample.getSampleRate();
    auto step = 1;

    if (modifiers.isAltDown())
        step = juce::jmax (1, juce::roundToInt (rate * 0.01));
    else if (modifiers.isShiftDown())
        step = juce::jmax (1, juce::roundToInt (rate * 0.001));

    // Schnell aufeinanderfolgende Schritte desselben Markers sind ein Undo-Schritt
    const auto now = juce::Time::getMillisecondCounter();

    if (selected != lastNudged || now - lastNudgeTime > nudgeGroupMs)
        session.getUndoManager().beginNewTransaction ("Move marker");

    lastNudged = selected;
    lastNudgeTime = now;

    moveMarker (selected, getMarkerFrame (selected) + (left ? -step : step));
    return true;
}

bool SampleCard::keyStateChanged (bool)
{
    if (auditionBySpace && ! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey))
    {
        stopAudition();
        return true;
    }

    return false;
}

void SampleCard::focusLost (FocusChangeType)
{
    if (auditionBySpace)
        stopAudition();

    lastNudged = Marker::none;
    repaint();
}

} // namespace autosampler
