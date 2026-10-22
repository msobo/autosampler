#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace autosampler
{

/** Erkennt das Ende eines Ausklangs: Der RMS-Pegel (über alle Kanäle,
    in kurzen Fenstern) muss für eine Mindestdauer unter der Schwelle liegen.
    Blöcke beliebiger Größe werden fortlaufend eingespeist.
*/
class TailDetector
{
public:
    TailDetector (double sampleRate, float thresholdDb,
                  double requiredSilenceSeconds = 0.3, double windowSeconds = 0.01);

    void reset() noexcept;

    /** Verarbeitet die Samples [start, start + num). Liefert true, sobald die
        Stille lange genug anhält. */
    bool process (const juce::AudioBuffer<float>&, int start, int num) noexcept;

    bool isFinished() const noexcept    { return finished; }

private:
    int windowSize;
    int requiredSilence;
    float thresholdSquared;

    double sumSquares = 0.0;
    int samplesInWindow = 0;
    int silentSamples = 0;
    bool finished = false;
};

/** Index des ersten Samples (über alle Kanäle), dessen Betrag die Schwelle
    überschreitet; -1, wenn keines. */
int findFirstSampleAbove (const juce::AudioBuffer<float>&, float thresholdGain);

/** Ende des Klangs: Ende des letzten RMS-Fensters über der Schwelle.
    0, wenn das Signal komplett darunter liegt. */
int findSoundEnd (const juce::AudioBuffer<float>&, float thresholdGain, int windowSize);

/** Lineares Ausblenden auf den letzten fadeLength Samples des Puffers. */
void applyFadeOut (juce::AudioBuffer<float>&, int fadeLength);

struct TrimSettings
{
    bool trimStart = true;
    float startThresholdDb = -60.0f;
    float endThresholdDb = -70.0f;
    int preRollSamples = 0;
    int fadeOutSamples = 0;
    int windowSamples = 480;
};

/** Entfernt Stille am Anfang (mit Pre-Roll) und am Ende, blendet aus.
    Liefert false, wenn das Signal nie die Startschwelle erreicht. */
bool trimSample (juce::AudioBuffer<float>&, const TrimSettings&);

/** Gain in dB und Panorama (-1 … +1). Mono wird beim Panorama stereo
    (Constant Power), Stereo wird über die Balance geregelt (die Gegenseite
    wird leiser, die Mitte bleibt unverändert). */
void applyGainAndPan (juce::AudioBuffer<float>&, float gainDb, float pan);

/** Pegel in dBFS: lautestes RMS-Fenster (100 ms, Schritt 50 ms) über alle Kanäle.
    Kürzere Samples werden als Ganzes gemessen; -100 bei Stille. */
float measureLevelDb (const juce::AudioBuffer<float>&, double sampleRate);

/** Unterschied zweier Aufnahmen in dB relativ zum Pegel von reference
    (Energie der Differenz über die gemeinsame Länge). -100 bei identischem Audio. */
float measureDifferenceDb (const juce::AudioBuffer<float>& reference, const juce::AudioBuffer<float>& other);

/** Wandelt die Sample-Rate (Windowed Sinc, latenzkompensiert). */
juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>&, double sourceRate, double targetRate);

} // namespace autosampler
