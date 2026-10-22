#include "AboutOverlay.h"
#include "Text.h"

#include <BinaryData.h>

namespace autosampler
{

namespace
{
    const juce::String supportUrl = "https://buymeacoffee.com/msobo";
    constexpr int cardWidth = 420, cardHeight = 560;
}

AboutOverlay::AboutOverlay (std::function<void()> onCloseCallback)
    : onClose (std::move (onCloseCallback))
{
    qr = juce::ImageCache::getFromMemory (BinaryData::SupportQr_png, BinaryData::SupportQr_pngSize);

    setLabel (nameLabel, "Autosampler", theme::getFont (theme::Face::logo, 30.0f), theme::primaryText);
    setLabel (versionLabel, "Version " + juce::String (JUCE_APPLICATION_VERSION_STRING),
              juce::Font (juce::FontOptions (theme::fontSize (12.0f))), theme::secondaryText);
    setLabel (authorLabel, "By Marek Sobolewski"_u, juce::Font (juce::FontOptions (theme::fontSize (14.0f))), theme::primaryText);
    setLabel (supportLabel, "Do you like Autosampler? Support me with a coffee!"_u,
              juce::Font (juce::FontOptions (theme::fontSize (12.0f))), theme::secondaryText);
    setLabel (licenseLabel, "Free software under the GNU AGPL v3, Copyright 2026 Marek Sobolewski"_u,
              juce::Font (juce::FontOptions (theme::fontSize (10.0f))), theme::tertiaryText);

    link.setButtonText (supportUrl);
    link.setURL (juce::URL (supportUrl));
    link.setFont (juce::FontOptions (theme::fontSize (13.0f)), false, juce::Justification::centred);
    theme::bindColour (link, juce::HyperlinkButton::textColourId, theme::accentText);
    link.setTooltip ("Opens buymeacoffee.com/msobo in the browser"_u);
    addAndMakeVisible (link);

    closeButton.setButtonText ("Close"_u);
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);

    setWantsKeyboardFocus (true);
}

void AboutOverlay::setLabel (juce::Label& label, const juce::String& text, const juce::Font& font, int colourId)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (font);
    label.setJustificationType (juce::Justification::centred);
    label.setInterceptsMouseClicks (false, false);
    theme::bindColour (label, juce::Label::textColourId, colourId);
    addAndMakeVisible (label);
}

void AboutOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));

    const auto card = cardArea.toFloat();
    g.setColour (findColour (theme::canvas));
    g.fillRoundedRectangle (card, 12.0f);
    g.setColour (findColour (theme::ink));
    g.drawRoundedRectangle (card, 12.0f, 3.0f);

    if (qr.isValid())
    {
        const auto area = qrArea.toFloat();
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (area.expanded (8.0f), 8.0f);
        g.setColour (findColour (theme::ink));
        g.drawRoundedRectangle (area.expanded (8.0f), 8.0f, 2.0f);
        g.drawImage (qr, area, juce::RectanglePlacement::centred);
    }
}

void AboutOverlay::resized()
{
    cardArea = getLocalBounds().withSizeKeepingCentre (juce::jmin (cardWidth, getWidth() - 20),
                                                       juce::jmin (cardHeight, getHeight() - 20));
    auto area = cardArea.reduced (24, 28);

    nameLabel.setBounds (area.removeFromTop (44));
    versionLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (14);
    authorLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (22);
    supportLabel.setBounds (area.removeFromTop (36));
    link.setBounds (area.removeFromTop (26));
    area.removeFromTop (22);
    qrArea = area.removeFromTop (220).withSizeKeepingCentre (220, 220);
    area.removeFromTop (14);
    licenseLabel.setBounds (area.removeFromBottom (36));
    closeButton.setBounds (area.removeFromBottom (30).withSizeKeepingCentre (100, 30));
}

void AboutOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! cardArea.contains (e.getPosition()) && onClose != nullptr)
        onClose();
}

bool AboutOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose != nullptr)
            onClose();

        return true;
    }

    return true; // Tasten gehören dem Overlay, solange es offen ist
}

void AboutOverlay::lookAndFeelChanged()
{
    theme::refreshColourBindings (*this);
}

void AboutOverlay::parentHierarchyChanged()
{
    if (getParentComponent() != nullptr)
        grabKeyboardFocus();
}

} // namespace autosampler
