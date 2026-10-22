#pragma once

#include "Text.h"
#include "host/PluginHost.h"

namespace autosampler
{

/** Play: Wiedergabe der Samples (Hüllkurve, Velocity, One Shot), Chord-Player,
    Arpeggiator bzw. Step-Sequencer und Monitor-Reverb. Die Einstellungen gelten
    für Plugin und Samples und werden in den App-Einstellungen gespeichert.
    Die Quelle (A/B) wird über die Toolbar bzw. das Menü Wiedergabe gewählt. */
class LivePlayView final : public juce::Component,
                           private juce::Timer
{
public:
    explicit LivePlayView (PluginHost&);
    ~LivePlayView() override;

    PlaySource getSource() const noexcept               { return settings.source; }
    const LivePlaySettings& getSettings() const noexcept { return settings; }
    void setSource (PlaySource);

    /** Chord-Player an/aus; beim Einschalten gilt der zuletzt gewählte Akkord (Vorgabe Major). */
    bool isChordEnabled() const noexcept                { return settings.perform.chord != ChordType::off; }
    void setChordEnabled (bool);

    /** Status des SamplerLoaders (Anzahl Samples, Fehler). */
    void setSamplerStatus (const juce::String&);

    /** Nach jeder Änderung der Einstellungen (auch der Quelle). */
    std::function<void()> onSettingsChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class StepGrid;

    struct Knob
    {
        juce::Label label;
        juce::Slider slider;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const juce::String& name, double min, double max, double interval,
                    const juce::String& suffix, const juce::String& tooltip);
    void updateControls();
    void storeSettings();
    void layoutKnobs (juce::Rectangle<int> area, std::initializer_list<Knob*>);
    juce::String getMidiInputText() const;

    ChordType lastChord = ChordType::major;
    PluginHost& host;
    LivePlaySettings settings;

    juce::GroupComponent samplerGroup { {}, "Sampler" }, chordGroup { {}, "Chord-Player" },
                         reverbGroup { {}, "Monitor-Reverb" }, patternGroup { {}, "Arpeggiator / Step-Sequencer" };

    Knob attackKnob, releaseKnob, velocityKnob, gainKnob, voicesKnob;
    juce::Label oneShotLabel { {}, "No loop" };
    juce::ComboBox oneShotBox;
    juce::Label samplerStatus, voicesLabel, midiLabel;

    juce::Label chordLabel { {}, "Chord" }, inversionLabel { {}, "Inversion" };
    juce::ComboBox chordBox, inversionBox;

    juce::ToggleButton reverbButton { "An" };
    Knob mixKnob, sizeKnob, dampingKnob;

    juce::Label patternLabel { {}, "Mode" }, rateLabel { {}, "Note value" }, arpModeLabel { {}, "Direction" },
                octavesLabel { {}, "Octaves" };
    juce::ComboBox patternBox, rateBox, arpModeBox, octavesBox;
    Knob bpmKnob, gateKnob, lengthKnob;
    juce::ToggleButton latchButton { "Latch" };
    juce::TextButton resetStepsButton { "Reset steps"_u };
    juce::Label sequencerHint;
    std::unique_ptr<StepGrid> stepGrid;

    juce::String midiText;
    int timerTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePlayView)
};

} // namespace autosampler
