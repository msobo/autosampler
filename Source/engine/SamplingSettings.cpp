#include "SamplingSettings.h"
#include "Text.h"

namespace autosampler
{

namespace ids
{
    static const juce::Identifier sampling              { "Sampling" };
    static const juce::Identifier lowNote               { "lowNote" };
    static const juce::Identifier highNote              { "highNote" };
    static const juce::Identifier noteStep              { "noteStep" };
    static const juce::Identifier useCustomNotes        { "useCustomNotes" };
    static const juce::Identifier customNotes           { "customNotes" };
    static const juce::Identifier velocities            { "velocities" };
    static const juce::Identifier midiChannel           { "midiChannel" };
    static const juce::Identifier holdSeconds           { "holdSeconds" };
    static const juce::Identifier maxTailSeconds        { "maxTailSeconds" };
    static const juce::Identifier renderMode            { "renderMode" };
    static const juce::Identifier sampleRate            { "sampleRate" };
    static const juce::Identifier bitDepth              { "bitDepth" };
    static const juce::Identifier restoreStatePerSample { "restoreStatePerSample" };
    static const juce::Identifier trimStart             { "trimStart" };
    static const juce::Identifier startThresholdDb      { "startThresholdDb" };
    static const juce::Identifier tailThresholdDb       { "tailThresholdDb" };
    static const juce::Identifier preRollMs             { "preRollMs" };
    static const juce::Identifier fadeOutMs             { "fadeOutMs" };
    static const juce::Identifier outputFolder          { "outputFolder" };
    static const juce::Identifier name                  { "name" };
    static const juce::Identifier roundRobinSource      { "roundRobinSource" };
    static const juce::Identifier roundRobinMode        { "roundRobinMode" };
    static const juce::Identifier roundRobinSteps       { "roundRobinSteps" };
    static const juce::Identifier variationTarget       { "variationTarget" };
    static const juce::Identifier variationController   { "variationController" };
    static const juce::Identifier variationParameterId  { "variationParameterId" };
    static const juce::Identifier variationParameterName { "variationParameterName" };
    static const juce::Identifier variationFrom         { "variationFrom" };
    static const juce::Identifier variationTo           { "variationTo" };
    static const juce::Identifier autoFindLoops         { "autoFindLoops" };
    static const juce::Identifier gainDb                { "gainDb" };
    static const juce::Identifier pan                   { "pan" };
}

juce::ValueTree SamplingSettings::toValueTree() const
{
    juce::ValueTree tree (ids::sampling);
    tree.setProperty (ids::lowNote, lowNote, nullptr);
    tree.setProperty (ids::highNote, highNote, nullptr);
    tree.setProperty (ids::noteStep, noteStep, nullptr);
    tree.setProperty (ids::useCustomNotes, useCustomNotes, nullptr);
    tree.setProperty (ids::customNotes, customNotes, nullptr);
    tree.setProperty (ids::velocities, velocities, nullptr);
    tree.setProperty (ids::midiChannel, midiChannel, nullptr);
    tree.setProperty (ids::holdSeconds, holdSeconds, nullptr);
    tree.setProperty (ids::maxTailSeconds, maxTailSeconds, nullptr);
    tree.setProperty (ids::renderMode, (int) renderMode, nullptr);
    tree.setProperty (ids::sampleRate, sampleRate, nullptr);
    tree.setProperty (ids::bitDepth, bitDepth, nullptr);
    tree.setProperty (ids::restoreStatePerSample, restoreStatePerSample, nullptr);
    tree.setProperty (ids::trimStart, trimStart, nullptr);
    tree.setProperty (ids::startThresholdDb, startThresholdDb, nullptr);
    tree.setProperty (ids::tailThresholdDb, tailThresholdDb, nullptr);
    tree.setProperty (ids::preRollMs, preRollMs, nullptr);
    tree.setProperty (ids::fadeOutMs, fadeOutMs, nullptr);
    tree.setProperty (ids::outputFolder, outputFolder.getFullPathName(), nullptr);
    tree.setProperty (ids::name, name, nullptr);
    tree.setProperty (ids::roundRobinSource, toString (roundRobinSource), nullptr);
    tree.setProperty (ids::roundRobinMode, toString (roundRobinMode), nullptr);
    tree.setProperty (ids::roundRobinSteps, roundRobinSteps, nullptr);
    tree.setProperty (ids::variationTarget, variationTarget == VariationTarget::parameter ? "parameter" : "midiCC", nullptr);
    tree.setProperty (ids::variationController, variationController, nullptr);
    tree.setProperty (ids::variationParameterId, variationParameterId, nullptr);
    tree.setProperty (ids::variationParameterName, variationParameterName, nullptr);
    tree.setProperty (ids::variationFrom, variationFrom, nullptr);
    tree.setProperty (ids::variationTo, variationTo, nullptr);
    tree.setProperty (ids::gainDb, gainDb, nullptr);
    tree.setProperty (ids::pan, pan, nullptr);
    tree.setProperty (ids::autoFindLoops, autoFindLoops, nullptr);
    return tree;
}

juce::Identifier SamplingSettings::getValueTreeType()
{
    return ids::sampling;
}

SamplingSettings SamplingSettings::fromValueTree (const juce::ValueTree& tree)
{
    SamplingSettings s;

    if (! tree.hasType (ids::sampling))
        return s;

    s.lowNote               = juce::jlimit (0, 127, (int) tree.getProperty (ids::lowNote, s.lowNote));
    s.highNote              = juce::jlimit (0, 127, (int) tree.getProperty (ids::highNote, s.highNote));
    s.noteStep              = juce::jlimit (1, 24, (int) tree.getProperty (ids::noteStep, s.noteStep));
    s.useCustomNotes        = tree.getProperty (ids::useCustomNotes, s.useCustomNotes);
    s.customNotes           = tree.getProperty (ids::customNotes, s.customNotes);
    s.velocities            = tree.getProperty (ids::velocities, s.velocities);
    s.midiChannel           = juce::jlimit (1, 16, (int) tree.getProperty (ids::midiChannel, s.midiChannel));
    s.holdSeconds           = tree.getProperty (ids::holdSeconds, s.holdSeconds);
    s.maxTailSeconds        = tree.getProperty (ids::maxTailSeconds, s.maxTailSeconds);
    s.renderMode            = (RenderMode) juce::jlimit (0, 2, (int) tree.getProperty (ids::renderMode, (int) s.renderMode));
    s.sampleRate            = tree.getProperty (ids::sampleRate, s.sampleRate);
    s.bitDepth              = tree.getProperty (ids::bitDepth, s.bitDepth);
    s.restoreStatePerSample = tree.getProperty (ids::restoreStatePerSample, s.restoreStatePerSample);
    s.trimStart             = tree.getProperty (ids::trimStart, s.trimStart);
    s.startThresholdDb      = tree.getProperty (ids::startThresholdDb, s.startThresholdDb);
    s.tailThresholdDb       = tree.getProperty (ids::tailThresholdDb, s.tailThresholdDb);
    s.preRollMs             = tree.getProperty (ids::preRollMs, s.preRollMs);
    s.fadeOutMs             = tree.getProperty (ids::fadeOutMs, s.fadeOutMs);
    s.name                  = tree.getProperty (ids::name, s.name);
    s.roundRobinSource      = parseRoundRobinSource (tree[ids::roundRobinSource]).value_or (s.roundRobinSource);
    s.roundRobinMode        = parseRoundRobinMode (tree[ids::roundRobinMode]).value_or (s.roundRobinMode);
    s.roundRobinSteps       = juce::jlimit (2, maxRoundRobinSteps, (int) tree.getProperty (ids::roundRobinSteps, s.roundRobinSteps));
    s.variationTarget       = tree[ids::variationTarget].toString() == "parameter" ? VariationTarget::parameter
                                                                                  : VariationTarget::midiCC;
    s.variationController   = juce::jlimit (0, 127, (int) tree.getProperty (ids::variationController, s.variationController));
    s.variationParameterId  = tree.getProperty (ids::variationParameterId, s.variationParameterId);
    s.variationParameterName = tree.getProperty (ids::variationParameterName, s.variationParameterName);
    s.variationFrom         = juce::jlimit (0, 127, (int) tree.getProperty (ids::variationFrom, s.variationFrom));
    s.variationTo           = juce::jlimit (0, 127, (int) tree.getProperty (ids::variationTo, s.variationTo));
    s.gainDb                = juce::jlimit (-24.0f, 12.0f, (float) tree.getProperty (ids::gainDb, s.gainDb));
    s.pan                   = juce::jlimit (-1.0f, 1.0f, (float) tree.getProperty (ids::pan, s.pan));

    s.autoFindLoops         = tree.getProperty (ids::autoFindLoops, s.autoFindLoops);

    if (const auto folder = tree.getProperty (ids::outputFolder).toString(); juce::File::isAbsolutePath (folder))
        s.outputFolder = juce::File (folder);

    return s;
}

//==============================================================================
juce::String getNoteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
}

std::optional<int> parseNoteName (const juce::String& input)
{
    const auto text = input.trim();

    if (text.isEmpty())
        return std::nullopt;

    if (text.containsOnly ("0123456789"))
    {
        const auto value = text.getIntValue();
        return juce::isPositiveAndBelow (value, 128) ? std::optional<int> (value) : std::nullopt;
    }

    static constexpr int semitones[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const auto letter = juce::CharacterFunctions::toUpperCase (text[0]);

    if (letter < 'A' || letter > 'G')
        return std::nullopt;

    auto note = semitones[letter - 'A'];
    auto rest = text.substring (1);

    if (rest.startsWithChar ('#'))
    {
        ++note;
        rest = rest.substring (1);
    }
    else if (rest.startsWithChar ('b'))
    {
        --note;
        rest = rest.substring (1);
    }

    const auto digits = rest.startsWithChar ('-') ? rest.substring (1) : rest;

    if (digits.isEmpty() || ! digits.containsOnly ("0123456789"))
        return std::nullopt;

    // C3 = 60, also Oktave -2 beginnt bei 0
    const auto value = (rest.getIntValue() + 2) * 12 + note;
    return juce::isPositiveAndBelow (value, 128) ? std::optional<int> (value) : std::nullopt;
}

namespace
{
    juce::StringArray tokenise (const juce::String& text)
    {
        juce::StringArray tokens;
        tokens.addTokens (text, ",; \t\n", {});
        tokens.removeEmptyStrings();
        return tokens;
    }

    void sortUnique (juce::Array<int>& values)
    {
        std::sort (values.begin(), values.end());
        values.removeRange ((int) (std::unique (values.begin(), values.end()) - values.begin()), values.size());
    }
}

juce::Array<int> parseNoteList (const juce::String& text, juce::String& error)
{
    juce::Array<int> notes;

    for (const auto& token : tokenise (text))
    {
        const auto note = parseNoteName (token);

        if (! note.has_value())
        {
            error = "Invalid note: "_u + token;
            return {};
        }

        notes.add (*note);
    }

    sortUnique (notes);
    return notes;
}

juce::Array<int> parseVelocityList (const juce::String& text, juce::String& error)
{
    juce::Array<int> velocities;

    for (const auto& token : tokenise (text))
    {
        const auto value = token.getIntValue();

        if (! token.containsOnly ("0123456789") || value < 1 || value > 127)
        {
            error = "Invalid velocity: "_u + token + " (allowed 1-127)";
            return {};
        }

        velocities.add (value);
    }

    sortUnique (velocities);
    return velocities;
}

juce::Array<int> getNotes (const SamplingSettings& settings, juce::String& error)
{
    if (settings.useCustomNotes)
        return parseNoteList (settings.customNotes, error);

    if (settings.lowNote > settings.highNote)
    {
        error = "Lowest note is above the highest"_u;
        return {};
    }

    juce::Array<int> notes;

    for (int note = settings.lowNote; note <= settings.highNote; note += juce::jmax (1, settings.noteStep))
        notes.add (note);

    return notes;
}

juce::String toString (RoundRobinSource source)
{
    switch (source)
    {
        case RoundRobinSource::off:     return "off";
        case RoundRobinSource::real:    return "real";
        case RoundRobinSource::midiCC:  return "midiCC";
    }

    return "off";
}

juce::String toString (RoundRobinMode mode)
{
    return mode == RoundRobinMode::linked ? "linked" : "independent";
}

std::optional<RoundRobinSource> parseRoundRobinSource (const juce::String& text)
{
    for (const auto source : { RoundRobinSource::off, RoundRobinSource::real, RoundRobinSource::midiCC })
        if (text.trim().equalsIgnoreCase (toString (source)))
            return source;

    if (text.trim().equalsIgnoreCase ("cc"))
        return RoundRobinSource::midiCC;

    return std::nullopt;
}

std::optional<RoundRobinMode> parseRoundRobinMode (const juce::String& text)
{
    for (const auto mode : { RoundRobinMode::independent, RoundRobinMode::linked })
        if (text.trim().equalsIgnoreCase (toString (mode)))
            return mode;

    return std::nullopt;
}

int getRoundRobinCount (const SamplingSettings& settings)
{
    if (settings.roundRobinSource == RoundRobinSource::off)
        return 1;

    // Linked bindet Variationswerte an Velocity-Layer; Real RR ist immer Independent
    if (settings.roundRobinSource == RoundRobinSource::midiCC && settings.roundRobinMode == RoundRobinMode::linked)
        return 1;

    return juce::jlimit (2, SamplingSettings::maxRoundRobinSteps, settings.roundRobinSteps);
}

juce::Array<int> getVariationValues (int from, int to, int count)
{
    juce::Array<int> values;
    from = juce::jlimit (0, 127, from);
    to = juce::jlimit (0, 127, to);

    for (int i = 0; i < count; ++i)
        values.add (count <= 1 ? from : (int) std::lround (from + (to - from) * (double) i / (count - 1)));

    return values;
}

juce::Array<SampleRequest> buildSampleList (const SamplingSettings& settings, juce::String& error)
{
    const auto notes = getNotes (settings, error);

    if (error.isNotEmpty())
        return {};

    const auto velocities = parseVelocityList (settings.velocities, error);

    if (error.isNotEmpty())
        return {};

    if (notes.isEmpty())
        error = "No notes selected"_u;
    else if (velocities.isEmpty())
        error = "No velocities given";

    const auto varies = settings.roundRobinSource == RoundRobinSource::midiCC;
    const auto linked = settings.roundRobinMode == RoundRobinMode::linked;
    const auto count = getRoundRobinCount (settings);

    if (error.isEmpty() && varies && settings.variationTarget == VariationTarget::parameter
        && settings.variationParameterId.isEmpty())
        error = "No plugin parameter selected for the variation (Learn)"_u;

    if (error.isEmpty() && varies && linked && velocities.size() < 2)
        error = "Linked needs at least two velocities"_u;

    if (error.isNotEmpty())
        return {};

    const auto values = getVariationValues (settings.variationFrom, settings.variationTo,
                                            linked ? velocities.size() : count);

    juce::Array<SampleRequest> requests;

    for (const auto note : notes)
    {
        for (int v = 0; v < velocities.size(); ++v)
        {
            for (int rr = 0; rr < count; ++rr)
            {
                SampleRequest request;
                request.note = note;
                request.velocity = velocities[v];
                request.roundRobin = rr;
                request.roundRobinCount = count;

                if (varies)
                    request.variation = values[linked ? v : rr];

                requests.add (request);
            }
        }
    }

    return requests;
}

} // namespace autosampler
