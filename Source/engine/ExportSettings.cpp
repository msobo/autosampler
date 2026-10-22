#include "ExportSettings.h"

namespace autosampler
{

namespace ids
{
    static const juce::Identifier exportSettings      { "Export" };
    static const juce::Identifier target              { "target" };
    static const juce::Identifier name                { "name" };
    static const juce::Identifier sampleRate          { "sampleRate" };
    static const juce::Identifier bitDepth            { "bitDepth" };
    static const juce::Identifier normalize           { "normalize" };
    static const juce::Identifier peakDb              { "peakDb" };
    static const juce::Identifier mono                { "mono" };
    static const juce::Identifier tail                { "tail" };
    static const juce::Identifier loopOnly            { "loopOnly" };
    static const juce::Identifier loopOnRelease       { "loopOnRelease" };
    static const juce::Identifier oneShotWithoutLoop  { "oneShotWithoutLoop" };
    static const juce::Identifier attackMs            { "attackMs" };
    static const juce::Identifier releaseMs           { "releaseMs" };
    static const juce::Identifier velocitySensitivity { "velocitySensitivity" };
    static const juce::Identifier zip                 { "zip" };
    static const juce::Identifier zoneSmoothing       { "zoneSmoothing" };
}

juce::String toString (ExportTarget target)
{
    switch (target)
    {
        case ExportTarget::wav:     return "wav";
        case ExportTarget::sfz:     return "sfz";
        case ExportTarget::tonverk: return "tonverk";
    }

    return "sfz";
}

std::optional<ExportTarget> parseExportTarget (const juce::String& text)
{
    for (const auto target : { ExportTarget::wav, ExportTarget::sfz, ExportTarget::tonverk })
        if (text.trim().equalsIgnoreCase (toString (target)))
            return target;

    return std::nullopt;
}

juce::String getDisplayName (ExportTarget target)
{
    switch (target)
    {
        case ExportTarget::wav:     return "WAV with metadata";
        case ExportTarget::sfz:     return "SFZ";
        case ExportTarget::tonverk: return "Elektron Tonverk";
    }

    return {};
}

juce::String toString (TailMode mode)
{
    return mode == TailMode::cutAfterLoop ? "cutAfterLoop" : "original";
}

std::optional<TailMode> parseTailMode (const juce::String& text)
{
    for (const auto mode : { TailMode::original, TailMode::cutAfterLoop })
        if (text.trim().equalsIgnoreCase (toString (mode)))
            return mode;

    return std::nullopt;
}

juce::String getDisplayName (TailMode mode)
{
    return mode == TailMode::cutAfterLoop ? juce::String ("Cut at loop end + 32")
                                          : juce::String ("Original");
}

//==============================================================================
juce::ValueTree ExportSettings::toValueTree() const
{
    juce::ValueTree tree (ids::exportSettings);
    tree.setProperty (ids::target, toString (target), nullptr);
    tree.setProperty (ids::name, name, nullptr);
    tree.setProperty (ids::sampleRate, sampleRate, nullptr);
    tree.setProperty (ids::bitDepth, bitDepth, nullptr);
    tree.setProperty (ids::normalize, normalize, nullptr);
    tree.setProperty (ids::peakDb, peakDb, nullptr);
    tree.setProperty (ids::mono, mono, nullptr);
    tree.setProperty (ids::tail, toString (tail), nullptr);
    tree.setProperty (ids::loopOnly, loopOnly, nullptr);
    tree.setProperty (ids::loopOnRelease, loopOnRelease, nullptr);
    tree.setProperty (ids::oneShotWithoutLoop, oneShotWithoutLoop, nullptr);
    tree.setProperty (ids::attackMs, attackMs, nullptr);
    tree.setProperty (ids::releaseMs, releaseMs, nullptr);
    tree.setProperty (ids::velocitySensitivity, velocitySensitivity, nullptr);
    tree.setProperty (ids::zip, zip, nullptr);
    tree.setProperty (ids::zoneSmoothing, zoneSmoothing, nullptr);
    return tree;
}

ExportSettings ExportSettings::fromValueTree (const juce::ValueTree& tree)
{
    ExportSettings s;
    s.target              = parseExportTarget (tree[ids::target]).value_or (s.target);
    s.name                = tree.getProperty (ids::name, s.name).toString().trim();
    s.sampleRate          = juce::jlimit (0.0, 384000.0, (double) tree.getProperty (ids::sampleRate, s.sampleRate));
    s.normalize           = tree.getProperty (ids::normalize, s.normalize);
    s.peakDb              = juce::jlimit (-24.0f, 0.0f, (float) tree.getProperty (ids::peakDb, s.peakDb));
    s.mono                = tree.getProperty (ids::mono, s.mono);
    s.tail                = parseTailMode (tree[ids::tail]).value_or (s.tail);
    s.loopOnly            = tree.getProperty (ids::loopOnly, s.loopOnly);
    s.loopOnRelease       = tree.getProperty (ids::loopOnRelease, s.loopOnRelease);
    s.oneShotWithoutLoop  = tree.getProperty (ids::oneShotWithoutLoop, s.oneShotWithoutLoop);
    s.attackMs            = juce::jlimit (0.0f, 10000.0f, (float) tree.getProperty (ids::attackMs, s.attackMs));
    s.releaseMs           = juce::jlimit (0.0f, 30000.0f, (float) tree.getProperty (ids::releaseMs, s.releaseMs));
    s.velocitySensitivity = juce::jlimit (0.0f, 1.0f, (float) tree.getProperty (ids::velocitySensitivity, s.velocitySensitivity));
    s.zip                 = tree.getProperty (ids::zip, s.zip);
    s.zoneSmoothing       = juce::jlimit (0.0f, 1.0f, (float) tree.getProperty (ids::zoneSmoothing, s.zoneSmoothing));

    const int bits = tree.getProperty (ids::bitDepth, s.bitDepth);
    s.bitDepth = bits == 16 || bits == 24 || bits == 32 ? bits : s.bitDepth;
    return s;
}

juce::Identifier ExportSettings::getValueTreeType()
{
    return ids::exportSettings;
}

} // namespace autosampler
