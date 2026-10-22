#include "LoopCrossfade.h"

namespace autosampler
{

namespace
{
    constexpr float bandFrequencies[] { 300.0f, 3000.0f };
    constexpr float bandLengths[] { 1.0f, 0.6f, 0.3f };     // tief, mittel, hoch
    constexpr int filterPadding = 2048;

    /** Tiefpass (RBJ, Q = 0,707) vorwärts und rückwärts: nullphasig,
        damit x - Tiefpass(x) exakt das Komplement ist. */
    void zeroPhaseLowpass (std::vector<float>& data, double sampleRate, float frequency)
    {
        const auto w0 = juce::MathConstants<double>::twoPi * juce::jmin ((double) frequency, sampleRate * 0.45) / sampleRate;
        const auto alpha = std::sin (w0) / juce::MathConstants<double>::sqrt2;     // sin / (2 Q)
        const auto cosw = std::cos (w0);
        const auto a0 = 1.0 + alpha;
        const auto b0 = (1.0 - cosw) * 0.5 / a0, b1 = (1.0 - cosw) / a0, b2 = b0;
        const auto a1 = -2.0 * cosw / a0, a2 = (1.0 - alpha) / a0;

        const auto run = [&] (auto begin, auto end)
        {
            double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

            // Startzustand auf den ersten Wert einschwingen, sonst springt der Filter
            if (begin != end)
                x1 = x2 = y1 = y2 = *begin;

            for (auto it = begin; it != end; ++it)
            {
                const double x0 = *it;
                const auto y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = x0; y2 = y1; y1 = y0;
                *it = (float) y0;
            }
        };

        run (data.begin(), data.end());
        run (data.rbegin(), data.rend());
    }

    /** Kopiert [from, from + length) eines Kanals, außerhalb des Puffers wird der Randwert gehalten. */
    std::vector<float> copySegment (const juce::AudioBuffer<float>& buffer, int channel, int from, int length)
    {
        std::vector<float> segment ((size_t) length);
        const auto* data = buffer.getReadPointer (channel);
        const auto last = buffer.getNumSamples() - 1;

        for (int i = 0; i < length; ++i)
            segment[(size_t) i] = data[juce::jlimit (0, last, from + i)];

        return segment;
    }

    struct Bands
    {
        std::array<std::vector<float>, 3> band;
    };

    /** Korrelation zweier Abschnitte, begrenzt auf [0, 1]. */
    float correlate (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t length)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;

        for (size_t i = from; i < from + length; ++i)
        {
            ab += (double) a[i] * b[i];
            aa += (double) a[i] * a[i];
            bb += (double) b[i] * b[i];
        }

        return aa > 1.0e-12 && bb > 1.0e-12 ? (float) juce::jlimit (0.0, 1.0, ab / std::sqrt (aa * bb)) : 0.0f;
    }

    /** S-Kurve, so normiert, dass die Leistung bei Korrelation r konstant bleibt:
        r = 1 ergibt Summe 1 (wie linear), r = 0 gleiche Leistung. */
    CrossfadeGains getAdaptiveGains (float t, float correlation) noexcept
    {
        const auto in = getCrossfadeGains (CrossfadeMode::sCurve, t).fadeIn;
        const auto out = 1.0f - in;
        const auto power = in * in + out * out + 2.0f * correlation * in * out;
        const auto scale = power > 1.0e-9f ? 1.0f / std::sqrt (power) : 1.0f;
        return { out * scale, in * scale };
    }

    Bands splitBands (std::vector<float> segment, double sampleRate)
    {
        auto low = segment, lowMid = segment;
        zeroPhaseLowpass (low, sampleRate, bandFrequencies[0]);
        zeroPhaseLowpass (lowMid, sampleRate, bandFrequencies[1]);

        Bands bands;
        bands.band[0] = low;
        bands.band[1].resize (segment.size());
        bands.band[2].resize (segment.size());

        for (size_t i = 0; i < segment.size(); ++i)
        {
            bands.band[1][i] = lowMid[i] - low[i];
            bands.band[2][i] = segment[i] - lowMid[i];
        }

        return bands;
    }
}

juce::String toString (CrossfadeMode mode)
{
    switch (mode)
    {
        case CrossfadeMode::linear:     return "linear";
        case CrossfadeMode::equalPower: return "equalPower";
        case CrossfadeMode::sCurve:     return "sCurve";
        case CrossfadeMode::spectral:   return "spectral";
    }

    return "equalPower";
}

CrossfadeMode parseCrossfadeMode (const juce::String& text, CrossfadeMode fallback)
{
    for (const auto mode : { CrossfadeMode::linear, CrossfadeMode::equalPower, CrossfadeMode::sCurve, CrossfadeMode::spectral })
        if (text == toString (mode))
            return mode;

    return fallback;
}

juce::String getDisplayName (CrossfadeMode mode)
{
    switch (mode)
    {
        case CrossfadeMode::linear:     return "1 Linear";
        case CrossfadeMode::equalPower: return "2 Equal Power";
        case CrossfadeMode::sCurve:     return "3 S-Curve";
        case CrossfadeMode::spectral:   return "4 Spectral";
    }

    return {};
}

CrossfadeGains getCrossfadeGains (CrossfadeMode mode, float t) noexcept
{
    t = juce::jlimit (0.0f, 1.0f, t);

    switch (mode)
    {
        case CrossfadeMode::linear:
            return { 1.0f - t, t };

        case CrossfadeMode::sCurve:
        {
            const auto in = t * t * (3.0f - 2.0f * t);
            return { 1.0f - in, in };
        }

        case CrossfadeMode::equalPower:
        case CrossfadeMode::spectral:
            break;
    }

    const auto angle = t * juce::MathConstants<float>::halfPi;
    return { std::cos (angle), std::sin (angle) };
}

int getMaxCrossfade (int loopStart, int loopEnd) noexcept
{
    return juce::jmax (0, juce::jmin (loopStart, loopEnd - loopStart));
}

void applyLoopCrossfade (juce::AudioBuffer<float>& buffer, int loopStart, int loopEnd, int length,
                         CrossfadeMode mode, double sampleRate)
{
    if (loopStart < 0 || loopEnd > buffer.getNumSamples() || loopEnd <= loopStart)
        return;

    const auto fadeLength = juce::jmin (length, getMaxCrossfade (loopStart, loopEnd));

    if (fadeLength <= 0)
        return;

    // Ziel [E - L, E) liegt hinter S, Quelle [S - L, S) davor: keine Überlappung
    const auto target = loopEnd - fadeLength;
    const auto source = loopStart - fadeLength;

    if (mode != CrossfadeMode::spectral)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int i = 0; i < fadeLength; ++i)
            {
                const auto gains = getCrossfadeGains (mode, ((float) i + 0.5f) / (float) fadeLength);
                data[target + i] = data[target + i] * gains.fadeOut + data[source + i] * gains.fadeIn;
            }
        }

        return;
    }

    const auto segmentLength = fadeLength + 2 * filterPadding;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto loopTail = splitBands (copySegment (buffer, channel, target - filterPadding, segmentLength), sampleRate);
        const auto preLoop = splitBands (copySegment (buffer, channel, source - filterPadding, segmentLength), sampleRate);
        auto* data = buffer.getWritePointer (channel);

        // Alle Bänder enden bei E, tiefe Bänder beginnen früher
        std::array<int, 3> offsets {}, lengths {};
        std::array<float, 3> correlations {};

        for (size_t b = 0; b < 3; ++b)
        {
            lengths[b] = juce::jmax (1, juce::roundToInt ((float) fadeLength * bandLengths[b]));
            offsets[b] = fadeLength - lengths[b];
            correlations[b] = correlate (loopTail.band[b], preLoop.band[b],
                                         (size_t) (filterPadding + offsets[b]), (size_t) lengths[b]);
        }

        for (int i = 0; i < fadeLength; ++i)
        {
            const auto index = (size_t) (filterPadding + i);
            float value = 0.0f;

            for (size_t b = 0; b < 3; ++b)
            {
                const auto gains = i < offsets[b] ? CrossfadeGains {}
                                                  : getAdaptiveGains (((float) (i - offsets[b]) + 0.5f) / (float) lengths[b],
                                                                      correlations[b]);

                value += loopTail.band[b][index] * gains.fadeOut + preLoop.band[b][index] * gains.fadeIn;
            }

            data[target + i] = value;
        }
    }
}

} // namespace autosampler
