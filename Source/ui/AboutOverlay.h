#pragma once

#include "Theme.h"

namespace autosampler
{

/** Overlay "Über Autosampler": Name, Version, Autor und ein Link samt QR-Code
    für alle, die die Entwicklung unterstützen möchten. Deckt das Hauptfenster ab;
    ein Klick neben die Karte, der Schließen-Knopf oder Escape schließen es. */
class AboutOverlay final : public juce::Component
{
public:
    explicit AboutOverlay (std::function<void()> onClose);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void lookAndFeelChanged() override;
    void parentHierarchyChanged() override;

private:
    void setLabel (juce::Label&, const juce::String& text, const juce::Font&, int colourId);

    std::function<void()> onClose;

    juce::Label nameLabel, versionLabel, authorLabel, supportLabel, licenseLabel;
    juce::HyperlinkButton link;
    juce::TextButton closeButton;
    juce::Image qr;
    juce::Rectangle<int> cardArea, qrArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutOverlay)
};

} // namespace autosampler
