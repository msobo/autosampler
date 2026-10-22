#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace autosampler
{

/** Eigenes Fenster für die Oberfläche des geladenen Plugins. Hat das Plugin
    keinen Editor, wird ein generischer Parameter-Editor angezeigt.

    Das Fenster muss zerstört werden, bevor die Plugin-Instanz gelöscht wird.
*/
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessor&, std::function<void()> onClose);
    ~PluginWindow() override;

    void closeButtonPressed() override;

private:
    static std::unique_ptr<juce::AudioProcessorEditor> createEditorFor (juce::AudioProcessor&);

    juce::AudioProcessor& processor;
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

} // namespace autosampler
