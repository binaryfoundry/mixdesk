#include "WaveformAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace mixdesk::engine
{
namespace
{
constexpr auto chunkSamples = 65536;
// Waveforms are display-only, so cap per-point sampling to avoid spending load
// time scanning millions of samples that map to the same pixel-scale peak.
constexpr auto maxSamplesPerWaveformPoint = 256;

float samplePeakAt(const juce::AudioBuffer<float>& buffer, int sampleIndex) noexcept
{
    auto peak = 0.0f;

    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = std::max(peak, std::abs(buffer.getSample(channel, sampleIndex)));

    return peak;
}
} // namespace

WaveformAnalyzer::WaveformAnalyzer()
{
    formatManager.registerBasicFormats();
}

model::StemWaveform WaveformAnalyzer::analyzeStem(const juce::File& stemFile,
    model::StemType stemType,
    double pointsPerSecond)
{
    model::StemWaveform waveform;
    waveform.type = stemType;
    waveform.pointsPerSecond = std::max(1.0, pointsPerSecond);

    if (! stemFile.existsAsFile())
        return waveform;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(stemFile));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return waveform;

    waveform.durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    const auto pointCount = std::max<std::size_t>(1,
        static_cast<std::size_t>(std::ceil(waveform.durationSeconds * waveform.pointsPerSecond)));
    waveform.peaks.assign(pointCount, 0.0f);

    const auto channelsToRead = std::max(1, static_cast<int>(std::min<juce::uint32>(reader->numChannels, 2)));
    juce::AudioBuffer<float> buffer(channelsToRead, chunkSamples);

    juce::int64 readPosition = 0;
    while (readPosition < reader->lengthInSamples)
    {
        const auto samplesThisChunk = static_cast<int>(
            std::min<juce::int64>(chunkSamples, reader->lengthInSamples - readPosition));

        buffer.clear();
        if (! reader->read(&buffer, 0, samplesThisChunk, readPosition, true, channelsToRead > 1))
            break;

        for (auto sampleIndex = 0; sampleIndex < samplesThisChunk; ++sampleIndex)
        {
            const auto globalSample = readPosition + sampleIndex;
            const auto pointIndex = std::min<std::size_t>(
                waveform.peaks.size() - 1,
                static_cast<std::size_t>((globalSample * static_cast<juce::int64>(waveform.peaks.size())) / reader->lengthInSamples));

            waveform.peaks[pointIndex] = std::max(waveform.peaks[pointIndex], samplePeakAt(buffer, sampleIndex));
        }

        readPosition += samplesThisChunk;
    }

    const auto maximumPeak = *std::max_element(waveform.peaks.begin(), waveform.peaks.end());
    if (maximumPeak > 0.0f)
        for (auto& peak : waveform.peaks)
            peak /= maximumPeak;

    return waveform;
}

model::StemWaveform WaveformAnalyzer::analyzeBuffer(const juce::AudioBuffer<float>& audio,
    double sampleRate,
    model::StemType stemType,
    double pointsPerSecond)
{
    model::StemWaveform waveform;
    waveform.type = stemType;
    waveform.pointsPerSecond = std::max(1.0, pointsPerSecond);

    if (audio.getNumSamples() <= 0 || audio.getNumChannels() <= 0 || sampleRate <= 0.0)
        return waveform;

    waveform.durationSeconds = static_cast<double>(audio.getNumSamples()) / sampleRate;
    const auto pointCount = std::max<std::size_t>(1,
        static_cast<std::size_t>(std::ceil(waveform.durationSeconds * waveform.pointsPerSecond)));
    waveform.peaks.assign(pointCount, 0.0f);

    const auto sampleCount = audio.getNumSamples();

    for (std::size_t pointIndex = 0; pointIndex < waveform.peaks.size(); ++pointIndex)
    {
        const auto startSample = static_cast<int>((static_cast<juce::int64>(pointIndex) * sampleCount)
            / static_cast<juce::int64>(waveform.peaks.size()));
        const auto endSample = static_cast<int>((static_cast<juce::int64>(pointIndex + 1) * sampleCount)
            / static_cast<juce::int64>(waveform.peaks.size()));
        const auto pointSamples = std::max(1, endSample - startSample);
        const auto stride = std::max(1, pointSamples / maxSamplesPerWaveformPoint);
        auto peak = 0.0f;

        if (endSample <= startSample)
        {
            waveform.peaks[pointIndex] = samplePeakAt(audio, std::clamp(startSample, 0, sampleCount - 1));
            continue;
        }

        for (auto sampleIndex = startSample; sampleIndex < endSample; sampleIndex += stride)
            peak = std::max(peak, samplePeakAt(audio, sampleIndex));

        peak = std::max(peak, samplePeakAt(audio, endSample - 1));

        waveform.peaks[pointIndex] = peak;
    }

    // Display-only normalization: raw stem buffers used for residual math are never
    // normalized independently.
    const auto maximumPeak = *std::max_element(waveform.peaks.begin(), waveform.peaks.end());
    if (maximumPeak > 0.0f)
        for (auto& peak : waveform.peaks)
            peak /= maximumPeak;

    return waveform;
}
} // namespace mixdesk::engine
