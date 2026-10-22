#include "AudioAnalysis.h"

namespace autosampler
{

TailDetector::TailDetector (double sampleRate, float thresholdDb, double requiredSilenceSeconds, double windowSeconds)
    : windowSize (juce::jmax (1, juce::roundToInt (sampleRate * windowSeconds))),
      requiredSilence (juce::jmax (1, juce::roundToInt (sampleRate * requiredSilenceSeconds))),
      thresholdSquared (juce::square (juce::Decibels::decibelsToGain (thresholdDb, -200.0f)))
{
}

void TailDetector::reset() noexcept
{
    sumSquares = 0.0;
    samplesInWindow = 0;
    silentSamples = 0;
    finished = false;
}

bool TailDetector::process (const juce::AudioBuffer<float>& buffer, int start, int num) noexcept
{
    const auto numChannels = buffer.getNumChannels();

    if (numChannels == 0)
        return finished = true;

    for (int i = start; i < start + num && ! finished; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            sumSquares += juce::square ((double) buffer.getSample (ch, i));

        if (++samplesInWindow < windowSize)
            continue;

        const auto meanSquare = sumSquares / (double) (windowSize * numChannels);
        silentSamples = meanSquare < (double) thresholdSquared ? silentSamples + windowSize : 0;
        finished = silentSamples >= requiredSilence;

        sumSquares = 0.0;
        samplesInWindow = 0;
    }

    return finished;
}

//==============================================================================
int findFirstSampleAbove (const juce::AudioBuffer<float>& buffer, float thresholdGain)
{
    int first = -1;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        const auto limit = first < 0 ? buffer.getNumSamples() : first;

        for (int i = 0; i < limit; ++i)
        {
            if (std::abs (data[i]) > thresholdGain)
            {
                first = i;
                break;
            }
        }
    }

    return first;
}

int findSoundEnd (const juce::AudioBuffer<float>& buffer, float thresholdGain, int windowSize)
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();
    windowSize = juce::jmax (1, windowSize);

    if (numChannels == 0)
        return 0;

    const auto thresholdSquared = (double) juce::square (thresholdGain);

    // Von hinten nach vorn: erstes Fenster über der Schwelle
    for (int windowEnd = numSamples; windowEnd > 0; windowEnd -= windowSize)
    {
        const auto windowStart = juce::jmax (0, windowEnd - windowSize);
        double sum = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);

            for (int i = windowStart; i < windowEnd; ++i)
                sum += juce::square ((double) data[i]);
        }

        if (sum / (double) ((windowEnd - windowStart) * numChannels) >= thresholdSquared)
            return windowEnd;
    }

    return 0;
}

void applyFadeOut (juce::AudioBuffer<float>& buffer, int fadeLength)
{
    fadeLength = juce::jmin (fadeLength, buffer.getNumSamples());

    if (fadeLength > 0)
        buffer.applyGainRamp (buffer.getNumSamples() - fadeLength, fadeLength, 1.0f, 0.0f);
}

bool trimSample (juce::AudioBuffer<float>& buffer, const TrimSettings& settings)
{
    const auto first = findFirstSampleAbove (buffer, juce::Decibels::decibelsToGain (settings.startThresholdDb, -200.0f));

    if (first < 0)
        return false;

    const auto start = settings.trimStart ? juce::jmax (0, first - settings.preRollSamples) : 0;
    const auto soundEnd = findSoundEnd (buffer, juce::Decibels::decibelsToGain (settings.endThresholdDb, -200.0f),
                                        settings.windowSamples);
    const auto end = juce::jlimit (first + 1, buffer.getNumSamples(), soundEnd);

    juce::AudioBuffer<float> trimmed (buffer.getNumChannels(), end - start);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        trimmed.copyFrom (ch, 0, buffer, ch, start, end - start);

    applyFadeOut (trimmed, settings.fadeOutSamples);
    buffer = std::move (trimmed);
    return true;
}

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source, double sourceRate, double targetRate)
{
    if (juce::approximatelyEqual (sourceRate, targetRate) || source.getNumSamples() == 0)
        return source;

    const auto ratio = sourceRate / targetRate;    // Eingangs-Samples pro Ausgangs-Sample
    const auto numIn = source.getNumSamples();
    const auto numOut = juce::roundToInt (numIn / ratio);

    // Das Interpolationsfilter verzögert um getBaseLatency() / ratio Ausgangs-Samples
    const auto latency = juce::roundToInt (juce::WindowedSincInterpolator::getBaseLatency() / ratio);

    juce::AudioBuffer<float> scratch (1, numOut + latency);
    juce::AudioBuffer<float> result (source.getNumChannels(), numOut);

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        juce::WindowedSincInterpolator interpolator;
        interpolator.process (ratio, source.getReadPointer (ch), scratch.getWritePointer (0),
                              scratch.getNumSamples(), numIn, 0);
        result.copyFrom (ch, 0, scratch, 0, latency, numOut);
    }

    return result;
}

//==============================================================================
void applyGainAndPan (juce::AudioBuffer<float>& audio, float gainDb, float pan)
{
    pan = juce::jlimit (-1.0f, 1.0f, pan);
    const auto gain = juce::Decibels::decibelsToGain (gainDb, -200.0f);

    if (! juce::approximatelyEqual (pan, 0.0f) && audio.getNumChannels() == 1)
    {
        // Constant Power: Mitte je -3 dB, ganz außen 0 dB auf einer Seite
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        juce::AudioBuffer<float> stereo (2, audio.getNumSamples());
        stereo.copyFrom (0, 0, audio.getReadPointer (0), audio.getNumSamples(), gain * std::cos (angle));
        stereo.copyFrom (1, 0, audio.getReadPointer (0), audio.getNumSamples(), gain * std::sin (angle));
        audio = std::move (stereo);
        return;
    }

    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        auto channelGain = gain;

        if (audio.getNumChannels() == 2)
            channelGain *= ch == 0 ? juce::jmin (1.0f, 1.0f - pan) : juce::jmin (1.0f, 1.0f + pan);

        if (! juce::approximatelyEqual (channelGain, 1.0f))
            audio.applyGain (ch, 0, audio.getNumSamples(), channelGain);
    }
}

float measureLevelDb (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    const auto numFrames = audio.getNumSamples();
    const auto numChannels = audio.getNumChannels();

    if (numFrames == 0 || numChannels == 0)
        return -100.0f;

    const auto window = juce::jlimit (1, numFrames, juce::roundToInt (0.1 * sampleRate));
    const auto hop = juce::jmax (1, window / 2);
    auto loudest = 0.0;

    for (int start = 0; start + window <= numFrames; start += hop)
    {
        auto sum = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* data = audio.getReadPointer (ch, start);

            for (int i = 0; i < window; ++i)
                sum += (double) data[i] * data[i];
        }

        loudest = juce::jmax (loudest, sum / ((double) window * numChannels));
    }

    return loudest > 0.0 ? juce::jmax (-100.0f, (float) (10.0 * std::log10 (loudest))) : -100.0f;
}

float measureDifferenceDb (const juce::AudioBuffer<float>& reference, const juce::AudioBuffer<float>& other)
{
    const auto numFrames = juce::jmin (reference.getNumSamples(), other.getNumSamples());
    const auto numChannels = juce::jmin (reference.getNumChannels(), other.getNumChannels());
    auto energy = 0.0, difference = 0.0;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* a = reference.getReadPointer (ch);
        const auto* b = other.getReadPointer (ch);

        for (int i = 0; i < numFrames; ++i)
        {
            energy += (double) a[i] * a[i];
            difference += ((double) a[i] - b[i]) * ((double) a[i] - b[i]);
        }
    }

    // Unterschiedliche Länge zählt als Unterschied
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto& longer = reference.getNumSamples() > numFrames ? reference : other;

        for (int i = numFrames; i < longer.getNumSamples(); ++i)
            difference += (double) longer.getSample (ch, i) * longer.getSample (ch, i);
    }

    if (difference <= 0.0)
        return -100.0f;

    if (energy <= 0.0)
        return 0.0f;

    return juce::jlimit (-100.0f, 100.0f, (float) (10.0 * std::log10 (difference / energy)));
}

} // namespace autosampler
