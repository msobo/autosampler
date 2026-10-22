#pragma once

#include "LivePlaySettings.h"
#include "SamplerInstrument.h"

namespace autosampler
{

/** Hörprüfung einzelner Samples aus dem Editor. Aufrufe vom Message-Thread. */
class SampleAudition
{
public:
    virtual ~SampleAudition() = default;

    virtual void startAudition (juce::int64 sampleId, int velocity) = 0;
    virtual void stopAudition() = 0;

    /** Abspielposition der Hörprüfung in Frames dieses Samples, -1 wenn es nicht spielt. */
    virtual int getAuditionPosition (juce::int64 sampleId) const = 0;
};

//==============================================================================
/** Spielt ein SamplerInstrument polyphon: Zone nach Taste und Velocity, Round Robin
    innerhalb einer Gruppe, Tonhöhe über die Abspielrate (Hermite-Interpolation).

    - Loop: Der Loop läuft, solange die Stimme klingt, auch im Release.
    - Ohne Loop: One Shot (Note-Off wird ignoriert) oder Release bei Note-Off.
    - Sustain-Pedal (CC 64), Pitch Bend, All Notes Off (CC 123), All Sound Off (CC 120).
    - Stimmenklau: die älteste Stimme blendet in 5 ms aus.

    Alle Methoden außer prepare() laufen auf dem Audio-Thread und allozieren nicht.
    Stimmen halten das Instrument per shared_ptr; freigegeben wird es vom Besitzer
    auf dem Message-Thread (siehe LivePlayProcessor). */
class SamplerEngine
{
public:
    static constexpr int numVoiceSlots = 160;

    SamplerEngine();

    void prepare (double sampleRate);

    void setInstrument (std::shared_ptr<const SamplerInstrument>);
    const SamplerInstrument* getInstrument() const noexcept      { return instrument.get(); }

    void setSettings (const SamplerSettings&);

    /** Addiert die Stimmen in output (Kanal 0 und 1) im Bereich [startSample, startSample + numSamples);
        MIDI-Zeitstempel relativ zu startSample. */
    void render (juce::AudioBuffer<float>& output, const juce::MidiBuffer&, int startSample, int numSamples);

    void startAudition (juce::int64 sampleId, int velocity);
    void stopAudition();

    /** Release aller Stimmen, bei immediately harter Abbruch. */
    void allNotesOff (bool immediately);

    int getNumActiveVoices() const noexcept                       { return numActive.load(); }
    juce::int64 getAuditionId() const noexcept                    { return auditionId.load(); }
    int getAuditionFrame() const noexcept                         { return auditionFrame.load(); }

private:
    struct Voice
    {
        std::shared_ptr<const SamplerInstrument> instrument;
        const SamplerZone* zone = nullptr;
        int key = -1, channel = 1;
        bool audition = false, keyDown = false, sustained = false, oneShot = false, stealing = false;
        double position = 0.0, increment = 1.0;
        float gain = 1.0f;
        juce::ADSR envelope;
        juce::uint64 age = 0;

        bool isActive() const noexcept  { return zone != nullptr; }
        void clear() noexcept           { instrument.reset(); zone = nullptr; audition = false; }
    };

    void handleMidi (const juce::MidiMessage&);
    void noteOn (int channel, int key, int velocity);
    void noteOff (int channel, int key);
    void releaseVoice (Voice&);
    Voice* allocateVoice();
    void startVoice (Voice&, const SamplerZone&, int key, int channel, int velocity, double pitchRatio);
    void renderVoice (Voice&, juce::AudioBuffer<float>&, int startSample, int numSamples);
    juce::ADSR::Parameters getEnvelopeParameters() const;

    double sampleRate = 48000.0;
    SamplerSettings settings;
    std::shared_ptr<const SamplerInstrument> instrument;
    std::array<Voice, numVoiceSlots> voices;
    std::array<int, 1024> roundRobin {};
    std::array<float, 16> pitchBend {};         // Halbtöne pro Kanal
    std::array<bool, 16> sustainPedal {};
    juce::uint64 voiceCounter = 0;

    std::atomic<int> numActive { 0 };
    std::atomic<juce::int64> auditionId { 0 };
    std::atomic<int> auditionFrame { -1 };
};

} // namespace autosampler
