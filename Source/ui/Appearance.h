#pragma once

#include "Theme.h"

namespace autosampler
{

/** Besitzt das LookAndFeel der App und schaltet zwischen Hell und Dunkel um:
    nach der Einstellung (System/Hell/Dunkel, gespeichert als "appearance") oder,
    bei System, nach der Systemeinstellung. Setzt auch das Erscheinungsbild der
    nativen Teile (Titelleiste, Menüs, Dateiauswahl) und aktualisiert alle
    offenen Fenster. Muss alle Komponenten überleben. */
class Appearance final : private juce::DarkModeSettingListener
{
public:
    explicit Appearance (juce::PropertiesFile&);
    ~Appearance() override;

    AppearanceMode getMode() const noexcept     { return mode; }
    void setMode (AppearanceMode);

    bool isDark() const noexcept                { return lookAndFeel.isDark(); }

private:
    void darkModeSettingChanged() override      { apply(); }
    void apply();

    juce::PropertiesFile& settings;
    AppLookAndFeel lookAndFeel;
    AppearanceMode mode = AppearanceMode::system;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Appearance)
};

} // namespace autosampler
