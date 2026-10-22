#include "Session.h"
#include "Text.h"

namespace autosampler
{

namespace
{
    constexpr const char* formatName = "Autosampler Session";
    const juce::Identifier pluginType { "PLUGIN" };     // Tag von PluginDescription::createXml()
}

//==============================================================================
SessionSample::SessionSample (juce::ValueTree treeToUse, juce::UndoManager* undoManagerToUse)
    : tree (std::move (treeToUse)), undoManager (undoManagerToUse)
{
}

juce::String SessionSample::getFileName() const     { return tree[sessionIds::file]; }
int SessionSample::getNote() const                  { return tree[sessionIds::note]; }
int SessionSample::getVelocity() const              { return tree[sessionIds::velocity]; }
double SessionSample::getSampleRate() const         { return tree.getProperty (sessionIds::sampleRate, 48000.0); }
int SessionSample::getNumFrames() const             { return juce::jmax (0, (int) tree[sessionIds::numFrames]); }
int SessionSample::getNumChannels() const           { return tree.getProperty (sessionIds::numChannels, 2); }
bool SessionSample::isLocked() const                { return tree[sessionIds::locked]; }
int SessionSample::getRoundRobin() const            { return juce::jmax (0, (int) tree[sessionIds::roundRobin]); }
int SessionSample::getVariation() const             { return tree.getProperty (sessionIds::variation, -1); }
bool SessionSample::isExported() const              { return tree.getProperty (sessionIds::exported, true); }

void SessionSample::setExported (bool shouldBeExported)
{
    if (shouldBeExported != isExported())
        tree.setProperty (sessionIds::exported, shouldBeExported, undoManager);
}

double SessionSample::getLengthSeconds() const
{
    return getSampleRate() > 0.0 ? getNumFrames() / getSampleRate() : 0.0;
}

bool SessionSample::isLoopEnabled() const
{
    return (bool) tree[sessionIds::loopEnabled] && getNumFrames() >= minLoopLength;
}

int SessionSample::getLoopStart() const
{
    const auto numFrames = getNumFrames();
    const auto start = (int) tree.getProperty (sessionIds::loopStart, -1);
    const auto end = (int) tree.getProperty (sessionIds::loopEnd, -1);

    if (start < 0 || end <= start)
        return getDefaultLoop (numFrames).getStart();

    return juce::jlimit (0, juce::jmax (0, numFrames - minLoopLength), start);
}

int SessionSample::getLoopEnd() const
{
    const auto numFrames = getNumFrames();
    const auto start = (int) tree.getProperty (sessionIds::loopStart, -1);
    const auto end = (int) tree.getProperty (sessionIds::loopEnd, -1);

    if (start < 0 || end <= start)
        return getDefaultLoop (numFrames).getEnd();

    return juce::jlimit (juce::jmin (numFrames, getLoopStart() + minLoopLength), numFrames, end);
}

juce::Range<int> SessionSample::getDefaultLoop (int numFrames)
{
    if (numFrames < minLoopLength)
        return { 0, juce::jmax (0, numFrames) };

    const auto start = juce::jmin (numFrames / 2, numFrames - minLoopLength);
    const auto end = juce::jmax (start + minLoopLength, (int) ((int64_t) numFrames * 9 / 10));
    return { start, juce::jmin (end, numFrames) };
}

void SessionSample::setLoopEnabled (bool shouldBeEnabled)
{
    if (isLocked() || (shouldBeEnabled && getNumFrames() < minLoopLength))
        return;

    // Beim ersten Einschalten die Standardpunkte festschreiben
    if (shouldBeEnabled)
        setLoop (getLoopStart(), getLoopEnd());

    tree.setProperty (sessionIds::loopEnabled, shouldBeEnabled, undoManager);
}

void SessionSample::setLoopStart (int frame)
{
    setLoop (frame, getLoopEnd());
}

void SessionSample::setLoopEnd (int frame)
{
    setLoop (getLoopStart(), frame);
}

void SessionSample::setLoop (int start, int end)
{
    const auto numFrames = getNumFrames();

    if (isLocked() || numFrames < minLoopLength)
        return;

    start = juce::jlimit (0, numFrames - minLoopLength, start);
    end = juce::jlimit (start + minLoopLength, numFrames, end);

    tree.setProperty (sessionIds::loopStart, start, undoManager);
    tree.setProperty (sessionIds::loopEnd, end, undoManager);
}

void SessionSample::setLocked (bool shouldBeLocked)
{
    tree.setProperty (sessionIds::locked, shouldBeLocked, undoManager);
}

int SessionSample::getCrossfade() const
{
    return juce::jlimit (0, getMaxCrossfade (getLoopStart(), getLoopEnd()), (int) tree[sessionIds::crossfade]);
}

CrossfadeMode SessionSample::getCrossfadeMode() const
{
    return parseCrossfadeMode (tree[sessionIds::crossfadeMode], LoopSearchSettings{}.crossfadeMode);
}

std::optional<LoopAlgorithm> SessionSample::getLoopAlgorithm() const
{
    return parseLoopAlgorithm (tree[sessionIds::loopAlgorithm]);
}

float SessionSample::getLoopScore() const
{
    return tree.getProperty (sessionIds::loopScore, -1.0f);
}

void SessionSample::setCrossfade (int frames)
{
    if (isLocked())
        return;

    tree.setProperty (sessionIds::crossfade, juce::jlimit (0, getMaxCrossfade (getLoopStart(), getLoopEnd()), frames), undoManager);
}

void SessionSample::setCrossfadeMode (CrossfadeMode mode)
{
    if (! isLocked())
        tree.setProperty (sessionIds::crossfadeMode, toString (mode), undoManager);
}

void SessionSample::setLoopScore (float score)
{
    tree.setProperty (sessionIds::loopScore, score < 0.0f ? -1.0f : juce::jlimit (0.0f, 100.0f, score), nullptr);
}

bool SessionSample::applyLoopResult (const LoopResult& result, CrossfadeMode mode)
{
    if (isLocked() || ! result.found || getNumFrames() < minLoopLength)
        return false;

    setLoop (result.start, result.end);
    setCrossfade (result.crossfade);
    setCrossfadeMode (mode);
    tree.setProperty (sessionIds::loopAlgorithm, toString (result.algorithm), undoManager);
    tree.setProperty (sessionIds::loopScore, juce::jlimit (0.0f, 100.0f, result.score), undoManager);
    tree.setProperty (sessionIds::loopEnabled, true, undoManager);
    return true;
}

//==============================================================================
Session::Session()
    : state (sessionIds::session)
{
    clear();
    state.addListener (this);
}

Session::~Session()
{
    state.removeListener (this);
}

juce::String Session::getName() const
{
    return state[sessionIds::name];
}

void Session::setName (const juce::String& newName)
{
    state.setProperty (sessionIds::name, newName.trim(), nullptr);
}

juce::File Session::getSamplesFolder() const
{
    return packageFolder != juce::File() ? packageFolder.getChildFile (samplesFolderName) : juce::File();
}

bool Session::isEmpty() const
{
    return getNumSamples() == 0 && ! getPluginDescription().has_value();
}

void Session::setDirty (bool shouldBeDirty)
{
    if (replacing || dirty == shouldBeDirty)
        return;

    dirty = shouldBeDirty;
    sendChangeMessage();
}

//==============================================================================
std::optional<juce::PluginDescription> Session::getPluginDescription() const
{
    const auto tree = state.getChildWithName (pluginType);

    if (! tree.isValid())
        return std::nullopt;

    juce::PluginDescription desc;

    if (auto xml = tree.createXml(); xml != nullptr && desc.loadFromXml (*xml))
        return desc;

    return std::nullopt;
}

std::optional<SamplingSettings> Session::getSamplingSettings() const
{
    const auto tree = state.getChildWithName (SamplingSettings::getValueTreeType());

    if (! tree.isValid())
        return std::nullopt;

    return SamplingSettings::fromValueTree (tree);
}

LoopSearchSettings Session::getLoopSearchSettings() const
{
    return LoopSearchSettings::fromValueTree (state.getChildWithName (LoopSearchSettings::getValueTreeType()));
}

void Session::setLoopSearchSettings (const LoopSearchSettings& settings)
{
    auto tree = state.getChildWithName (LoopSearchSettings::getValueTreeType());

    if (! tree.isValid())
    {
        tree = juce::ValueTree (LoopSearchSettings::getValueTreeType());
        state.appendChild (tree, nullptr);
    }

    tree.copyPropertiesFrom (settings.toValueTree(), nullptr);
}

ExportSettings Session::getExportSettings() const
{
    return ExportSettings::fromValueTree (state.getChildWithName (ExportSettings::getValueTreeType()));
}

void Session::setExportSettings (const ExportSettings& settings)
{
    auto tree = state.getChildWithName (ExportSettings::getValueTreeType());

    if (! tree.isValid())
    {
        tree = juce::ValueTree (ExportSettings::getValueTreeType());
        state.appendChild (tree, nullptr);
    }

    tree.copyPropertiesFrom (settings.toValueTree(), nullptr);
}

LoopHints Session::getLoopHints (const juce::ValueTree& sampleTree) const
{
    const auto sample = SessionSample (sampleTree, nullptr);
    LoopHints hints;
    hints.note = sample.getNote();

    // Aufnahme beginnt mit dem Pre-Roll vor dem Note-On (bei getrimmtem Anfang)
    if (const auto settings = getSamplingSettings())
        hints.sustainEndFrame = juce::roundToInt ((settings->holdSeconds + (settings->trimStart ? settings->preRollMs * 0.001 : 0.0))
                                                  * sample.getSampleRate());

    return hints;
}

//==============================================================================
juce::ValueTree Session::getSamplesTree() const
{
    return state.getChildWithName (sessionIds::samples);
}

int Session::getNumSamples() const
{
    return getSamplesTree().getNumChildren();
}

SessionSample Session::getSample (int index)
{
    return wrap (getSamplesTree().getChild (index));
}

juce::File Session::getSampleFile (const juce::ValueTree& sampleTree) const
{
    const auto name = sampleTree[sessionIds::file].toString();

    if (name.isEmpty())
        return {};

    if (juce::File::isAbsolutePath (name))
        return juce::File (name);

    const auto folder = getSamplesFolder();
    return folder != juce::File() ? folder.getChildFile (name) : juce::File();
}

//==============================================================================
void Session::replaceContent (const juce::ValueTree& newState, const juce::File& newPackageFolder,
                              juce::MemoryBlock newPluginState)
{
    {
        const juce::ScopedValueSetter<bool> guard (replacing, true);
        packageFolder = newPackageFolder;
        pluginState = std::move (newPluginState);
        state.copyPropertiesAndChildrenFrom (newState, nullptr);

        if (! getSamplesTree().isValid())
            state.appendChild (juce::ValueTree (sessionIds::samples), nullptr);
    }

    undoManager.clearUndoHistory();
    dirty = false;
    sendChangeMessage();
}

void Session::clear()
{
    juce::ValueTree empty (sessionIds::session);
    empty.appendChild (juce::ValueTree (sessionIds::samples), nullptr);
    replaceContent (empty, {}, {});
}

juce::String Session::beginRecording (const juce::File& newPackageFolder, const juce::String& name,
                                      const juce::PluginDescription& plugin, const juce::MemoryBlock& newPluginState,
                                      const SamplingSettings& settings)
{
    if (const auto result = newPackageFolder.getChildFile (samplesFolderName).createDirectory(); result.failed())
        return "Cannot create session folder: " + result.getErrorMessage();

    juce::ValueTree tree (sessionIds::session);
    tree.setProperty (sessionIds::name, name, nullptr);
    tree.setProperty (sessionIds::created, juce::Time::getCurrentTime().toISO8601 (true), nullptr);

    if (auto xml = plugin.createXml())
        tree.appendChild (juce::ValueTree::fromXml (*xml), nullptr);

    tree.appendChild (settings.toValueTree(), nullptr);
    tree.appendChild (juce::ValueTree (sessionIds::samples), nullptr);

    replaceContent (tree, newPackageFolder, newPluginState);
    return save();
}

void Session::addRecordedSample (const RecordedSample& recorded)
{
    const auto samplesFolder = getSamplesFolder();
    const auto fileName = recorded.file.isAChildOf (samplesFolder) ? recorded.file.getRelativePathFrom (samplesFolder)
                                                                    : recorded.file.getFullPathName();
    const auto loop = SessionSample::getDefaultLoop (recorded.numFrames);

    juce::ValueTree sample (sessionIds::sample);
    sample.setProperty (sessionIds::file, fileName, nullptr);
    sample.setProperty (sessionIds::note, recorded.request.note, nullptr);
    sample.setProperty (sessionIds::velocity, recorded.request.velocity, nullptr);
    sample.setProperty (sessionIds::sampleRate, recorded.sampleRate, nullptr);
    sample.setProperty (sessionIds::numFrames, recorded.numFrames, nullptr);
    sample.setProperty (sessionIds::numChannels, recorded.numChannels, nullptr);
    sample.setProperty (sessionIds::loopEnabled, false, nullptr);
    sample.setProperty (sessionIds::loopStart, loop.getStart(), nullptr);
    sample.setProperty (sessionIds::loopEnd, loop.getEnd(), nullptr);
    sample.setProperty (sessionIds::locked, false, nullptr);

    if (recorded.request.roundRobinCount > 1)
        sample.setProperty (sessionIds::roundRobin, recorded.request.roundRobin, nullptr);

    if (recorded.request.variation >= 0)
        sample.setProperty (sessionIds::variation, recorded.request.variation, nullptr);

    getSamplesTree().appendChild (sample, nullptr);
}

//==============================================================================
juce::String Session::writeFiles (const juce::File& folder) const
{
    if (const auto result = folder.createDirectory(); result.failed())
        return "Cannot create folder: " + result.getErrorMessage();

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", formatName);
    root->setProperty ("version", formatVersion);
    root->setProperty ("session", toJson (state));

    const auto json = juce::JSON::toString (juce::var (root), juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine));

    if (! folder.getChildFile (sessionFileName).replaceWithText (json + "\n"))
        return "Cannot write session.json in " + folder.getFullPathName();

    const auto stateFile = folder.getChildFile (pluginStateFileName);

    if (pluginState.isEmpty())
        stateFile.deleteFile();
    else if (! stateFile.replaceWithData (pluginState.getData(), pluginState.getSize()))
        return "Cannot write plugin-state.bin";

    return {};
}

juce::String Session::save()
{
    if (packageFolder == juce::File())
        return "The session has no location yet";

    const auto error = writeFiles (packageFolder);

    if (error.isEmpty())
        setDirty (false);

    return error;
}

juce::String Session::saveAs (const juce::File& target)
{
    const auto newFolder = withPackageExtension (target);

    if (newFolder == packageFolder)
        return save();

    if (newFolder.exists())
        return newFolder.getFileName() + " already exists";

    if (packageFolder != juce::File() && newFolder.isAChildOf (packageFolder))
        return "Destination is inside the session";

    const auto newSamplesFolder = newFolder.getChildFile (samplesFolderName);

    if (const auto result = newSamplesFolder.createDirectory(); result.failed())
        return "Cannot create folder: " + result.getErrorMessage();

    // Samples kopieren; absolute Pfade (außerhalb des Pakets) bleiben, wie sie sind
    for (const auto& sample : getSamplesTree())
    {
        const auto name = sample[sessionIds::file].toString();

        if (name.isEmpty() || juce::File::isAbsolutePath (name))
            continue;

        if (const auto source = getSampleFile (sample); source.existsAsFile())
        {
            const auto destination = newSamplesFolder.getChildFile (name);
            destination.getParentDirectory().createDirectory();

            if (! source.copyFileTo (destination))
            {
                newFolder.deleteRecursively();
                return name + " cannot be copied";
            }
        }
    }

    {
        const juce::ScopedValueSetter<bool> guard (replacing, true);
        packageFolder = newFolder;
        setName (newFolder.getFileNameWithoutExtension());
    }

    dirty = true;
    return save();
}

juce::String Session::load (const juce::File& file)
{
    const auto folder = file.isDirectory() ? file : file.getParentDirectory();
    const auto sessionFile = folder.getChildFile (sessionFileName);

    if (! sessionFile.existsAsFile())
        return "No session found in " + folder.getFullPathName();

    juce::var json;

    if (const auto result = juce::JSON::parse (sessionFile.loadFileAsString(), json); result.failed())
        return "session.json is invalid: "_u + result.getErrorMessage();

    if (json["format"].toString() != formatName)
        return "Unknown file format";

    if ((int) json["version"] > formatVersion)
        return "The session comes from a newer version of Autosampler";

    auto tree = fromJson (json["session"]);

    if (! tree.hasType (sessionIds::session))
        return "session.json contains no session"_u;

    if (tree[sessionIds::name].toString().isEmpty())
        tree.setProperty (sessionIds::name, folder.getFileNameWithoutExtension(), nullptr);

    juce::MemoryBlock newPluginState;
    folder.getChildFile (pluginStateFileName).loadFileAsData (newPluginState);

    replaceContent (tree, folder, std::move (newPluginState));
    return {};
}

//==============================================================================
juce::var Session::toJson (const juce::ValueTree& tree)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("type", tree.getType().toString());

    auto* properties = new juce::DynamicObject();

    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto name = tree.getPropertyName (i);
        properties->setProperty (name, tree[name]);
    }

    object->setProperty ("properties", juce::var (properties));

    if (tree.getNumChildren() > 0)
    {
        juce::Array<juce::var> children;

        for (const auto& child : tree)
            children.add (toJson (child));

        object->setProperty ("children", children);
    }

    return juce::var (object);
}

juce::ValueTree Session::fromJson (const juce::var& json)
{
    auto* object = json.getDynamicObject();

    if (object == nullptr)
        return {};

    const auto type = object->getProperty ("type").toString();

    if (! juce::Identifier::isValidIdentifier (type))
        return {};

    juce::ValueTree tree { juce::Identifier (type) };

    if (auto* properties = object->getProperty ("properties").getDynamicObject())
        for (const auto& property : properties->getProperties())
            tree.setProperty (property.name, property.value, nullptr);

    if (auto* children = object->getProperty ("children").getArray())
        for (const auto& child : *children)
            if (auto childTree = fromJson (child); childTree.isValid())
                tree.appendChild (childTree, nullptr);

    return tree;
}

juce::File Session::withPackageExtension (const juce::File& file)
{
    // Nicht withFileExtension(): "My.Synth" soll nicht zu "My.asession" werden
    return file.hasFileExtension (packageExtension) ? file : file.getSiblingFile (file.getFileName() + packageExtension);
}

} // namespace autosampler
