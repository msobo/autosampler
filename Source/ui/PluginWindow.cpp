#include "PluginWindow.h"
#include "Theme.h"

namespace autosampler
{

PluginWindow::PluginWindow (juce::AudioProcessor& p, std::function<void()> onCloseCallback)
    : DocumentWindow (p.getName(),
                      juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      processor (p),
      onClose (std::move (onCloseCallback))
{
    theme::bindColour (*this, juce::ResizableWindow::backgroundColourId, juce::ResizableWindow::backgroundColourId);
    setUsingNativeTitleBar (true);

    auto editor = createEditorFor (processor);
    const auto resizable = editor->isResizable();

    setContentOwned (editor.release(), true);
    setResizable (resizable, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

PluginWindow::~PluginWindow()
{
    // Der Editor meldet sich in seinem Destruktor selbst beim Prozessor ab
    clearContentComponent();
}

void PluginWindow::closeButtonPressed()
{
    // Löst die Zerstörung über den Besitzer aus; danach nichts mehr anfassen
    if (onClose != nullptr)
        onClose();
}

std::unique_ptr<juce::AudioProcessorEditor> PluginWindow::createEditorFor (juce::AudioProcessor& p)
{
    if (p.hasEditor())
        if (auto* editor = p.createEditorAndMakeActive())
            return std::unique_ptr<juce::AudioProcessorEditor> (editor);

    return std::make_unique<juce::GenericAudioProcessorEditor> (p);
}

} // namespace autosampler
