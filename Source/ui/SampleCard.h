#pragma once

#include "engine/SamplerEngine.h"
#include "engine/Session.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace autosampler
{

/** Karte eines Samples im Editor: Wellenform, Loop-Bereich mit ziehbaren
    Markern S, E und X (Crossfade-Beginn), Score, Knöpfe Play, Auto, Loop und Lock.
    Ein angeklickter Marker lässt sich mit den Pfeiltasten verschieben (Nudge).
    Play (halten) bzw. die Leertaste spielt das Sample mit Loop und Crossfade über
    den Sampler und zeigt die Abspielposition.
    Alle Änderungen gehen über den UndoManager der Session. */
class SampleCard final : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::ValueTree::Listener,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    SampleCard (Session&, const juce::ValueTree& sampleTree,
                juce::AudioFormatManager&, juce::AudioThumbnailCache&);
    ~SampleCard() override;

    const juce::ValueTree& getSampleTree() const noexcept   { return sample.getTree(); }

    /** Auto-Knopf oder erstes Einschalten des Loops: Loop-Suche für dieses Sample. */
    std::function<void (const juce::ValueTree&)> onFindLoop;

    /** Ziel der Hörprüfung; nullptr schaltet Play ab. */
    void setAudition (SampleAudition*);

    /** Höhe der Karte bei dieser Höhe der Wellenform. */
    static int getHeightForWaveform (int waveformHeight) noexcept;

    /** Rahmen der gewählten Karte (Editor und Seitenleiste folgen der Auswahl). */
    void setSelected (bool);
    bool isSelected() const noexcept    { return selectedCard; }

    /** Wird aufgerufen, wenn die Karte angeklickt wird. */
    std::function<void (SampleCard*)> onSelected;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

private:
    enum class Marker { none, start, end, crossfade };

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void startAudition();
    void stopAudition();
    void updateButtons();
    juce::Rectangle<int> getWaveformArea() const;
    float frameToX (int frame) const;
    int xToFrame (float x) const;
    Marker getMarkerAt (juce::Point<float>) const;
    bool canEdit() const;
    void moveMarker (Marker, int frame);
    int getMarkerFrame (Marker) const;

    Session& session;
    SessionSample sample;
    juce::ValueTree listenedTree;
    juce::AudioThumbnail thumbnail;
    bool fileMissing = false;

    juce::TextButton playButton;
    juce::TextButton autoButton { "Auto" };
    juce::TextButton loopButton { "Loop" };
    juce::TextButton lockButton;
    bool selectedCard = false;

    SampleAudition* audition = nullptr;
    bool auditioning = false, auditionBySpace = false;
    int playPosition = -1;

    Marker dragging = Marker::none;
    Marker selected = Marker::none;
    Marker lastNudged = Marker::none;
    juce::uint32 lastNudgeTime = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleCard)
};

} // namespace autosampler
