#include "LivePlayProcessor.h"

namespace autosampler
{

/** Playhead für das Plugin: läuft immer, Tempo aus den Live-Einstellungen
    (für Plugins mit eigenem Arp oder tempo-synchronen LFOs). */
class LivePlayProcessor::PlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        const auto rate = sampleRate.load();
        const auto samples = position.load();
        const auto seconds = rate > 0.0 ? (double) samples / rate : 0.0;
        const auto tempo = bpm.load();

        PositionInfo info;
        info.setTimeInSamples (samples);
        info.setTimeInSeconds (seconds);
        info.setBpm (tempo);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setPpqPosition (seconds * tempo / 60.0);
        info.setIsPlaying (true);
        return info;
    }

    std::atomic<double> sampleRate { 48000.0 }, bpm { 120.0 };
    std::atomic<int64_t> position { 0 };
};

namespace
{
    constexpr int releaseIntervalMs = 500;
    constexpr int midiReserveBytes = 8192;
}

LivePlayProcessor::LivePlayProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      pluginPlayHead (std::make_unique<PlayHead>())
{
    for (auto* buffer : { &chordMidi, &patternMidi, &pluginMidi, &samplerMidi, &scratchMidi })
        buffer->ensureSize (midiReserveBytes);

    pendingSettings = settings;
    startTimer (releaseIntervalMs);
}

LivePlayProcessor::~LivePlayProcessor()
{
    stopTimer();
    setPlugin (nullptr);
}

//==============================================================================
void LivePlayProcessor::setPlugin (juce::AudioPluginInstance* newPlugin)
{
    juce::AudioPluginInstance* old = nullptr;

    {
        const juce::ScopedLock lock (getCallbackLock());

        if (newPlugin == plugin)
            return;

        old = std::exchange (plugin, nullptr);
    }

    if (old != nullptr && prepared)
    {
        old->releaseResources();
        old->setPlayHead (nullptr);
    }

    // Vorbereiten, solange der Audio-Thread das Plugin noch nicht sieht
    if (newPlugin != nullptr && prepared)
        preparePlugin (*newPlugin);

    const juce::ScopedLock lock (getCallbackLock());
    plugin = newPlugin;
}

void LivePlayProcessor::preparePlugin (juce::AudioPluginInstance& instance)
{
    instance.setNonRealtime (false);
    instance.setProcessingPrecision (juce::AudioProcessor::singlePrecision);
    instance.setPlayHead (pluginPlayHead.get());
    instance.setRateAndBufferSizeDetails (currentSampleRate, currentBlockSize);
    instance.prepareToPlay (currentSampleRate, currentBlockSize);

    const auto channels = juce::jmax (1, instance.getTotalNumInputChannels(), instance.getTotalNumOutputChannels());
    pluginBuffer.setSize (channels, juce::jmax (currentBlockSize, mixBuffer.getNumSamples()));
}

void LivePlayProcessor::setInstrument (std::shared_ptr<const SamplerInstrument> newInstrument)
{
    if (newInstrument != nullptr)
        releasePool.push_back (newInstrument);

    const juce::SpinLock::ScopedLockType lock (instrumentLock);
    pendingInstrument = std::move (newInstrument);
    instrumentChanged = true;
}

void LivePlayProcessor::setSettings (const LivePlaySettings& newSettings)
{
    settings = newSettings;

    const juce::SpinLock::ScopedLockType lock (settingsLock);
    pendingSettings = newSettings;
    settingsChanged = true;
}

void LivePlayProcessor::panic()
{
    pushCommand ({ Command::Type::panic });
}

void LivePlayProcessor::startAudition (juce::int64 sampleId, int velocity)
{
    pushCommand ({ Command::Type::auditionStart, sampleId, velocity });
}

void LivePlayProcessor::stopAudition()
{
    pushCommand ({ Command::Type::auditionStop });
}

int LivePlayProcessor::getAuditionPosition (juce::int64 sampleId) const
{
    const auto frame = sampler.getAuditionFrame();
    return frame >= 0 && sampler.getAuditionId() == sampleId ? frame : -1;
}

void LivePlayProcessor::pushCommand (const Command& command)
{
    const auto scope = commandFifo.write (1);

    if (scope.blockSize1 > 0)
        commands[(size_t) scope.startIndex1] = command;
}

void LivePlayProcessor::timerCallback()
{
    // Nur hier (Message-Thread) werden Instrumente gelöscht: wenn weder der
    // Sampler noch eine Stimme sie hält
    std::erase_if (releasePool, [] (const auto& instrument) { return instrument.use_count() <= 1; });
}

//==============================================================================
void LivePlayProcessor::prepareToPlay (double sampleRate, int blockSize)
{
    const juce::ScopedLock lock (getCallbackLock());

    currentSampleRate = sampleRate;
    currentBlockSize = juce::jmax (1, blockSize);
    allocateBuffers (currentBlockSize);

    sampler.prepare (sampleRate);
    pattern.prepare (sampleRate);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    pluginPlayHead->sampleRate = sampleRate;

    // Einstellungen beim nächsten Block neu übernehmen (Pattern, Chord, Reverb)
    {
        const juce::SpinLock::ScopedLockType settingsGuard (settingsLock);
        settingsChanged = true;
    }

    prepared = true;

    if (plugin != nullptr)
        preparePlugin (*plugin);
}

void LivePlayProcessor::releaseResources()
{
    const juce::ScopedLock lock (getCallbackLock());

    if (plugin != nullptr)
        plugin->releaseResources();

    prepared = false;
}

bool LivePlayProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto out = layout.getMainOutputChannelSet();
    return layout.inputBuses.isEmpty() && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void LivePlayProcessor::allocateBuffers (int blockSize)
{
    mixBuffer.setSize (2, blockSize, false, true, true);
    pluginBuffer.setSize (juce::jmax (2, pluginBuffer.getNumChannels()), blockSize, false, true, true);
}

//==============================================================================
void LivePlayProcessor::handleCommands()
{
    const auto scope = commandFifo.read (commandFifo.getNumReady());

    const auto run = [this] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& command = commands[(size_t) i];

            switch (command.type)
            {
                case Command::Type::auditionStart:
                    sampler.startAudition (command.sampleId, command.velocity);
                    break;

                case Command::Type::auditionStop:
                    sampler.stopAudition();
                    break;

                case Command::Type::panic:
                    chord.reset (scratchMidi, 0);
                    pattern.reset (scratchMidi, 0);
                    sampler.allNotesOff (true);
                    reverb.reset();

                    for (int channel = 1; channel <= 16; ++channel)
                    {
                        pluginMidi.addEvent (juce::MidiMessage::allNotesOff (channel), 0);
                        pluginMidi.addEvent (juce::MidiMessage::allSoundOff (channel), 0);
                    }

                    break;
            }
        }
    };

    run (scope.startIndex1, scope.blockSize1);
    run (scope.startIndex2, scope.blockSize2);
    scratchMidi.clear();
}

void LivePlayProcessor::applySettings()
{
    bool changed = false;

    if (const juce::SpinLock::ScopedTryLockType lock (settingsLock); lock.isLocked() && settingsChanged)
    {
        audioSettings = pendingSettings;
        settingsChanged = false;
        changed = true;
    }

    if (changed)
    {
        sampler.setSettings (audioSettings.sampler);
        chord.setChord (audioSettings.perform.chord, audioSettings.perform.inversion);
        pattern.setSettings (audioSettings.perform, patternMidi);
        pluginPlayHead->bpm = audioSettings.perform.bpm;

        const auto& r = audioSettings.reverb;
        juce::Reverb::Parameters parameters;
        parameters.roomSize = r.size;
        parameters.damping = r.damping;
        parameters.wetLevel = r.mix * 0.5f;
        parameters.dryLevel = 0.5f;                 // Freeverb verstärkt trocken um 2: 0,5 = unverändert
        parameters.width = 1.0f;
        reverb.setParameters (parameters);

        if (r.enabled && ! reverbActive)
            reverb.reset();

        reverbActive = r.enabled;
    }

    if (const juce::SpinLock::ScopedTryLockType lock (instrumentLock); lock.isLocked() && instrumentChanged)
    {
        sampler.setInstrument (std::move (pendingInstrument));
        pendingInstrument = nullptr;
        instrumentChanged = false;
    }
}

void LivePlayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();

    // Nur falls der Host größere Blöcke liefert als angekündigt
    if (numSamples > mixBuffer.getNumSamples())
        allocateBuffers (numSamples);

    for (auto* m : { &chordMidi, &patternMidi, &pluginMidi, &samplerMidi })
        m->clear();

    handleCommands();
    applySettings();

    chord.process (midi, chordMidi);
    pattern.process (chordMidi, patternMidi, numSamples);

    // A/B: die bisherige Quelle lässt ihre Noten los
    if (audioSettings.source != activeSource)
    {
        if (activeSource == PlaySource::plugin)
            for (int channel = 1; channel <= 16; ++channel)
                pluginMidi.addEvent (juce::MidiMessage::allNotesOff (channel), 0);
        else
            sampler.allNotesOff (false);

        activeSource = audioSettings.source;
    }

    (activeSource == PlaySource::plugin ? pluginMidi : samplerMidi).addEvents (patternMidi, 0, numSamples, 0);

    mixBuffer.clear (0, numSamples);

    if (plugin != nullptr)
    {
        const juce::ScopedLock pluginLock (plugin->getCallbackLock());

        if (! plugin->isSuspended())
        {
            juce::AudioBuffer<float> view (pluginBuffer.getArrayOfWritePointers(), pluginBuffer.getNumChannels(), numSamples);
            view.clear();
            plugin->processBlock (view, pluginMidi);

            const auto outputs = juce::jmin (view.getNumChannels(), juce::jmax (1, plugin->getTotalNumOutputChannels()));
            mixBuffer.addFrom (0, 0, view, 0, 0, numSamples);
            mixBuffer.addFrom (1, 0, view, juce::jmin (1, outputs - 1), 0, numSamples);
        }
    }

    pluginPlayHead->position += numSamples;

    sampler.render (mixBuffer, samplerMidi, 0, numSamples);

    if (reverbActive)
        reverb.processStereo (mixBuffer.getWritePointer (0), mixBuffer.getWritePointer (1), numSamples);

    buffer.clear();

    if (buffer.getNumChannels() >= 2)
    {
        buffer.copyFrom (0, 0, mixBuffer, 0, 0, numSamples);
        buffer.copyFrom (1, 0, mixBuffer, 1, 0, numSamples);
    }
    else if (buffer.getNumChannels() == 1)
    {
        buffer.addFrom (0, 0, mixBuffer, 0, 0, numSamples, 0.5f);
        buffer.addFrom (0, 0, mixBuffer, 1, 0, numSamples, 0.5f);
    }

    midi.clear();
}

} // namespace autosampler
