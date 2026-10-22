#include "NoteRenderer.h"
#include "Text.h"

namespace autosampler
{

namespace
{
    int getRecordChannels (const juce::AudioProcessor& processor)
    {
        return processor.getMainBusNumOutputChannels();
    }

    int getProcessChannels (const juce::AudioProcessor& processor)
    {
        return juce::jmax (1, processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels());
    }

    void addAllNotesOff (juce::MidiBuffer& midi, int channel)
    {
        midi.addEvent (juce::MidiMessage::allNotesOff (channel), 0);
        midi.addEvent (juce::MidiMessage::allSoundOff (channel), 0);
    }
}

//==============================================================================
void NoteCapture::begin (const NoteRenderSpec& specToUse, int latencySamples, double sampleRate, int numChannels)
{
    spec = specToUse;
    spec.holdSamples = juce::jmax (1, spec.holdSamples);
    spec.maxTailSamples = juce::jmax (0, spec.maxTailSamples);

    latency = juce::jmax (0, latencySamples);
    blockPosition = 0;
    recording.setSize (juce::jmax (1, numChannels), spec.holdSamples + spec.maxTailSamples);
    recording.clear();
    tailDetector = std::make_unique<TailDetector> (sampleRate, spec.tailThresholdDb);
    numRecorded = 0;
    finished = false;
    hitMaxTail = false;
}

void NoteCapture::addMidi (juce::MidiBuffer& midi, int numSamples) const
{
    // Gleicher Zeitpunkt: der Controller steht im Puffer vor dem Note-On
    if (blockPosition == 0 && juce::isPositiveAndBelow (spec.controller, 128))
        midi.addEvent (juce::MidiMessage::controllerEvent (spec.channel, spec.controller,
                                                          juce::jlimit (0, 127, spec.controllerValue)), 0);

    if (blockPosition == 0)
        midi.addEvent (juce::MidiMessage::noteOn (spec.channel, spec.note, (juce::uint8) spec.velocity), 0);

    if (spec.holdSamples >= blockPosition && spec.holdSamples < blockPosition + numSamples)
        midi.addEvent (juce::MidiMessage::noteOff (spec.channel, spec.note), spec.holdSamples - blockPosition);
}

bool NoteCapture::consume (const juce::AudioBuffer<float>& pluginOutput, int numSamples) noexcept
{
    if (finished)
        return true;

    const auto capacity = recording.getNumSamples();
    const auto recordedBefore = numRecorded.load();

    // Die ersten "latency" Samples des Plugin-Ausgangs liegen vor dem Note-On
    const auto firstIndex = juce::jmax (0, latency - blockPosition);
    const auto targetStart = blockPosition + firstIndex - latency;
    const auto count = juce::jmin (numSamples - firstIndex, capacity - targetStart);

    blockPosition += numSamples;

    if (count <= 0)
        return false;

    const auto channels = juce::jmin (recording.getNumChannels(), pluginOutput.getNumChannels());

    for (int ch = 0; ch < channels; ++ch)
        recording.copyFrom (ch, targetStart, pluginOutput, ch, firstIndex, count);

    const auto recorded = targetStart + count;
    numRecorded = recorded;

    // Ausklang erst nach dem Note-Off bewerten
    const auto tailFrom = juce::jmax (recordedBefore, spec.holdSamples);

    if (recorded > tailFrom && tailDetector->process (recording, tailFrom, recorded - tailFrom))
        finished = true;
    else if (recorded >= capacity)
        finished = hitMaxTail = true;

    return finished;
}

RenderedNote NoteCapture::takeResult()
{
    RenderedNote result;
    result.audio = std::move (recording);
    result.audio.setSize (result.audio.getNumChannels(), numRecorded.load(), true);
    result.hitMaxTail = hitMaxTail;
    recording = {};
    return result;
}

//==============================================================================
juce::Optional<juce::AudioPlayHead::PositionInfo> FixedTempoPlayHead::getPosition() const
{
    constexpr double bpm = 120.0;
    const auto samples = position.load();
    const auto seconds = (double) samples / sampleRate;

    PositionInfo info;
    info.setTimeInSamples (samples);
    info.setTimeInSeconds (seconds);
    info.setBpm (bpm);
    info.setTimeSignature (TimeSignature { 4, 4 });
    info.setPpqPosition (seconds * bpm / 60.0);
    info.setPpqPositionOfLastBarStart (0.0);
    info.setIsPlaying (true);
    return info;
}

//==============================================================================
OfflineRenderer::OfflineRenderer (juce::AudioProcessor& processorToUse, double sampleRateToUse, int blockSizeToUse)
    : processor (processorToUse),
      sampleRate (sampleRateToUse),
      blockSize (blockSizeToUse),
      playHead (sampleRateToUse)
{
    processor.setPlayHead (&playHead);
    processor.setNonRealtime (true);
    processor.setProcessingPrecision (juce::AudioProcessor::singlePrecision);
    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    buffer.setSize (getProcessChannels (processor), blockSize);
    midi.ensureSize (1024);
}

OfflineRenderer::~OfflineRenderer()
{
    processor.releaseResources();
    processor.setNonRealtime (false);
    processor.setPlayHead (nullptr);
}

void OfflineRenderer::processBlock (juce::MidiBuffer& events)
{
    buffer.clear();

    {
        const juce::ScopedLock sl (processor.getCallbackLock());

        if (! processor.isSuspended())
            processor.processBlock (buffer, events);
    }

    playHead.advance (blockSize);
}

void OfflineRenderer::prepareNextNote (const std::function<void()>& restoreState, bool resetProcessor)
{
    midi.clear();

    for (int channel = 1; channel <= 16; ++channel)
        addAllNotesOff (midi, channel);

    processBlock (midi);

    if (restoreState != nullptr)
        restoreState();

    if (resetProcessor)
        processor.reset();

    playHead.reset();

    // Ausklingen lassen, bis nichts mehr zu hören ist (höchstens 2 s)
    TailDetector silence (sampleRate, -90.0f, 0.05);
    midi.clear();

    for (int rendered = 0; rendered < juce::roundToInt (sampleRate * 2.0); rendered += blockSize)
    {
        processBlock (midi);

        if (silence.process (buffer, 0, blockSize))
            break;
    }

    playHead.reset();
}

std::optional<RenderedNote> OfflineRenderer::render (const NoteRenderSpec& spec,
                                                     const std::function<bool()>& shouldAbort,
                                                     juce::String& error)
{
    const auto numChannels = getRecordChannels (processor);

    if (numChannels <= 0)
    {
        error = "Plugin has no audio outputs"_u;
        return std::nullopt;
    }

    capture.begin (spec, processor.getLatencySamples(), sampleRate, numChannels);

    while (! capture.isFinished())
    {
        if (shouldAbort != nullptr && shouldAbort())
            return std::nullopt;

        midi.clear();
        capture.addMidi (midi, blockSize);
        processBlock (midi);
        capture.consume (buffer, blockSize);
    }

    return capture.takeResult();
}

//==============================================================================
RealtimeRenderer::RealtimeRenderer (juce::AudioProcessor& processorToUse, juce::AudioDeviceManager& deviceManagerToUse)
    : processor (processorToUse),
      deviceManager (deviceManagerToUse)
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr || ! device->isPlaying())
    {
        initError = "No active audio device - realtime render not possible"_u;
        return;
    }

    sampleRate = device->getCurrentSampleRate();
    blockSize = juce::jmax (16, device->getCurrentBufferSizeSamples());
    playHead.setSampleRate (sampleRate);

    processor.setPlayHead (&playHead);
    processor.setNonRealtime (false);
    processor.setProcessingPrecision (juce::AudioProcessor::singlePrecision);
    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    buffer.setSize (getProcessChannels (processor), blockSize);
    midi.ensureSize (1024);

    deviceManager.addAudioCallback (this);
    callbackAdded = true;
}

RealtimeRenderer::~RealtimeRenderer()
{
    if (! callbackAdded)
        return;

    deviceManager.removeAudioCallback (this);
    processor.releaseResources();
    processor.setPlayHead (nullptr);
}

void RealtimeRenderer::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (! juce::approximatelyEqual (device->getCurrentSampleRate(), sampleRate))
        deviceChanged = true;
}

void RealtimeRenderer::audioDeviceStopped() {}

void RealtimeRenderer::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                         float* const* outputChannelData, int numOutputChannels,
                                                         int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    const auto recordChannels = juce::jmax (1, getRecordChannels (processor));

    for (int position = 0; position < numSamples;)
    {
        auto current = phase.load();

        if (current == Phase::paused)
        {
            pauseAcknowledged = true;
            return;
        }

        const auto num = juce::jmin (blockSize, numSamples - position);
        midi.clear();

        if (sendAllNotesOff.exchange (false))
            addAllNotesOff (midi, midiChannel);

        if (current == Phase::recording)
        {
            if (abortRequested.load())
            {
                addAllNotesOff (midi, midiChannel);
                phase = current = Phase::done;
            }
            else
            {
                capture.addMidi (midi, num);
            }
        }

        juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), num);
        block.clear();

        {
            const juce::ScopedTryLock sl (processor.getCallbackLock());

            if (sl.isLocked() && ! processor.isSuspended())
                processor.processBlock (block, midi);
        }

        playHead.advance (num);

        if (current == Phase::recording && capture.consume (block, num))
            phase = Phase::done;

        // Mithören über das Audio-Gerät
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[ch] + position,
                                                   block.getReadPointer (juce::jmin (ch, recordChannels - 1)), num);

        position += num;
    }
}

void RealtimeRenderer::setPausedAndWait (bool shouldPause)
{
    if (! shouldPause)
    {
        phase = Phase::idle;
        return;
    }

    pauseAcknowledged = false;
    phase = Phase::paused;

    // Läuft kein Audio-Callback mehr, kommt keine Bestätigung: dann nach 500 ms weiter
    for (int waited = 0; waited < 500 && ! pauseAcknowledged.load(); waited += 2)
        juce::Thread::sleep (2);
}

void RealtimeRenderer::prepareNextNote (const std::function<void()>& restoreState, bool resetProcessor)
{
    sendAllNotesOff = true;
    juce::Thread::sleep (50);

    // Während State und reset() nicht verarbeiten
    setPausedAndWait (true);

    if (restoreState != nullptr)
        restoreState();

    if (resetProcessor)
        processor.reset();

    playHead.reset();
    setPausedAndWait (false);

    juce::Thread::sleep (100);
}

std::optional<RenderedNote> RealtimeRenderer::render (const NoteRenderSpec& spec,
                                                      const std::function<bool()>& shouldAbort,
                                                      juce::String& error)
{
    if (deviceChanged.load())
    {
        error = "The sample rate of the audio device changed during the run"_u;
        return std::nullopt;
    }

    const auto numChannels = getRecordChannels (processor);

    if (numChannels <= 0)
    {
        error = "Plugin has no audio outputs"_u;
        return std::nullopt;
    }

    midiChannel = spec.channel;
    capture.begin (spec, processor.getLatencySamples(), sampleRate, numChannels);
    abortRequested = false;
    playHead.reset();
    phase = Phase::recording;

    auto lastRecorded = -1;
    auto lastProgress = juce::Time::getMillisecondCounter();

    while (phase.load() == Phase::recording)
    {
        if (shouldAbort != nullptr && shouldAbort())
            abortRequested = true;

        juce::Thread::sleep (5);

        const auto now = juce::Time::getMillisecondCounter();

        if (const auto recorded = capture.getNumRecorded(); recorded != lastRecorded)
        {
            lastRecorded = recorded;
            lastProgress = now;
        }
        else if (now - lastProgress > 3000)
        {
            auto expected = Phase::recording;

            if (phase.compare_exchange_strong (expected, Phase::idle))
            {
                error = "The audio device delivers no data"_u;
                return std::nullopt;
            }
        }
    }

    const auto aborted = abortRequested.load();
    std::optional<RenderedNote> result;

    if (! aborted)
        result = capture.takeResult();

    phase = Phase::idle;
    return result;
}

} // namespace autosampler
