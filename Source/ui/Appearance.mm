#include "Appearance.h"

#import <AppKit/AppKit.h>

namespace autosampler
{

namespace
{
    constexpr const char* settingsKey = "appearance";

    /** Erscheinungsbild der nativen Teile; nil folgt dem System. */
    void setNativeAppearance (AppearanceMode mode)
    {
        NSAppearance* appearance = nil;

        if (mode == AppearanceMode::light)
            appearance = [NSAppearance appearanceNamed: NSAppearanceNameAqua];
        else if (mode == AppearanceMode::dark)
            appearance = [NSAppearance appearanceNamed: NSAppearanceNameDarkAqua];

        [NSApp setAppearance: appearance];
    }
}

Appearance::Appearance (juce::PropertiesFile& settingsToUse)
    : settings (settingsToUse),
      mode (appearanceModeFromString (settings.getValue (settingsKey)))
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    juce::Desktop::getInstance().addDarkModeSettingListener (this);
    apply();
}

Appearance::~Appearance()
{
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void Appearance::setMode (AppearanceMode newMode)
{
    if (newMode == mode)
        return;

    mode = newMode;
    settings.setValue (settingsKey, toString (mode));
    apply();
}

void Appearance::apply()
{
    const auto dark = mode == AppearanceMode::system ? juce::Desktop::getInstance().isDarkModeActive()
                                                     : mode == AppearanceMode::dark;
    setNativeAppearance (mode);

    if (dark == lookAndFeel.isDark())
        return;

    lookAndFeel.setDark (dark);

    // Gesetzte Farben erneuern, dann alle Komponenten benachrichtigen (Slider bauen ihre Textfelder neu)
    auto& desktop = juce::Desktop::getInstance();

    for (int i = desktop.getNumComponents(); --i >= 0;)
    {
        if (auto* component = desktop.getComponent (i))
        {
            theme::refreshColourBindings (*component);
            component->sendLookAndFeelChange();
        }
    }
}

} // namespace autosampler
