#include "SamplerEngine.h"

namespace autosampler
{

namespace
{
    constexpr float stealReleaseSeconds = 0.005f;

    /** 4-Punkt-Hermite (Catmull-Rom) zwischen x0 und x1, t in [0, 1). */
    inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
    {
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
}

SamplerEngine::SamplerEngine()
{
    prepare (sampleRate);
}

void SamplerEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    for (auto& voice : voices)
    {
        voice.clear();
        voice.envelope.setSampleRate (sampleRate);
        voice.envelope.reset();
    }

    pitchBend.fill (0.0f);
    sustainPedal.fill (false);
    numActive = 0;
    auditionFrame = -1;
}

void SamplerEngine::setInstrument (std::shared_ptr<const SamplerInstrument> newInstrument)
{
    // Laufende Stimmen behalten ihr Instrument, neue Noten nutzen das neue
    instrument = std::move (newInstrument);
    roundRobin.fill (0);
}

void SamplerEngine::setSettings (const SamplerSettings& newSettings)
{
    settings = newSettings;
}

juce::ADSR::Parameters SamplerEngine::getEnvelopeParameters() const
{
    return { settings.attackMs * 0.001f, 0.0f, 1.0f, juce::jmax (0.001f, settings.releaseMs * 0.001f) };
}

//==============================================================================
void SamplerEngine::render (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi, int startSample, int numSamples)
{
    const auto renderVoices = [this, &output] (int from, int length)
    {
        if (length <= 0)
            return;

        for (auto& voice : voices)
            if (voice.isActive())
                renderVoice (voice, output, from, length);
    };

    int position = 0;

    for (const auto metadata : midi)
    {
        const auto time = juce::jlimit (0, numSamples, metadata.samplePosition);
        renderVoices (startSample + position, time - position);
        position = time;
        handleMidi (metadata.getMessage());
    }

    renderVoices (startSample + position, numSamples - position);

    // Zustand für die UI
    int active = 0;
    const Voice* audition = nullptr;

    for (const auto& voice : voices)
    {
        if (! voice.isActive())
            continue;

        ++active;

        if (voice.audition && (audition == nullptr || voice.age > audition->age))
            audition = &voice;
    }

    numActive = active;
    auditionFrame = audition != nullptr ? (int) audition->position : -1;

    if (audition != nullptr)
        auditionId = audition->zone->id;
}

void SamplerEngine::renderVoice (Voice& voice, juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    const auto& zone = *voice.zone;
    const auto& audio = *zone.audio;
    const auto numFrames = audio.getNumSamples();
    const auto loopLength = zone.loopEnd - zone.loopStart;
    const float* source[2] { audio.getReadPointer (0), audio.getReadPointer (juce::jmin (1, audio.getNumChannels() - 1)) };

    const auto fetch = [&] (const float* data, int index) noexcept -> float
    {
        if (zone.loop)
        {
            while (index >= zone.loopEnd)
                index -= loopLength;
        }
        else if (index >= numFrames)
        {
            return 0.0f;
        }

        return index < 0 ? 0.0f : data[index];
    };

    const auto bend = voice.audition ? 0.0f : pitchBend[(size_t) juce::jlimit (0, 15, voice.channel - 1)];
    const auto increment = voice.increment * (bend != 0.0f ? std::exp2 ((double) bend / 12.0) : 1.0);
    const auto gain = voice.gain * juce::Decibels::decibelsToGain (settings.gainDb);

    auto* left = output.getWritePointer (0);
    auto* right = output.getNumChannels() > 1 ? output.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        if (! zone.loop && voice.position >= numFrames)
        {
            voice.clear();
            return;
        }

        const auto index = (int) voice.position;
        const auto t = (float) (voice.position - index);
        const auto level = voice.envelope.getNextSample() * gain;

        float values[2];

        for (int c = 0; c < 2; ++c)
            values[c] = hermite (fetch (source[c], index - 1), fetch (source[c], index),
                                 fetch (source[c], index + 1), fetch (source[c], index + 2), t) * level;

        if (right != nullptr)
        {
            left[startSample + i] += values[0];
            right[startSample + i] += values[1];
        }
        else
        {
            left[startSample + i] += 0.5f * (values[0] + values[1]);
        }

        voice.position += increment;

        if (zone.loop && voice.position >= zone.loopEnd)
            voice.position -= loopLength;

        if (! voice.envelope.isActive())
        {
            voice.clear();
            return;
        }
    }
}

//==============================================================================
void SamplerEngine::handleMidi (const juce::MidiMessage& message)
{
    const auto channel = juce::jlimit (1, 16, message.getChannel());

    if (message.isNoteOn())
    {
        noteOn (channel, message.getNoteNumber(), message.getVelocity());
    }
    else if (message.isNoteOff())
    {
        noteOff (channel, message.getNoteNumber());
    }
    else if (message.isPitchWheel())
    {
        pitchBend[(size_t) channel - 1] = (float) (message.getPitchWheelValue() - 8192) / 8192.0f * (float) settings.pitchBendRange;
    }
    else if (message.isSustainPedalOn())
    {
        sustainPedal[(size_t) channel - 1] = true;
    }
    else if (message.isSustainPedalOff())
    {
        sustainPedal[(size_t) channel - 1] = false;

        for (auto& voice : voices)
            if (voice.isActive() && voice.sustained && voice.channel == channel)
                releaseVoice (voice);
    }
    else if (message.isAllNotesOff())
    {
        allNotesOff (false);
    }
    else if (message.isAllSoundOff())
    {
        allNotesOff (true);
    }
}

void SamplerEngine::noteOn (int channel, int key, int velocity)
{
    if (instrument == nullptr)
        return;

    const auto group = instrument->findGroup (key, velocity);

    if (group < 0)
        return;

    const auto& members = instrument->getGroup (group);
    size_t member = 0;

    if (members.size() > 1 && group < (int) roundRobin.size())
    {
        member = (size_t) roundRobin[(size_t) group] % members.size();
        roundRobin[(size_t) group] = (int) ((member + 1) % members.size());
    }

    const auto& zone = instrument->getZones()[(size_t) members[member]];

    if (auto* voice = allocateVoice())
        startVoice (*voice, zone, key, channel, velocity, std::exp2 ((key - zone.rootNote) / 12.0));
}

void SamplerEngine::noteOff (int channel, int key)
{
    for (auto& voice : voices)
    {
        if (! voice.isActive() || voice.audition || ! voice.keyDown || voice.key != key || voice.channel != channel)
            continue;

        voice.keyDown = false;

        if (voice.oneShot)
            continue;

        if (sustainPedal[(size_t) channel - 1])
            voice.sustained = true;
        else
            releaseVoice (voice);
    }
}

void SamplerEngine::releaseVoice (Voice& voice)
{
    voice.keyDown = false;
    voice.sustained = false;
    voice.envelope.noteOff();
}

SamplerEngine::Voice* SamplerEngine::allocateVoice()
{
    Voice* freeVoice = nullptr;
    Voice* oldest = nullptr;
    Voice* oldestReleased = nullptr;
    Voice* oldestStealing = nullptr;
    int playing = 0;

    for (auto& voice : voices)
    {
        if (! voice.isActive())
        {
            if (freeVoice == nullptr)
                freeVoice = &voice;

            continue;
        }

        if (voice.stealing)
        {
            if (oldestStealing == nullptr || voice.age < oldestStealing->age)
                oldestStealing = &voice;

            continue;
        }

        ++playing;

        if (oldest == nullptr || voice.age < oldest->age)
            oldest = &voice;

        if (! voice.keyDown && ! voice.sustained && (oldestReleased == nullptr || voice.age < oldestReleased->age))
            oldestReleased = &voice;
    }

    // Über der Polyphonie: bevorzugt eine losgelassene Stimme kurz ausblenden
    if (playing >= settings.maxVoices)
        if (auto* victim = oldestReleased != nullptr ? oldestReleased : oldest)
        {
            victim->envelope.setParameters ({ 0.0f, 0.0f, 1.0f, stealReleaseSeconds });
            victim->envelope.noteOff();
            victim->stealing = true;
            victim->keyDown = victim->sustained = false;
        }

    if (freeVoice != nullptr)
        return freeVoice;

    // Alle Plätze belegt (nur bei sehr vielen Stimmen im Ausblenden): hart übernehmen
    auto* taken = oldestStealing != nullptr ? oldestStealing : oldest;

    if (taken != nullptr)
        taken->clear();

    return taken;
}

void SamplerEngine::startVoice (Voice& voice, const SamplerZone& zone, int key, int channel, int velocity, double pitchRatio)
{
    voice.instrument = instrument;
    voice.zone = &zone;
    voice.key = key;
    voice.channel = channel;
    voice.keyDown = true;
    voice.sustained = false;
    voice.stealing = false;
    voice.oneShot = ! voice.audition && ! zone.loop && settings.oneShotWithoutLoop;
    voice.position = 0.0;
    voice.increment = pitchRatio * zone.sampleRate / sampleRate;
    voice.age = ++voiceCounter;

    // Innerhalb eines Layers folgt die Lautstärke der Velocity (relativ zur Aufnahme)
    const auto relative = juce::jmin (1.0f, (float) velocity / (float) juce::jmax (1, zone.velocity));
    voice.gain = (1.0f - settings.velocitySensitivity * (1.0f - relative)) * zone.gain;

    voice.envelope.setSampleRate (sampleRate);
    voice.envelope.setParameters (getEnvelopeParameters());
    voice.envelope.reset();
    voice.envelope.noteOn();
}

//==============================================================================
void SamplerEngine::startAudition (juce::int64 sampleId, int velocity)
{
    stopAudition();

    if (instrument == nullptr)
        return;

    const auto index = instrument->findZone (sampleId);

    if (index < 0)
        return;

    const auto& zone = instrument->getZones()[(size_t) index];

    if (auto* voice = allocateVoice())
    {
        voice->audition = true;
        startVoice (*voice, zone, zone.rootNote, 1, velocity, 1.0);
    }
}

void SamplerEngine::stopAudition()
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.audition && voice.keyDown)
            releaseVoice (voice);
}

void SamplerEngine::allNotesOff (bool immediately)
{
    for (auto& voice : voices)
    {
        if (! voice.isActive())
            continue;

        if (immediately)
            voice.clear();
        else
            releaseVoice (voice);
    }

    sustainPedal.fill (false);
}

} // namespace autosampler
