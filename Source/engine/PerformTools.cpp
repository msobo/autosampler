#include "PerformTools.h"

namespace autosampler
{

namespace
{
    inline size_t slot (int channel, int note) noexcept
    {
        return (size_t) (juce::jlimit (1, 16, channel) - 1) * 128 + (size_t) juce::jlimit (0, 127, note);
    }
}

//==============================================================================
void ChordProcessor::setChord (ChordType type, int inversion)
{
    const auto chord = getChordIntervals (type, inversion);
    numIntervals = juce::jlimit (1, maxChordNotes, chord.count);

    for (int i = 0; i < numIntervals; ++i)
        intervals[(size_t) i] = chord.values[(size_t) i];
}

void ChordProcessor::process (const juce::MidiBuffer& input, juce::MidiBuffer& output)
{
    for (const auto metadata : input)
    {
        const auto message = metadata.getMessage();
        const auto position = metadata.samplePosition;

        if (message.isNoteOn())
        {
            noteOn (message.getChannel(), message.getNoteNumber(), message.getVelocity(), output, position);
        }
        else if (message.isNoteOff())
        {
            noteOff (message.getChannel(), message.getNoteNumber(), output, position);
        }
        else
        {
            if (message.isAllNotesOff() || message.isAllSoundOff())
                reset (output, position);

            output.addEvent (message, position);
        }
    }
}

void ChordProcessor::noteOn (int channel, int note, juce::uint8 velocity, juce::MidiBuffer& output, int position)
{
    auto& mapping = mappings[slot (channel, note)];

    // Doppeltes Note-On derselben Taste: erst den alten Akkord beenden
    if (mapping.count > 0)
        noteOff (channel, note, output, position);

    for (int i = 0; i < numIntervals; ++i)
    {
        const auto target = note + intervals[(size_t) i];

        if (! juce::isPositiveAndBelow (target, 128))
            continue;

        if (soundingCount[slot (channel, target)]++ == 0)
            output.addEvent (juce::MidiMessage::noteOn (channel, target, velocity), position);

        mapping.notes[(size_t) mapping.count++] = (juce::int8) target;
    }
}

void ChordProcessor::noteOff (int channel, int note, juce::MidiBuffer& output, int position)
{
    auto& mapping = mappings[slot (channel, note)];

    for (int i = 0; i < mapping.count; ++i)
    {
        const auto target = (int) mapping.notes[(size_t) i];
        auto& count = soundingCount[slot (channel, target)];

        if (count > 0 && --count == 0)
            output.addEvent (juce::MidiMessage::noteOff (channel, target), position);
    }

    mapping.count = 0;
}

void ChordProcessor::reset (juce::MidiBuffer& output, int position)
{
    for (int channel = 1; channel <= 16; ++channel)
        for (int note = 0; note < 128; ++note)
            if (auto& count = soundingCount[slot (channel, note)]; count > 0)
            {
                count = 0;
                output.addEvent (juce::MidiMessage::noteOff (channel, note), position);
            }

    for (auto& mapping : mappings)
        mapping.count = 0;
}

//==============================================================================
void PatternPlayer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    numHeld = numPending = 0;
    running = false;
    stepCounter = 0;
    currentStep = -1;
}

double PatternPlayer::getStepLength() const noexcept
{
    return juce::jmax (1.0, 60.0 / juce::jlimit (20.0, 300.0, settings.bpm) * getBeatsPerStep (settings.rate) * sampleRate);
}

void PatternPlayer::setSettings (const PerformSettings& newSettings, juce::MidiBuffer& output)
{
    const auto oldMode = settings.pattern;
    const auto latchReleased = settings.latch && ! newSettings.latch;
    settings = newSettings;

    if (oldMode != settings.pattern)
    {
        // Durchgereichte Noten bzw. laufende Pattern-Noten beenden
        if (oldMode == PatternMode::off)
        {
            for (int i = 0; i < numHeld; ++i)
                output.addEvent (juce::MidiMessage::noteOff (held[(size_t) i].channel, held[(size_t) i].note), 0);
        }
        else
        {
            flushPendingOffs (output, 0);
        }

        running = settings.pattern != PatternMode::off && numHeld > 0;
        stepCounter = 0;
        nextStepTime = 0.0;
        currentStep = -1;
    }

    if (latchReleased || settings.pattern == PatternMode::off)
    {
        for (int i = numHeld; --i >= 0;)
            if (! held[(size_t) i].down)
                removeHeld (i);

        if (numHeld == 0)
            stop (output, 0);
    }
}

void PatternPlayer::process (const juce::MidiBuffer& input, juce::MidiBuffer& output, int numSamples)
{
    int position = 0;

    for (const auto metadata : input)
    {
        const auto time = juce::jlimit (0, juce::jmax (0, numSamples - 1), metadata.samplePosition);
        advance (output, position, time);
        position = time;

        const auto message = metadata.getMessage();

        if (message.isNoteOnOrOff())
        {
            handleNote (message, output, time);
        }
        else
        {
            if (message.isAllNotesOff() || message.isAllSoundOff())
                reset (output, time);

            output.addEvent (message, time);
        }
    }

    advance (output, position, numSamples);

    nextStepTime -= numSamples;

    for (int i = 0; i < numPending; ++i)
        pending[(size_t) i].time -= numSamples;
}

void PatternPlayer::handleNote (const juce::MidiMessage& message, juce::MidiBuffer& output, int position)
{
    const auto note = message.getNoteNumber();
    const auto channel = message.getChannel();
    const auto patternActive = settings.pattern != PatternMode::off;

    if (! patternActive)
        output.addEvent (message, position);

    auto found = -1;

    for (int i = 0; i < numHeld; ++i)
        if (held[(size_t) i].note == note && held[(size_t) i].channel == channel)
            found = i;

    if (message.isNoteOn())
    {
        // Latch: die erste Taste nach dem Loslassen aller Tasten beginnt neu
        if (patternActive && settings.latch)
        {
            const auto anyDown = std::any_of (held.begin(), held.begin() + numHeld, [] (const HeldNote& h) { return h.down; });

            if (! anyDown)
            {
                numHeld = 0;
                found = -1;
            }
        }

        if (found < 0 && numHeld < (int) held.size())
            found = numHeld++;

        if (found >= 0)
            held[(size_t) found] = { note, channel, message.getVelocity(), ++orderCounter, true };

        if (patternActive && ! running && numHeld > 0)
        {
            running = true;
            stepCounter = 0;
            nextStepTime = position;
        }

        return;
    }

    if (found < 0)
        return;

    held[(size_t) found].down = false;

    if (! patternActive || ! settings.latch)
        removeHeld (found);

    if (numHeld == 0)
        stop (output, position);
}

void PatternPlayer::removeHeld (int index)
{
    if (! juce::isPositiveAndBelow (index, numHeld))
        return;

    // Reihenfolge ist egal, sie steckt in order
    held[(size_t) index] = held[(size_t) numHeld - 1];
    --numHeld;
}

void PatternPlayer::stop (juce::MidiBuffer&, int)
{
    // Ausstehende Note-Offs laufen zu ihrer Zeit aus
    running = false;
    currentStep = -1;
}

void PatternPlayer::advance (juce::MidiBuffer& output, int from, int to)
{
    for (;;)
    {
        int offIndex = -1;

        for (int i = 0; i < numPending; ++i)
            if (offIndex < 0 || pending[(size_t) i].time < pending[(size_t) offIndex].time)
                offIndex = i;

        const auto offTime = offIndex >= 0 ? pending[(size_t) offIndex].time : std::numeric_limits<double>::max();
        const auto stepTime = running ? nextStepTime : std::numeric_limits<double>::max();
        if (offIndex < 0 && ! running)
            return;

        const auto offFirst = offTime <= stepTime;
        const auto time = offFirst ? offTime : stepTime;

        const auto sample = (int) std::ceil (time - 1.0e-6);

        if (sample >= to)
            return;

        const auto position = juce::jmax (from, sample);

        if (offFirst)
        {
            const auto off = pending[(size_t) offIndex];
            pending[(size_t) offIndex] = pending[(size_t) --numPending];
            output.addEvent (juce::MidiMessage::noteOff (off.channel, off.note), position);
        }
        else
        {
            playStep (output, position);
            nextStepTime += getStepLength();
        }
    }
}

void PatternPlayer::playStep (juce::MidiBuffer& output, int position)
{
    if (numHeld == 0)
        return;

    int note = -1, channel = 1, velocity = 100;

    if (settings.pattern == PatternMode::arp)
    {
        std::array<int, 128> order {};

        for (int i = 0; i < numHeld; ++i)
            order[(size_t) i] = i;

        const auto byPlayed = settings.arpMode == ArpMode::asPlayed;
        std::sort (order.begin(), order.begin() + numHeld, [&] (int a, int b)
        {
            const auto& x = held[(size_t) a];
            const auto& y = held[(size_t) b];
            return byPlayed ? x.order < y.order : (x.note != y.note ? x.note < y.note : x.channel < y.channel);
        });

        const auto octaves = juce::jlimit (1, 4, settings.arpOctaves);
        const auto length = numHeld * octaves;
        const auto counter = stepCounter;
        int index = 0;

        switch (settings.arpMode)
        {
            case ArpMode::up:
            case ArpMode::asPlayed:
                index = counter % length;
                break;

            case ArpMode::down:
                index = length - 1 - counter % length;
                break;

            case ArpMode::upDown:
            {
                const auto cycle = juce::jmax (1, 2 * length - 2);
                const auto k = counter % cycle;
                index = length <= 1 ? 0 : (k < length ? k : cycle - k);
                break;
            }

            case ArpMode::random:
                index = random.nextInt (length);
                break;
        }

        const auto& source = held[(size_t) order[(size_t) (index % numHeld)]];
        note = source.note + 12 * (index / numHeld);
        channel = source.channel;
        velocity = source.velocity;
        currentStep = index;

        // Oktaven über 127 entfallen: auf die höchste spielbare zurückfallen
        while (note > 127)
            note -= 12;
    }
    else if (settings.pattern == PatternMode::sequencer)
    {
        const auto length = juce::jlimit (1, PerformSettings::maxSteps, settings.sequencerLength);
        const auto index = stepCounter % length;
        const auto& step = settings.steps[(size_t) index];
        currentStep = index;

        const auto* root = &held[0];

        for (int i = 1; i < numHeld; ++i)
            if (held[(size_t) i].order > root->order)
                root = &held[(size_t) i];

        if (step.active)
        {
            note = juce::jlimit (0, 127, root->note + step.offset);
            channel = root->channel;
            velocity = juce::jlimit (1, 127, step.velocity);
        }
    }

    ++stepCounter;

    if (note < 0)
        return;

    // Derselbe Ton klingt noch (Gate 100 %): erst beenden
    for (int i = 0; i < numPending; ++i)
        if (pending[(size_t) i].note == note && pending[(size_t) i].channel == channel)
        {
            output.addEvent (juce::MidiMessage::noteOff (channel, note), position);
            pending[(size_t) i] = pending[(size_t) --numPending];
            break;
        }

    output.addEvent (juce::MidiMessage::noteOn (channel, note, (juce::uint8) velocity), position);

    const auto gateLength = juce::jmax (1.0, getStepLength() * juce::jlimit (0.05f, 1.0f, settings.gate));

    if (numPending < (int) pending.size())
        pending[(size_t) numPending++] = { note, channel, position + gateLength - 0.5 };
    else
        output.addEvent (juce::MidiMessage::noteOff (channel, note), position);
}

void PatternPlayer::flushPendingOffs (juce::MidiBuffer& output, int position)
{
    for (int i = 0; i < numPending; ++i)
        output.addEvent (juce::MidiMessage::noteOff (pending[(size_t) i].channel, pending[(size_t) i].note), position);

    numPending = 0;
}

void PatternPlayer::reset (juce::MidiBuffer& output, int position)
{
    flushPendingOffs (output, position);

    if (settings.pattern == PatternMode::off)
        for (int i = 0; i < numHeld; ++i)
            output.addEvent (juce::MidiMessage::noteOff (held[(size_t) i].channel, held[(size_t) i].note), position);

    numHeld = 0;
    running = false;
    stepCounter = 0;
    currentStep = -1;
}

} // namespace autosampler
