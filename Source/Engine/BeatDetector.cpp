#include "BeatDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace mixdesk::engine
{
namespace
{
constexpr auto lowPassFrequencyHz = 240.0;
constexpr auto defaultBeatsPerBar = 4;
constexpr auto minimumNumberOfPeaks = 30;

struct BiquadLowPass
{
    BiquadLowPass(double sampleRate, double frequencyHz, double q = 1.0)
    {
        const auto omega = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
        const auto sinOmega = std::sin(omega);
        const auto cosOmega = std::cos(omega);
        const auto alpha = sinOmega / (2.0 * q);

        const auto rawB0 = (1.0 - cosOmega) * 0.5;
        const auto rawB1 = 1.0 - cosOmega;
        const auto rawB2 = (1.0 - cosOmega) * 0.5;
        const auto rawA0 = 1.0 + alpha;
        const auto rawA1 = -2.0 * cosOmega;
        const auto rawA2 = 1.0 - alpha;

        b0 = rawB0 / rawA0;
        b1 = rawB1 / rawA0;
        b2 = rawB2 / rawA0;
        a1 = rawA1 / rawA0;
        a2 = rawA2 / rawA0;
    }

    float process(float input) noexcept
    {
        const auto output = (b0 * input) + (b1 * x1) + (b2 * x2) - (a1 * y1) - (a2 * y2);
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;

        return static_cast<float>(output);
    }

    double b0 {};
    double b1 {};
    double b2 {};
    double a1 {};
    double a2 {};
    double x1 {};
    double x2 {};
    double y1 {};
    double y2 {};
};

struct IntervalBucket
{
    int interval {};
    std::vector<int> peaks;
};

struct TempoBucket
{
    double tempo {};
    double score {};
    std::vector<int> peaks;
};

double getMaximumValue(const std::vector<float>& channelData) noexcept
{
    auto maximumValue = 0.0;

    for (const auto sample : channelData)
        if (sample > maximumValue)
            maximumValue = sample;

    return maximumValue;
}

std::vector<int> getPeaksAtThreshold(const std::vector<float>& channelData, double threshold, double sampleRate)
{
    const auto length = static_cast<int>(channelData.size());
    std::vector<int> peaks;
    auto lastValueWasAboveThreshold = false;
    const auto skipSamples = std::max(1, static_cast<int>(sampleRate / 4.0) - 1);

    for (auto i = 0; i < length; ++i)
    {
        if (channelData[static_cast<std::size_t>(i)] > threshold)
        {
            lastValueWasAboveThreshold = true;
        }
        else if (lastValueWasAboveThreshold)
        {
            lastValueWasAboveThreshold = false;
            peaks.push_back(i - 1);

            // Matches the WebAudio worker: skip 0.25 seconds to move past the current peak.
            i += skipSamples;
        }
    }

    if (lastValueWasAboveThreshold)
        peaks.push_back(length - 1);

    return peaks;
}

std::vector<IntervalBucket> countIntervalsBetweenNearbyPeaks(const std::vector<int>& peaks)
{
    std::vector<IntervalBucket> intervalBuckets;

    for (std::size_t peakIndex = 0; peakIndex < peaks.size(); ++peakIndex)
    {
        const auto peak = peaks[peakIndex];
        const auto length = std::min<std::size_t>(peaks.size() - peakIndex, 10);

        for (std::size_t nearbyIndex = 1; nearbyIndex < length; ++nearbyIndex)
        {
            const auto interval = peaks[peakIndex + nearbyIndex] - peak;
            auto foundInterval = false;

            for (auto& intervalBucket : intervalBuckets)
            {
                if (intervalBucket.interval == interval)
                {
                    intervalBucket.peaks.push_back(peak);
                    foundInterval = true;
                    break;
                }
            }

            if (! foundInterval)
                intervalBuckets.push_back({ interval, { peak } });
        }
    }

    return intervalBuckets;
}

std::vector<TempoBucket> groupNeighborsByTempo(const std::vector<IntervalBucket>& intervalBuckets,
    double sampleRate,
    TempoSettings tempoSettings)
{
    const auto maxTempo = std::max(0.0, tempoSettings.maxTempo);
    const auto minTempo = std::max(0.0, tempoSettings.minTempo);
    std::vector<TempoBucket> tempoBuckets;

    for (const auto& intervalBucket : intervalBuckets)
    {
        if (intervalBucket.interval <= 0)
            continue;

        auto theoreticalTempo = 60.0 / (static_cast<double>(intervalBucket.interval) / sampleRate);

        while (theoreticalTempo < minTempo)
            theoreticalTempo *= 2.0;

        while (theoreticalTempo > maxTempo && maxTempo > 0.0)
            theoreticalTempo /= 2.0;

        if (theoreticalTempo < minTempo)
            continue;

        auto foundTempo = false;
        auto score = static_cast<double>(intervalBucket.peaks.size());

        for (auto& tempoBucket : tempoBuckets)
        {
            if (tempoBucket.tempo == theoreticalTempo)
            {
                tempoBucket.score += static_cast<double>(intervalBucket.peaks.size());
                tempoBucket.peaks.insert(tempoBucket.peaks.end(), intervalBucket.peaks.begin(), intervalBucket.peaks.end());
                foundTempo = true;
            }

            if (tempoBucket.tempo > theoreticalTempo - 0.5 && tempoBucket.tempo < theoreticalTempo + 0.5)
            {
                const auto tempoDifference = std::abs(tempoBucket.tempo - theoreticalTempo) * 2.0;
                score += (1.0 - tempoDifference) * static_cast<double>(tempoBucket.peaks.size());
                tempoBucket.score += (1.0 - tempoDifference) * static_cast<double>(intervalBucket.peaks.size());
            }
        }

        if (! foundTempo)
            tempoBuckets.push_back({ theoreticalTempo, score, intervalBucket.peaks });
    }

    return tempoBuckets;
}

std::vector<TempoBucket> computeTempoBuckets(const std::vector<float>& channelData,
    double sampleRate,
    TempoSettings tempoSettings)
{
    const auto maximumValue = getMaximumValue(channelData);
    const auto minimumThreshold = maximumValue * 0.3;
    auto threshold = maximumValue - (maximumValue * 0.05);
    std::vector<int> peaks;

    if (maximumValue > 0.25)
    {
        while (static_cast<int>(peaks.size()) < minimumNumberOfPeaks && threshold >= minimumThreshold)
        {
            peaks = getPeaksAtThreshold(channelData, threshold, sampleRate);
            threshold -= maximumValue * 0.05;
        }
    }

    auto tempoBuckets = groupNeighborsByTempo(countIntervalsBetweenNearbyPeaks(peaks), sampleRate, tempoSettings);
    std::sort(tempoBuckets.begin(), tempoBuckets.end(),
        [](const auto& left, const auto& right) { return left.score > right.score; });

    return tempoBuckets;
}

std::vector<double> makeBeatTimes(double offsetSeconds, double secondsPerBeat, double durationSeconds)
{
    std::vector<double> beatTimes;

    if (secondsPerBeat <= 0.0 || durationSeconds <= 0.0)
        return beatTimes;

    beatTimes.reserve(static_cast<std::size_t>(durationSeconds / secondsPerBeat) + 2);

    for (auto beatTime = offsetSeconds; beatTime <= durationSeconds; beatTime += secondsPerBeat)
        if (beatTime >= 0.0)
            beatTimes.push_back(beatTime);

    return beatTimes;
}
} // namespace

BeatDetector::BeatDetector()
{
    formatManager.registerBasicFormats();
}

BeatDetectionResult BeatDetector::analyzeDrumStem(const juce::File& drumStemFile, TempoSettings tempoSettings)
{
    if (! drumStemFile.existsAsFile())
        return { false, {}, "Drum stem does not exist: " + drumStemFile.getFullPathName() };

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(drumStemFile));
    if (reader == nullptr)
        return { false, {}, "JUCE could not create an audio reader for: " + drumStemFile.getFullPathName() };

    if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return { false, {}, "Drum stem has no readable audio samples: " + drumStemFile.getFullPathName() };

    if (reader->lengthInSamples > static_cast<juce::int64>(std::numeric_limits<int>::max()))
        return { false, {}, "Drum stem is too long for this prototype analyzer." };

    const auto sampleCount = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer(1, sampleCount);

    if (! reader->read(&buffer, 0, sampleCount, 0, true, false))
        return { false, {}, "Failed to decode drum stem: " + drumStemFile.getFullPathName() };

    std::vector<float> channelData(static_cast<std::size_t>(sampleCount));
    BiquadLowPass lowPass(reader->sampleRate, lowPassFrequencyHz);
    const auto* samples = buffer.getReadPointer(0);

    for (auto i = 0; i < sampleCount; ++i)
        channelData[static_cast<std::size_t>(i)] = lowPass.process(samples[i]);

    auto tempoBuckets = computeTempoBuckets(channelData, reader->sampleRate, tempoSettings);
    if (tempoBuckets.empty())
        return { false, {}, "No detectable beats found in drum stem: " + drumStemFile.getFileName() };

    const auto& bestBucket = tempoBuckets.front();
    const auto bpm = std::max(1, static_cast<int>(std::round(bestBucket.tempo)));
    const auto secondsPerBeat = 60.0 / static_cast<double>(bpm);
    auto peaks = bestBucket.peaks;
    std::sort(peaks.begin(), peaks.end());

    auto offsetSeconds = peaks.empty() ? 0.0 : static_cast<double>(peaks.front()) / reader->sampleRate;
    while (offsetSeconds > secondsPerBeat)
        offsetSeconds -= secondsPerBeat;

    const auto durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;

    model::BeatGrid beatGrid;
    beatGrid.tempo = bestBucket.tempo;
    beatGrid.bpm = bpm;
    beatGrid.firstBeatOffsetSeconds = offsetSeconds;
    beatGrid.secondsPerBeat = secondsPerBeat;
    beatGrid.beatsPerBar = defaultBeatsPerBar;
    beatGrid.durationSeconds = durationSeconds;
    beatGrid.beatTimesSeconds = makeBeatTimes(offsetSeconds, secondsPerBeat, durationSeconds);

    return { true, std::move(beatGrid), {} };
}
} // namespace mixdesk::engine
