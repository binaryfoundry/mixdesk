#include "PhraseAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mixdesk::engine
{
namespace
{
struct BarEnergy
{
    float drums {};
    float bass {};
    float vocal {};
    float total {};
};

struct WindowEnergy
{
    int startBar {};
    int lengthBars {};
    float drums {};
    float bass {};
    float vocal {};
    float total {};
    float previousTotal {};
    float nextTotal {};
    float novelty {};
};

const model::StemWaveform* findStem(const std::vector<model::StemWaveform>& waveforms, model::StemType type)
{
    const auto iter = std::find_if(waveforms.begin(), waveforms.end(),
        [type](const model::StemWaveform& waveform) { return waveform.type == type; });

    return iter == waveforms.end() ? nullptr : &*iter;
}

float averageStemPeak(const model::StemWaveform* waveform, double startSeconds, double endSeconds)
{
    if (waveform == nullptr || waveform->peaks.empty() || waveform->pointsPerSecond <= 0.0 || endSeconds <= startSeconds)
        return 0.0f;

    const auto startIndex = std::min<std::size_t>(
        waveform->peaks.size() - 1,
        static_cast<std::size_t>(std::max(0.0, std::floor(startSeconds * waveform->pointsPerSecond))));
    const auto endIndex = std::min<std::size_t>(
        waveform->peaks.size(),
        static_cast<std::size_t>(std::max(0.0, std::ceil(endSeconds * waveform->pointsPerSecond))));

    if (endIndex <= startIndex)
        return waveform->peaks[startIndex];

    auto sumSquares = 0.0;
    for (auto index = startIndex; index < endIndex; ++index)
    {
        const auto peak = static_cast<double>(std::clamp(waveform->peaks[index], 0.0f, 1.0f));
        sumSquares += peak * peak;
    }

    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(endIndex - startIndex)));
}

void normalizeStemValues(std::vector<BarEnergy>& bars, float BarEnergy::*member)
{
    auto maximum = 0.0f;
    for (const auto& bar : bars)
        maximum = std::max(maximum, bar.*member);

    if (maximum <= 0.0f)
        return;

    for (auto& bar : bars)
        bar.*member = std::clamp((bar.*member) / maximum, 0.0f, 1.0f);
}

float averageMember(const std::vector<BarEnergy>& bars, int startBar, int lengthBars, float BarEnergy::*member)
{
    if (bars.empty() || lengthBars <= 0)
        return 0.0f;

    const auto start = std::clamp(startBar, 0, static_cast<int>(bars.size()));
    const auto end = std::clamp(startBar + lengthBars, 0, static_cast<int>(bars.size()));

    if (end <= start)
        return 0.0f;

    auto sum = 0.0f;
    for (auto index = start; index < end; ++index)
        sum += bars[static_cast<std::size_t>(index)].*member;

    return sum / static_cast<float>(end - start);
}

float meanOf(const std::vector<WindowEnergy>& windows, float WindowEnergy::*member)
{
    if (windows.empty())
        return 0.0f;

    auto sum = 0.0f;
    for (const auto& window : windows)
        sum += window.*member;

    return sum / static_cast<float>(windows.size());
}

bool isCoreEnergy(const WindowEnergy& window, float meanTotal)
{
    return window.drums > 0.48f
        && window.bass > 0.44f
        && window.total > std::max(0.50f, meanTotal + 0.04f);
}

bool hasLowToHighContext(const std::vector<WindowEnergy>& windows, std::size_t index, float meanTotal)
{
    if (index == 0)
        return false;

    const auto& window = windows[index];
    const auto& previous = windows[index - 1];
    const auto& previous2 = windows[index >= 2 ? index - 2 : index - 1];
    const auto previousWasSparse = previous.total < meanTotal - 0.08f || previous.drums < 0.40f || previous.bass < 0.38f;
    const auto bassReturn = window.bass > 0.46f && (window.bass > previous.bass + 0.14f || previous.bass < 0.36f);
    const auto drumReturn = window.drums > 0.48f && (window.drums > previous.drums + 0.12f || previous.drums < 0.38f);
    const auto risingIntoHere = previous.total > previous2.total + 0.04f && window.total > previous.total + 0.02f;

    return (previousWasSparse && (bassReturn || drumReturn || window.novelty > 0.12f))
        || ((bassReturn || drumReturn) && window.novelty > 0.10f)
        || risingIntoHere;
}

bool isDropCandidate(const std::vector<WindowEnergy>& windows, std::size_t index, float meanTotal, std::size_t introWindowCount)
{
    if (index < introWindowCount || index >= windows.size())
        return false;

    const auto& window = windows[index];
    const auto& previous = windows[index - 1];
    const auto stemReturn = (window.bass > 0.46f && (window.bass > previous.bass + 0.12f || previous.bass < 0.36f))
        || (window.drums > 0.48f && (window.drums > previous.drums + 0.10f || previous.drums < 0.38f));

    return isCoreEnergy(window, meanTotal)
        && stemReturn
        && hasLowToHighContext(windows, index, meanTotal);
}

std::vector<model::PhraseType> classifyWindows(const std::vector<WindowEnergy>& windows)
{
    std::vector<model::PhraseType> labels(windows.size(), model::PhraseType::Groove);

    if (windows.empty())
        return labels;

    const auto meanTotal = meanOf(windows, &WindowEnergy::total);
    const auto introWindowCount = std::min<std::size_t>(windows.size(), windows.size() >= 6 ? 4 : 1);
    const auto outroWindowCount = std::min<std::size_t>(windows.size(), windows.size() >= 8 ? 2 : 1);

    std::vector<bool> dropCandidate(windows.size(), false);
    auto previousDropIndex = windows.size();

    for (std::size_t index = 0; index < windows.size(); ++index)
    {
        const auto farEnoughFromPreviousDrop = previousDropIndex == windows.size() || index > previousDropIndex + 1;
        dropCandidate[index] = farEnoughFromPreviousDrop && isDropCandidate(windows, index, meanTotal, introWindowCount);

        if (dropCandidate[index])
            previousDropIndex = index;
    }

    for (std::size_t index = 0; index < windows.size(); ++index)
    {
        const auto& window = windows[index];
        const auto isIntro = index < introWindowCount
            || (index < introWindowCount + 2 && window.vocal < 0.28f && ! dropCandidate[index] && window.novelty < 0.18f);
        const auto isOutro = index + outroWindowCount >= windows.size() && window.vocal < 0.38f;
        const auto nextIsDrop = index + 1 < windows.size() && dropCandidate[index + 1];
        const auto next2IsDrop = index + 2 < windows.size() && dropCandidate[index + 2];
        const auto isRising = window.nextTotal > window.total + 0.08f;
        const auto fellFromPrevious = window.previousTotal > window.total + 0.12f;

        if (isIntro)
        {
            labels[index] = model::PhraseType::Intro;
        }
        else if (isOutro)
        {
            labels[index] = model::PhraseType::Outro;
        }
        else if (dropCandidate[index])
        {
            labels[index] = model::PhraseType::Drop;
        }
        else if (nextIsDrop)
        {
            labels[index] = model::PhraseType::Build;
        }
        else if (next2IsDrop && (window.drums < 0.46f || window.bass < 0.38f || window.vocal > 0.34f))
        {
            labels[index] = model::PhraseType::Breakdown;
        }
        else if ((window.drums < 0.34f || window.bass < 0.30f) && (window.vocal > 0.30f || fellFromPrevious))
        {
            labels[index] = model::PhraseType::Breakdown;
        }
        else if (isRising && window.drums > 0.34f)
        {
            labels[index] = model::PhraseType::Build;
        }
        else if (window.total < std::max(0.20f, meanTotal - 0.22f))
        {
            labels[index] = model::PhraseType::FX;
        }
        else if (window.drums > 0.52f && window.bass < 0.34f && window.vocal < 0.34f)
        {
            labels[index] = model::PhraseType::Loop;
        }
        else
        {
            labels[index] = model::PhraseType::Groove;
        }
    }

    return labels;
}

float combinedEnergy(const WindowEnergy& window)
{
    return std::clamp((window.drums * 0.38f) + (window.bass * 0.38f) + (window.vocal * 0.24f), 0.0f, 1.0f);
}
} // namespace

std::vector<model::PhraseBlock> PhraseAnalyzer::analyze(const model::BeatGrid& beatGrid,
    const std::vector<model::StemWaveform>& stemWaveforms,
    int barsPerPhrase) const
{
    const auto secondsPerBar = beatGrid.secondsPerBeat * static_cast<double>(std::max(1, beatGrid.beatsPerBar));
    if (secondsPerBar <= 0.0 || beatGrid.durationSeconds <= 0.0)
        return {};

    const auto drumStem = findStem(stemWaveforms, model::StemType::Drums);
    const auto bassStem = findStem(stemWaveforms, model::StemType::Bass);
    const auto vocalStem = findStem(stemWaveforms, model::StemType::Vocal);
    const auto barCount = std::max(1, static_cast<int>(std::floor((beatGrid.durationSeconds - beatGrid.firstBeatOffsetSeconds) / secondsPerBar)));

    std::vector<BarEnergy> bars(static_cast<std::size_t>(barCount));
    for (auto barIndex = 0; barIndex < barCount; ++barIndex)
    {
        const auto startSeconds = beatGrid.firstBeatOffsetSeconds + (static_cast<double>(barIndex) * secondsPerBar);
        const auto endSeconds = std::min(beatGrid.durationSeconds, startSeconds + secondsPerBar);
        auto& bar = bars[static_cast<std::size_t>(barIndex)];
        bar.drums = averageStemPeak(drumStem, startSeconds, endSeconds);
        bar.bass = averageStemPeak(bassStem, startSeconds, endSeconds);
        bar.vocal = averageStemPeak(vocalStem, startSeconds, endSeconds);
    }

    normalizeStemValues(bars, &BarEnergy::drums);
    normalizeStemValues(bars, &BarEnergy::bass);
    normalizeStemValues(bars, &BarEnergy::vocal);

    for (auto& bar : bars)
        bar.total = (bar.drums * 0.38f) + (bar.bass * 0.38f) + (bar.vocal * 0.24f);

    barsPerPhrase = std::max(4, barsPerPhrase);
    std::vector<WindowEnergy> windows;

    for (auto startBar = 0; startBar < barCount; startBar += barsPerPhrase)
    {
        const auto lengthBars = std::min(barsPerPhrase, barCount - startBar);
        if (lengthBars < 4)
            break;

        WindowEnergy window;
        window.startBar = startBar;
        window.lengthBars = lengthBars;
        window.drums = averageMember(bars, startBar, lengthBars, &BarEnergy::drums);
        window.bass = averageMember(bars, startBar, lengthBars, &BarEnergy::bass);
        window.vocal = averageMember(bars, startBar, lengthBars, &BarEnergy::vocal);
        window.total = averageMember(bars, startBar, lengthBars, &BarEnergy::total);
        window.previousTotal = averageMember(bars, startBar - barsPerPhrase, barsPerPhrase, &BarEnergy::total);
        window.nextTotal = averageMember(bars, startBar + barsPerPhrase, barsPerPhrase, &BarEnergy::total);

        if (! windows.empty())
        {
            const auto& previous = windows.back();
            window.novelty = (std::abs(window.drums - previous.drums) * 0.32f)
                + (std::abs(window.bass - previous.bass) * 0.38f)
                + (std::abs(window.vocal - previous.vocal) * 0.30f);
        }

        windows.push_back(window);
    }

    std::vector<model::PhraseBlock> blocks;
    blocks.reserve(windows.size());
    const auto labels = classifyWindows(windows);

    for (std::size_t index = 0; index < windows.size(); ++index)
    {
        const auto& window = windows[index];
        model::PhraseBlock block;
        block.type = labels[index];
        block.startBar = window.startBar;
        block.lengthBars = window.lengthBars;
        block.energy = combinedEnergy(window);
        block.hasDrums = window.drums > 0.32f;
        block.hasBass = window.bass > 0.36f;
        block.hasVocal = window.vocal > 0.28f;
        block.hasMelody = block.type == model::PhraseType::Build
            || block.type == model::PhraseType::Breakdown
            || block.type == model::PhraseType::Drop;

        blocks.push_back(block);
    }

    return blocks;
}
} // namespace mixdesk::engine
