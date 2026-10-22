#include "ZoneSmoothing.h"

#include <map>
#include <set>

namespace autosampler
{

namespace
{
    constexpr float silentDb = -90.0f;
}

std::vector<float> computeZoneSmoothing (const std::vector<ZoneLevel>& levels, float amount)
{
    std::vector<float> corrections (levels.size(), 0.0f);
    amount = juce::jlimit (0.0f, 1.0f, amount);

    if (amount <= 0.0f)
        return corrections;

    // Velocity-Layer wie im Export: Rang der Velocity innerhalb der Note
    std::map<int, std::set<int>> velocitiesPerNote;

    for (const auto& level : levels)
        velocitiesPerNote[level.note].insert (level.velocity);

    std::vector<int> layers;

    for (const auto& level : levels)
    {
        const auto& velocities = velocitiesPerNote[level.note];
        layers.push_back ((int) std::distance (velocities.begin(), velocities.find (level.velocity)));
    }

    std::set<int> layerIndices (layers.begin(), layers.end());

    for (const auto layer : layerIndices)
    {
        // Mittlerer Pegel pro Note (Leistung über die Varianten)
        std::map<int, std::pair<double, int>> sums;

        for (size_t i = 0; i < levels.size(); ++i)
            if (layers[i] == layer && levels[i].levelDb > silentDb)
            {
                auto& [power, count] = sums[levels[i].note];
                power += std::pow (10.0, levels[i].levelDb / 10.0);
                ++count;
            }

        if (sums.size() < 3)
            continue;

        std::vector<int> notes;
        std::vector<double> measured;

        for (const auto& [note, sum] : sums)
        {
            notes.push_back (note);
            measured.push_back (10.0 * std::log10 (sum.first / sum.second));
        }

        const auto n = measured.size();
        std::map<int, double> correctionPerNote;

        for (size_t i = 0; i < n; ++i)
        {
            double smoothed;

            if (i == 0)
                smoothed = (5.0 * measured[0] + 2.0 * measured[1] - measured[2]) / 6.0;
            else if (i == n - 1)
                smoothed = (5.0 * measured[n - 1] + 2.0 * measured[n - 2] - measured[n - 3]) / 6.0;
            else
                smoothed = (measured[i - 1] + 2.0 * measured[i] + measured[i + 1]) / 4.0;

            correctionPerNote[notes[i]] = juce::jlimit ((double) -maxZoneSmoothingDb, (double) maxZoneSmoothingDb,
                                                        amount * (smoothed - measured[i]));
        }

        for (size_t i = 0; i < levels.size(); ++i)
            if (layers[i] == layer && levels[i].levelDb > silentDb)
                corrections[i] = (float) correctionPerNote[levels[i].note];
    }

    return corrections;
}

} // namespace autosampler
