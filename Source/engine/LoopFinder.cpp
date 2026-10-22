#include "LoopFinder.h"

#include <juce_dsp/juce_dsp.h>

namespace autosampler
{

namespace
{
    namespace ids
    {
        const juce::Identifier loopSearch       { "LoopSearch" };
        const juce::Identifier algorithm        { "algorithm" };
        const juce::Identifier crossfadeMode    { "crossfadeMode" };
        const juce::Identifier crossfadeSeconds { "crossfadeSeconds" };
        const juce::Identifier padAmount        { "padAmount" };
        const juce::Identifier minLoopSeconds   { "minLoopSeconds" };
    }

    constexpr int numMelBands = 32;
    constexpr int padContext = 3;               // Frames links und rechts für den Fingerprint-Vergleich
    constexpr int padMaxFrames = 800;
    constexpr float universalLengthWeight = 0.1f, tonalLengthWeight = 0.1f, padLengthWeight = 0.25f;

    std::vector<float> makeMono (const juce::AudioBuffer<float>& audio)
    {
        const auto numChannels = audio.getNumChannels();
        std::vector<float> mono ((size_t) audio.getNumSamples(), 0.0f);

        if (numChannels == 0)
            return mono;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* data = audio.getReadPointer (channel);

            for (size_t i = 0; i < mono.size(); ++i)
                mono[i] += data[i];
        }

        const auto scale = 1.0f / (float) numChannels;

        for (auto& value : mono)
            value *= scale;

        return mono;
    }

    std::vector<int> findRisingZeroCrossings (const std::vector<float>& x, int begin, int end)
    {
        std::vector<int> crossings;
        begin = juce::jmax (1, begin);
        end = juce::jmin ((int) x.size() - 1, end);

        for (int i = begin; i <= end; ++i)
            if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
                crossings.push_back (i);

        return crossings;
    }

    std::vector<int> crossingsWithin (const std::vector<int>& crossings, int begin, int end)
    {
        std::vector<int> result;

        for (const auto c : crossings)
            if (c >= begin && c <= end)
                result.push_back (c);

        return result;
    }

    /** Gleichmäßig ausgedünnt auf höchstens maxCount Einträge. */
    std::vector<int> thinOut (const std::vector<int>& values, size_t maxCount)
    {
        if (values.size() <= maxCount || maxCount == 0)
            return values;

        std::vector<int> result;
        result.reserve (maxCount);

        for (size_t i = 0; i < maxCount; ++i)
            result.push_back (values[i * (values.size() - 1) / (maxCount - 1)]);

        return result;
    }

    //==============================================================================
    /** Vergleicht die Umgebung [-w, w) zweier Stellen: gewichtete normalisierte
        Kreuzkorrelation (Dreiecksfenster, die Mitte zählt am meisten) mal Pegelverhältnis. */
    class JunctionMatcher
    {
    public:
        explicit JunctionMatcher (int halfWidthToUse)
            : halfWidth (juce::jmax (1, halfWidthToUse)), weights ((size_t) halfWidth * 2)
        {
            for (int k = 0; k < 2 * halfWidth; ++k)
                weights[(size_t) k] = 1.0f - std::abs ((float) (k - halfWidth) + 0.5f) / (float) halfWidth;
        }

        int getHalfWidth() const noexcept   { return halfWidth; }

        float match (const float* a, const float* b) const noexcept
        {
            double ab = 0.0, aa = 0.0, bb = 0.0;

            for (int k = 0; k < 2 * halfWidth; ++k)
            {
                const double w = weights[(size_t) k];
                const double va = a[k - halfWidth], vb = b[k - halfWidth];
                ab += w * va * vb;
                aa += w * va * va;
                bb += w * vb * vb;
            }

            if (aa < 1.0e-12 || bb < 1.0e-12)
                return 0.0f;

            const auto correlation = ab / std::sqrt (aa * bb);
            const auto levelRatio = std::sqrt (juce::jmin (aa, bb) / juce::jmax (aa, bb));
            return (float) (juce::jmax (0.0, correlation) * levelRatio);
        }

    private:
        int halfWidth;
        std::vector<float> weights;
    };

    //==============================================================================
    /** Log-Mel-Fingerprint eines Fensters, mittelwertfrei und auf Länge 1
        normiert; das Skalarprodukt zweier Fingerprints ist ihre Kosinus-Ähnlichkeit. */
    class Fingerprinter
    {
    public:
        Fingerprinter (double sampleRate, int order)
            : fft (order), size (1 << order), window ((size_t) size), fftData ((size_t) size * 2)
        {
            for (int i = 0; i < size; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (size - 1));

            const auto toMel = [] (double f) { return 2595.0 * std::log10 (1.0 + f / 700.0); };
            const auto fromMel = [] (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); };
            const auto melLow = toMel (40.0), melHigh = toMel (juce::jmin (16000.0, sampleRate * 0.49));
            const auto numBins = size / 2;

            std::array<double, numMelBands + 2> edges {};

            for (size_t i = 0; i < edges.size(); ++i)
                edges[i] = fromMel (melLow + (melHigh - melLow) * (double) i / (double) (edges.size() - 1)) / sampleRate * size;

            for (size_t band = 0; band < (size_t) numMelBands; ++band)
            {
                const auto low = edges[band], centre = edges[band + 1], high = edges[band + 2];

                for (int bin = juce::jmax (1, (int) std::floor (low)); bin <= juce::jmin (numBins, (int) std::ceil (high)); ++bin)
                {
                    const auto weight = bin < centre ? (bin - low) / juce::jmax (1.0e-9, centre - low)
                                                     : (high - bin) / juce::jmax (1.0e-9, high - centre);

                    if (weight > 0.0)
                        filters[band].push_back ({ bin, (float) weight });
                }

                // Schmale Bänder bei tiefen Frequenzen: mindestens das nächste Bin
                if (filters[band].empty())
                    filters[band].push_back ({ juce::jlimit (1, numBins, juce::roundToInt (centre)), 1.0f });
            }
        }

        int getSize() const noexcept    { return size; }

        /** Fingerprint des Fensters [start, start + size); außerhalb zählt 0.
            Liefert den RMS-Pegel des Fensters in dB. */
        float compute (const std::vector<float>& x, int start, float* out)
        {
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            double sumSquares = 0.0;

            for (int i = 0; i < size; ++i)
            {
                const auto index = start + i;
                const auto value = index >= 0 && index < (int) x.size() ? x[(size_t) index] : 0.0f;
                sumSquares += (double) value * value;
                fftData[(size_t) i] = value * window[(size_t) i];
            }

            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

            float mean = 0.0f;

            for (size_t band = 0; band < (size_t) numMelBands; ++band)
            {
                double energy = 0.0;

                for (const auto& [bin, weight] : filters[band])
                    energy += (double) weight * fftData[(size_t) bin] * fftData[(size_t) bin];

                out[band] = (float) std::log10 (1.0e-10 + energy);
                mean += out[band];
            }

            mean /= (float) numMelBands;
            float norm = 0.0f;

            for (int band = 0; band < numMelBands; ++band)
            {
                out[band] -= mean;
                norm += out[band] * out[band];
            }

            norm = std::sqrt (norm);

            for (int band = 0; band < numMelBands; ++band)
                out[band] = norm > 1.0e-6f ? out[band] / norm : 0.0f;

            return (float) (10.0 * std::log10 (1.0e-12 + sumSquares / size));
        }

        static float similarity (const float* a, const float* b) noexcept
        {
            float sum = 0.0f;

            for (int band = 0; band < numMelBands; ++band)
                sum += a[band] * b[band];

            return sum;
        }

    private:
        juce::dsp::FFT fft;
        int size;
        std::vector<float> window, fftData;
        std::array<std::vector<std::pair<int, float>>, numMelBands> filters;
    };

    //==============================================================================
    /** Bereich, in dem Loop-Punkte liegen dürfen: nach dem Anschlag
        (Pegel erstmals bei −6 dB der Spitze, plus 50 ms) bis kurz vor dem
        Note-Off bzw. bis der Pegel unter −40 dB der Spitze fällt. */
    struct Region
    {
        int begin = 0, end = 0;
        int getLength() const noexcept  { return end - begin; }
    };

    Region findSearchRegion (const std::vector<float>& x, double sampleRate, const LoopHints& hints, int guard)
    {
        const auto numFrames = (int) x.size();
        const auto window = juce::jmax (16, juce::roundToInt (sampleRate * 0.01));
        const auto numWindows = numFrames / window;

        if (numWindows < 4)
            return {};

        std::vector<float> envelope ((size_t) numWindows);

        for (int w = 0; w < numWindows; ++w)
        {
            double sum = 0.0;

            for (int i = 0; i < window; ++i)
                sum += (double) x[(size_t) (w * window + i)] * x[(size_t) (w * window + i)];

            envelope[(size_t) w] = (float) std::sqrt (sum / window);
        }

        const auto peak = *std::max_element (envelope.begin(), envelope.end());

        if (peak < 1.0e-5f)
            return {};

        int firstLoud = 0, lastAudible = 0;

        while (envelope[(size_t) firstLoud] < peak * 0.5f)
            ++firstLoud;

        for (int w = 0; w < numWindows; ++w)
            if (envelope[(size_t) w] >= peak * 0.01f)
                lastAudible = w;

        const auto audibleEnd = juce::jmin (numFrames - guard, (lastAudible + 1) * window);
        Region region;
        region.begin = juce::jmax (guard, (firstLoud + 1) * window + juce::roundToInt (sampleRate * 0.05));
        region.end = audibleEnd;

        if (hints.sustainEndFrame > 0)
            region.end = juce::jmin (region.end, hints.sustainEndFrame);

        region.end -= window;

        // Zu kurz (kurzer Ton, langsamer Anschlag): die letzten zwei Drittel des hörbaren Teils
        if (region.getLength() < window * 10)
        {
            region.end = audibleEnd;
            region.begin = juce::jmax (guard, audibleEnd / 3);
        }

        region.end = juce::jmax (region.begin, region.end);
        return region;
    }

    //==============================================================================
    struct Candidate
    {
        int start = -1, end = -1;
        float total = -1.0f;
        bool isValid() const noexcept   { return start >= 0 && end > start; }
    };

    void evaluatePair (Candidate& best, const std::vector<float>& x, const JunctionMatcher& matcher,
                       int start, int end, float lengthWeight, int regionLength)
    {
        const auto total = matcher.match (&x[(size_t) end], &x[(size_t) start])
                         + lengthWeight * (float) (end - start) / (float) juce::jmax (1, regionLength);

        if (total > best.total)
            best = { start, end, total };
    }

    Candidate searchPairs (const std::vector<float>& x, const JunctionMatcher& matcher,
                           const std::vector<int>& starts, const std::vector<int>& ends,
                           int minLoop, float lengthWeight, int regionLength)
    {
        Candidate best;

        for (const auto start : starts)
            for (const auto end : ends)
                if (end - start >= minLoop)
                    evaluatePair (best, x, matcher, start, end, lengthWeight, regionLength);

        return best;
    }

    std::vector<int> neighbours (const std::vector<int>& crossings, int value, int count)
    {
        const auto it = std::lower_bound (crossings.begin(), crossings.end(), value);
        const auto index = (int) std::distance (crossings.begin(), it);
        std::vector<int> result;

        for (int i = juce::jmax (0, index - count); i < juce::jmin ((int) crossings.size(), index + count + 1); ++i)
            result.push_back (crossings[(size_t) i]);

        return result;
    }

    /** Universal: Paare steigender Nulldurchgänge, grob über eine Auswahl, dann
        über die benachbarten Nulldurchgänge des besten Paars verfeinert. */
    Candidate searchUniversal (const std::vector<float>& x, const Region& region, int minLoop,
                          const JunctionMatcher& matcher, int preferredStart)
    {
        const auto crossings = findRisingZeroCrossings (x, region.begin, region.end);

        if (crossings.size() < 2)
            return {};

        // S möglichst hinter dem gewünschten Crossfade, sofern dann noch Platz bleibt
        auto startBegin = region.begin;

        if (preferredStart > startBegin && region.end - preferredStart >= minLoop * 2)
            startBegin = preferredStart;

        const auto starts = thinOut (crossingsWithin (crossings, startBegin, region.end - minLoop), 300);
        const auto ends = thinOut (crossingsWithin (crossings, startBegin + minLoop, region.end), 200);
        auto best = searchPairs (x, matcher, starts, ends, minLoop, universalLengthWeight, region.getLength());

        if (! best.isValid())
            return best;

        const auto refined = searchPairs (x, matcher, neighbours (crossings, best.start, 12), neighbours (crossings, best.end, 12),
                                          minLoop, universalLengthWeight, region.getLength());

        return refined.total > best.total ? refined : best;
    }

    /** Tonal: Grundton per YIN, Loop-Länge ein ganzzahliges Vielfaches der Periode,
        bevorzugt mit geringem Phasenfehler; E wird um ±2 Frames verfeinert. */
    Candidate searchTonal (const std::vector<float>& x, double sampleRate, const Region& region, int minLoop,
                          const JunctionMatcher& matcher, const LoopHints& hints, double& period)
    {
        auto minFrequency = 25.0, maxFrequency = 4000.0;

        if (hints.note >= 0 && hints.note <= 127)
        {
            const auto expected = juce::MidiMessage::getMidiNoteInHertz (hints.note);
            minFrequency = juce::jmax (20.0, expected / 2.5);
            maxFrequency = juce::jmin (sampleRate / 4.0, expected * 2.5);
        }

        const auto analysisLength = juce::jmin (region.getLength(), 32768);
        const auto analysisStart = region.begin + (region.getLength() - analysisLength) / 2;
        period = detectPitchPeriod (&x[(size_t) analysisStart], analysisLength, sampleRate, minFrequency, maxFrequency);

        if (period <= 1.0)
            return {};

        const auto kMin = juce::jmax (1, (int) std::ceil (minLoop / period));
        const auto kMax = (int) std::floor (region.getLength() / period);

        if (kMax < kMin)
            return {};

        // Längste Vielfache mit kleinem Phasenfehler, dazu die genauesten
        const auto phaseError = [&] (int k) { return std::abs (k * period - std::round (k * period)); };
        std::vector<int> multiples;

        for (int k = kMax; k >= kMin && multiples.size() < 48; --k)
            if (phaseError (k) <= 0.2)
                multiples.push_back (k);

        std::vector<int> byError;

        for (int k = kMin; k <= kMax; ++k)
            byError.push_back (k);

        std::partial_sort (byError.begin(), byError.begin() + juce::jmin ((int) byError.size(), 16), byError.end(),
                           [&] (int a, int b) { return phaseError (a) < phaseError (b); });

        for (int i = 0; i < juce::jmin ((int) byError.size(), 16); ++i)
            if (std::find (multiples.begin(), multiples.end(), byError[(size_t) i]) == multiples.end())
                multiples.push_back (byError[(size_t) i]);

        const auto startEnd = juce::jmin (region.end - minLoop,
                                          region.begin + juce::jmax (juce::roundToInt (period * 8.0), region.getLength() / 4));
        auto starts = thinOut (crossingsWithin (findRisingZeroCrossings (x, region.begin, startEnd), region.begin, startEnd), 24);

        if (starts.empty())
            starts.push_back (region.begin);

        Candidate best;

        for (const auto start : starts)
        {
            for (const auto k : multiples)
            {
                const auto nominalEnd = start + (int) std::round (k * period);

                for (int end = nominalEnd - 2; end <= nominalEnd + 2; ++end)
                    if (end <= region.end && end - start >= minLoop)
                        evaluatePair (best, x, matcher, start, end, tonalLengthWeight, region.getLength());
            }
        }

        return best;
    }

    /** Pad: Log-Mel-Fingerprints im Raster; gesucht werden lange Abschnitte,
        deren Umgebung bei S und E spektral und im Pegel übereinstimmt. Danach
        Verfeinerung auf Nulldurchgänge in der Nähe. */
    Candidate searchPad (const std::vector<float>& x, double sampleRate, const Region& region, int minLoop,
                           const JunctionMatcher& matcher)
    {
        Fingerprinter fingerprinter (sampleRate, sampleRate > 60000.0 ? 12 : 11);
        const auto frameSize = fingerprinter.getSize();
        const auto hop = juce::jmax (frameSize / 4, region.getLength() / padMaxFrames);
        const auto numFrames = region.getLength() / hop + 1;

        if (numFrames < 4)
            return {};

        const auto totalFrames = numFrames + 2 * padContext;
        std::vector<float> prints ((size_t) totalFrames * numMelBands);
        std::vector<float> levels ((size_t) totalFrames);

        for (int t = 0; t < totalFrames; ++t)
        {
            const auto centre = region.begin + (t - padContext) * hop;
            levels[(size_t) t] = fingerprinter.compute (x, centre - frameSize / 2, &prints[(size_t) t * numMelBands]);
        }

        const auto print = [&] (int frame) { return &prints[(size_t) (frame + padContext) * numMelBands]; };
        const auto level = [&] (int frame) { return levels[(size_t) (frame + padContext)]; };
        const auto minLoopFrames = juce::jmax (1, (int) std::ceil ((double) minLoop / hop));
        constexpr auto contextSize = (float) (2 * padContext + 1);

        int bestI = -1, bestJ = -1;
        float bestTotal = -1.0f;

        for (int i = 0; i < numFrames; ++i)
        {
            for (int j = i + minLoopFrames; j < numFrames; ++j)
            {
                float similarity = 0.0f, levelDifference = 0.0f;

                for (int c = -padContext; c <= padContext; ++c)
                {
                    similarity += Fingerprinter::similarity (print (i + c), print (j + c));
                    levelDifference += std::abs (level (i + c) - level (j + c));
                }

                const auto total = juce::jmax (0.0f, similarity / contextSize) * std::exp (-levelDifference / contextSize / 3.0f)
                                 + padLengthWeight * (float) (j - i) / (float) numFrames;

                if (total > bestTotal)
                {
                    bestTotal = total;
                    bestI = i;
                    bestJ = j;
                }
            }
        }

        if (bestI < 0)
            return {};

        const auto roughStart = region.begin + bestI * hop;
        const auto roughEnd = juce::jmin (region.end, region.begin + bestJ * hop);

        const auto crossingsNear = [&] (int position)
        {
            const auto from = juce::jmax (region.begin, position - hop), to = juce::jmin (region.end, position + hop);
            return thinOut (findRisingZeroCrossings (x, from, to), 32);
        };

        auto refined = searchPairs (x, matcher, crossingsNear (roughStart), crossingsNear (roughEnd), minLoop, 0.0f, region.getLength());

        if (! refined.isValid() && roughEnd - roughStart >= minLoop)
            refined = { roughStart, roughEnd, 0.0f };

        return refined;
    }

    //==============================================================================
    float scoreMono (const std::vector<float>& x, double sampleRate, int loopStart, int loopEnd,
                     int crossfade, CrossfadeMode mode)
    {
        const auto numFrames = (int) x.size();

        if (loopStart < 0 || loopEnd > numFrames || loopEnd - loopStart < 2 || sampleRate <= 0.0)
            return 0.0f;

        // Abgespielte Fassung: mit Crossfade
        juce::AudioBuffer<float> played (1, numFrames);
        std::copy (x.begin(), x.end(), played.getWritePointer (0));
        applyLoopCrossfade (played, loopStart, loopEnd, crossfade, mode, sampleRate);
        const auto* y = played.getReadPointer (0);

        // Übergang: Nach y[E-1] folgt y[S]. Nahtlos ist er, wenn die Frames vor E
        // denen vor S im Original gleichen (dahinter ist beides dasselbe Material).
        const auto width = juce::jmin (juce::jlimit (32, 1024, juce::roundToInt (sampleRate * 0.01)), loopStart, loopEnd - loopStart);
        float junction = 1.0f;

        if (width >= 4)
        {
            double ab = 0.0, aa = 0.0, bb = 0.0;

            for (int k = 1; k <= width; ++k)
            {
                const auto weight = 1.0 - (k - 1) / (double) width;     // nahe am Sprung zählt mehr
                const double a = y[loopEnd - k], b = x[(size_t) (loopStart - k)];
                ab += weight * a * b;
                aa += weight * a * a;
                bb += weight * b * b;
            }

            junction = aa > 1.0e-12 && bb > 1.0e-12
                         ? (float) (juce::jmax (0.0, ab / std::sqrt (aa * bb)) * std::sqrt (juce::jmin (aa, bb) / juce::jmax (aa, bb)))
                         : (aa <= 1.0e-12 && bb <= 1.0e-12 ? 1.0f : 0.0f);
        }

        // Klang am Loop-Anfang gegen Loop-Ende
        const auto loopLength = loopEnd - loopStart;
        auto order = 8;

        while (order < (sampleRate > 60000.0 ? 12 : 11) && (1 << (order + 1)) <= loopLength)
            ++order;

        Fingerprinter fingerprinter (sampleRate, order);
        const auto size = fingerprinter.getSize();
        std::array<float, numMelBands> begin {}, end {};
        std::vector<float> loopAudio (y + loopStart, y + loopEnd);
        const auto levelBegin = fingerprinter.compute (loopAudio, 0, begin.data());
        const auto levelEnd = fingerprinter.compute (loopAudio, loopLength - size, end.data());
        const auto spectral = juce::jmax (0.0f, Fingerprinter::similarity (begin.data(), end.data()));
        const auto levelMatch = std::pow (10.0f, -std::abs (levelBegin - levelEnd) / 40.0f);    // sqrt des Amplitudenverhältnisses

        return juce::jlimit (0.0f, 100.0f, 100.0f * (0.6f * junction + 0.4f * spectral) * levelMatch);
    }
}

//==============================================================================
juce::String toString (LoopAlgorithm algorithm)
{
    switch (algorithm)
    {
        case LoopAlgorithm::pad:       return "pad";
        case LoopAlgorithm::universal: return "universal";
        case LoopAlgorithm::tonal:     return "tonal";
    }

    return "pad";
}

std::optional<LoopAlgorithm> parseLoopAlgorithm (const juce::String& text)
{
    for (const auto algorithm : { LoopAlgorithm::pad, LoopAlgorithm::universal, LoopAlgorithm::tonal })
        if (text.equalsIgnoreCase (toString (algorithm)) || text.equalsIgnoreCase (getDisplayName (algorithm)))
            return algorithm;

    return std::nullopt;
}

juce::String getDisplayName (LoopAlgorithm algorithm)
{
    switch (algorithm)
    {
        case LoopAlgorithm::pad:       return "Pad";
        case LoopAlgorithm::universal: return "Universal";
        case LoopAlgorithm::tonal:     return "Tonal";
    }

    return {};
}

juce::ValueTree LoopSearchSettings::toValueTree() const
{
    juce::ValueTree tree (ids::loopSearch);
    tree.setProperty (ids::algorithm, toString (algorithm), nullptr);
    tree.setProperty (ids::crossfadeMode, toString (crossfadeMode), nullptr);
    tree.setProperty (ids::crossfadeSeconds, crossfadeSeconds, nullptr);
    tree.setProperty (ids::padAmount, padAmount, nullptr);
    tree.setProperty (ids::minLoopSeconds, minLoopSeconds, nullptr);
    return tree;
}

LoopSearchSettings LoopSearchSettings::fromValueTree (const juce::ValueTree& tree)
{
    LoopSearchSettings s;
    s.algorithm        = parseLoopAlgorithm (tree[ids::algorithm]).value_or (s.algorithm);
    s.crossfadeMode    = parseCrossfadeMode (tree[ids::crossfadeMode], s.crossfadeMode);
    s.crossfadeSeconds = juce::jlimit (0.0, 2.0, (double) tree.getProperty (ids::crossfadeSeconds, s.crossfadeSeconds));
    s.padAmount        = juce::jlimit (0.0f, 1.0f, (float) tree.getProperty (ids::padAmount, s.padAmount));
    s.minLoopSeconds   = juce::jlimit (0.001, 10.0, (double) tree.getProperty (ids::minLoopSeconds, s.minLoopSeconds));
    return s;
}

juce::Identifier LoopSearchSettings::getValueTreeType()
{
    return ids::loopSearch;
}

//==============================================================================
double detectPitchPeriod (const float* x, int numSamples, double sampleRate, double minFrequency, double maxFrequency)
{
    if (x == nullptr || numSamples < 64 || sampleRate <= 0.0 || minFrequency <= 0.0 || maxFrequency <= minFrequency)
        return 0.0;

    const auto maxTau = juce::jmin ((int) std::ceil (sampleRate / minFrequency), numSamples / 2 - 2);
    const auto minTau = juce::jmax (2, (int) std::floor (sampleRate / maxFrequency));

    if (maxTau <= minTau + 2)
        return 0.0;

    const auto windowSize = juce::jmin (numSamples - maxTau - 2, juce::jmax (2048, 2 * maxTau));

    // Differenzfunktion und kumulativ normierte Form (YIN, Schritte 2 und 3)
    std::vector<double> normalised ((size_t) maxTau + 2, 1.0);
    double runningSum = 0.0;

    for (int tau = 1; tau <= maxTau + 1; ++tau)
    {
        double difference = 0.0;

        for (int j = 0; j < windowSize; ++j)
        {
            const double delta = x[j] - x[j + tau];
            difference += delta * delta;
        }

        runningSum += difference;
        normalised[(size_t) tau] = runningSum > 0.0 ? difference * tau / runningSum : 1.0;
    }

    // Erstes lokales Minimum unter der Schwelle, sonst das globale Minimum
    constexpr double threshold = 0.15;
    int best = -1;

    for (int tau = minTau; tau <= maxTau; ++tau)
    {
        if (normalised[(size_t) tau] < threshold)
        {
            while (tau + 1 <= maxTau && normalised[(size_t) tau + 1] < normalised[(size_t) tau])
                ++tau;

            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        best = minTau;

        for (int tau = minTau; tau <= maxTau; ++tau)
            if (normalised[(size_t) tau] < normalised[(size_t) best])
                best = tau;

        if (normalised[(size_t) best] > 0.4)
            return 0.0;
    }

    // Parabolische Interpolation für Sub-Sample-Genauigkeit
    const auto a = normalised[(size_t) best - 1], m = normalised[(size_t) best], c = normalised[(size_t) best + 1];
    const auto denominator = a - 2.0 * m + c;
    const auto shift = denominator > 0.0 ? juce::jlimit (-1.0, 1.0, 0.5 * (a - c) / denominator) : 0.0;
    return best + shift;
}

float scoreLoop (const juce::AudioBuffer<float>& audio, double sampleRate, int loopStart, int loopEnd,
                 int crossfade, CrossfadeMode mode)
{
    return scoreMono (makeMono (audio), sampleRate, loopStart, loopEnd, crossfade, mode);
}

LoopResult findLoop (const juce::AudioBuffer<float>& audio, double sampleRate,
                     const LoopSearchSettings& settings, const LoopHints& hints)
{
    LoopResult result;
    result.algorithm = settings.algorithm;

    if (sampleRate <= 0.0 || audio.getNumSamples() < 256)
    {
        result.message = "Sample too short";
        return result;
    }

    const auto x = makeMono (audio);
    const auto numFrames = (int) x.size();
    const JunctionMatcher matcher (juce::jmin (juce::jlimit (32, 1024, juce::roundToInt (sampleRate * 0.01)), numFrames / 8));
    const auto region = findSearchRegion (x, sampleRate, hints, matcher.getHalfWidth());

    if (region.getLength() < 32)
    {
        result.message = "No sustained part found (too short or silent)";
        return result;
    }

    const auto minLoop = juce::jlimit (16, juce::jmax (16, region.getLength() / 2), juce::roundToInt (settings.minLoopSeconds * sampleRate));
    const auto wantedCrossfade = juce::roundToInt (juce::jmax (0.0, settings.crossfadeSeconds) * sampleRate);

    Candidate candidate;
    double period = 0.0;

    switch (settings.algorithm)
    {
        case LoopAlgorithm::universal:
            candidate = searchUniversal (x, region, minLoop, matcher, wantedCrossfade);
            break;

        case LoopAlgorithm::tonal:
            candidate = searchTonal (x, sampleRate, region, minLoop, matcher, hints, period);

            if (! candidate.isValid())
            {
                candidate = searchUniversal (x, region, minLoop, matcher, wantedCrossfade);
                result.message = "No fundamental found, looping with Universal";
                result.algorithm = LoopAlgorithm::universal;
            }
            break;

        case LoopAlgorithm::pad:
            candidate = searchPad (x, sampleRate, region, minLoop, matcher);

            if (! candidate.isValid())
            {
                candidate = searchUniversal (x, region, minLoop, matcher, wantedCrossfade);
                result.message = "Pad without result, looping with Universal";
                result.algorithm = LoopAlgorithm::universal;
            }
            break;
    }

    if (! candidate.isValid())
    {
        result.message = "No loop found";
        return result;
    }

    result.found = true;
    result.start = candidate.start;
    result.end = candidate.end;

    auto crossfade = wantedCrossfade;

    if (result.algorithm == LoopAlgorithm::tonal)
        crossfade = juce::jmin (crossfade, juce::roundToInt (2.0 * period));
    else if (result.algorithm == LoopAlgorithm::pad)
        crossfade = juce::jmax (crossfade, juce::roundToInt (settings.padAmount * 0.5f * (float) (result.end - result.start)));

    result.crossfade = juce::jlimit (0, getMaxCrossfade (result.start, result.end), crossfade);
    result.score = scoreMono (x, sampleRate, result.start, result.end, result.crossfade, settings.crossfadeMode);
    return result;
}

} // namespace autosampler
