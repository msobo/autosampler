#pragma once

#include "engine/SamplerLoader.h"
#include "engine/Session.h"
#include "host/PluginHost.h"
#include "ui/AboutOverlay.h"
#include "ui/Appearance.h"
#include "ui/AutosampleView.h"
#include "ui/EditorView.h"
#include "ui/ExportView.h"
#include "ui/LivePlayView.h"
#include "ui/PluginWindow.h"
#include "ui/SidebarView.h"
#include "ui/Widgets.h"

namespace autosampler
{

/** Hauptansicht: Plugin-Scan, Instrument laden, Editor öffnen,
    Audio-/MIDI-Einstellungen, Sampling, Sample-Editor und Live-Spiel über
    das In-App-Keyboard und MIDI-In, wahlweise über das Plugin oder die Samples (A/B).
    Besitzt die Session und das Menü Datei/Bearbeiten/Wiedergabe/Ansicht.
*/
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::MenuBarModel,
                            private juce::ChangeListener,
                            private juce::KeyListener
{
public:
    MainComponent (PluginHost&, Appearance&);
    ~MainComponent() override;

    /** Fragt bei ungespeicherten Änderungen nach (Speichern/Verwerfen/
        Abbrechen) und ruft danach proceed auf, außer bei Abbrechen. */
    void confirmDiscardChanges (std::function<void()> proceed);

    /** Öffnet einen Paketordner .asession oder dessen session.json. */
    void openSessionFile (const juce::File&);

    /** Zeigt den Tab mit diesem Namen (ohne Groß-/Kleinschreibung), z. B. "play" oder "export". */
    void showTab (const juce::String& name);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void parentHierarchyChanged() override;
    void lookAndFeelChanged() override;

    // ApplicationCommandTarget
    ApplicationCommandTarget* getNextCommandTarget() override  { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int, int) override {}

private:
    class PluginListWindow;

    enum CommandIDs
    {
        newSession = 0x2001,
        openSession,
        saveSession,
        saveSessionAs,
        exportInstrument,
        undo,
        redo,
        playPlugin,
        playSamples,
        toggleSource,
        panic,
        appearanceSystem,
        appearanceLight,
        appearanceDark,
        showAbout
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    using juce::Component::keyPressed;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void setKeyboardOctave (int octave);

    void refreshInstrumentList();
    void loadSelectedInstrument();
    void showPluginList();
    void showAudioSettings();
    void showAboutWindow();
    void openEditor();
    void closeEditor();
    void updatePluginInfo();
    void setStatus (const juce::String& text, bool isError = false);
    void setSamplingActive (bool active);
    void updateSourceButtons();
    void updateTab();
    void updateSidebarSamples();
    void updateKeyboardMarks();
    juce::Component* getCurrentBottomBar();
    juce::Rectangle<int> getCaseBounds() const;

    void newSessionNow();
    void chooseSessionToOpen();
    void restoreSessionPlugin();
    void saveCurrentSession (std::function<void()> onSaved = nullptr);
    void saveCurrentSessionAs (std::function<void()> onSaved = nullptr);
    void updateWindowTitle();

    PluginHost& host;
    Appearance& appearance;
    Session session;

    juce::ApplicationCommandManager commandManager;
    juce::PopupMenu appMenu;
    juce::TooltipWindow tooltipWindow { this, 700 };

    class Tabs final : public juce::TabbedComponent
    {
    public:
        Tabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}

        std::function<void (int)> onChanged;

        void currentTabChanged (int index, const juce::String&) override
        {
            if (onChanged != nullptr)
                onChanged (index);
        }
    };

    juce::Label titleLabel, subtitleLabel;
    SegmentedControl navControl { { "Autosample", "Editor", "Play", "Export" }, 24, 14 };
    juce::TextButton pluginsButton { "Plugins..." };
    juce::ComboBox instrumentBox;
    juce::TextButton editorButton { "Plugin-Editor" };
    juce::TextButton audioButton { "Audio/MIDI" };
    juce::TextButton panicButton { "Panic" };
    SegmentedControl sourceControl { { "Plugin", "Samples" }, 22, 12 };
    juce::ToggleButton chordToggle { "Chord" };
    juce::Label sourceStatusLabel;
    juce::Label statusLabel;
    juce::Label velocityLabel { {}, "Vel" }, velocityValueLabel;
    juce::Slider velocitySlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    PianoKeyboard keyboard;
    int keyboardOctave = 4;
    int keyboardPanelTop = 0;
    bool showLogoText = true;
    bool logoHovered = false;
    juce::Rectangle<int> getLogoBounds() const;
    void setLogoHovered (bool);
    Tabs tabs;
    SidebarView sidebar;
    AutosampleView autosampleView;
    EditorView editorView;
    LivePlayView livePlayView;
    ExportView exportView;
    SamplerLoader samplerLoader;

    juce::Array<juce::PluginDescription> instruments;
    std::unique_ptr<PluginWindow> editorWindow;
    std::unique_ptr<AboutOverlay> aboutOverlay;
    std::unique_ptr<PluginListWindow> pluginListWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace autosampler
