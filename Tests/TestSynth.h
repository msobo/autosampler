#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace autosampler::test
{

/** Einfaches Test-Instrument: Sinus pro Note, Amplitude = Velocity / 127 · 0,5,
    linearer Release. Optional mit Latenz (Ausgang verzögert) und Stille im
    Offline-Modus, um Plugins nachzubilden, die offline nichts liefern.
    Der Parameter "level" und CC 7 skalieren die Amplitude; mit roundRobin
    wird jeder dritte Anschlag leiser (1, 0,9, 0,8), auch über reset() hinweg.
*/
class TestSynth final : public juce::AudioProcessor
{
public:
    explicit TestSynth (int latency = 0, double releaseSecondsToUse = 0.25, bool silentOffline = false)
        : AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          latencySamples (latency),
          releaseSeconds (releaseSecondsToUse),
          silentWhenOffline (silentOffline)
    {
        setLatencySamples (latency);
        addParameter (levelParameter = new juce::AudioParameterFloat (juce::ParameterID { "level", 1 }, "Level", 0.0f, 1.0f, 1.0f));
    }

    const juce::String getName() const override             { return "TestSynth"; }

    void prepareToPlay (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        reset();
    }

    void releaseResources() override {}

    void reset() override
    {
        delayLine.assign ((size_t) latencySamples + 1, 0.0f);
        delayPosition = 0;
        level = 0.0f;
        releaseStep = 0.0f;
        phase = 0.0;
        gate = false;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();

        auto events = midi.cbegin();

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            for (; events != midi.cend() && (*events).samplePosition <= i; ++events)
            {
                const auto message = (*events).getMessage();

                if (message.isController() && message.getControllerNumber() == 7)
                {
                    controllerGain = (float) message.getControllerValue() / 127.0f;
                }
                else if (message.isNoteOn())
                {
                    const auto variant = roundRobin ? 1.0f - 0.1f * (float) (numNoteOns++ % 3) : 1.0f;
                    frequency = juce::MidiMessage::getMidiNoteInHertz (message.getNoteNumber());
                    phase = 0.0;
                    level = message.getFloatVelocity() * 0.5f * gain * levelParameter->get() * controllerGain * variant;
                    releaseStep = 0.0f;
                    gate = true;
                }
                else if (message.isNoteOff() && gate)
                {
                    releaseStep = level / (float) juce::jmax (1.0, releaseSeconds * sampleRate);
                    gate = false;
                }
                else if (message.isAllNotesOff() || message.isAllSoundOff())
                {
                    level = 0.0f;
                    gate = false;
                }
            }

            auto sample = level * (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * frequency / sampleRate;

            if (! gate)
                level = juce::jmax (0.0f, level - releaseStep);

            // Latenz: Ausgang über eine Verzögerungsleitung
            delayLine[(size_t) delayPosition] = sample;
            delayPosition = (delayPosition + 1) % (int) delayLine.size();
            sample = delayLine[(size_t) delayPosition];

            if (silentWhenOffline && isNonRealtime())
                sample = 0.0f;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, sample);
        }
    }

    double getTailLengthSeconds() const override            { return releaseSeconds; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    juce::AudioProcessorEditor* createEditor() override     { return nullptr; }
    bool hasEditor() const override                         { return false; }
    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& data) override
    {
        data.replaceAll (&gain, sizeof (gain));
    }

    void setStateInformation (const void* data, int size) override
    {
        if (size == (int) sizeof (gain))
            std::memcpy (&gain, data, sizeof (gain));

        ++numStateRestores;
    }

    float gain = 1.0f;
    std::atomic<int> numStateRestores { 0 };
    bool roundRobin = false;
    juce::AudioParameterFloat* levelParameter = nullptr;

private:
    int latencySamples;
    double releaseSeconds;
    bool silentWhenOffline;

    double sampleRate = 48000.0;
    double frequency = 440.0;
    double phase = 0.0;
    float level = 0.0f;
    float releaseStep = 0.0f;
    bool gate = false;
    float controllerGain = 1.0f;
    int numNoteOns = 0;

    std::vector<float> delayLine;
    int delayPosition = 0;
};

} // namespace autosampler::test
