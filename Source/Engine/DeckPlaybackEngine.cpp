#include "DeckPlaybackEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace mixdesk::engine
{
namespace
{
struct LoadedAudio
{
    juce::AudioBuffer<float> audio;
    double sampleRate {};
};

double bufferDurationSeconds(const DeckPlaybackEngine::PlaybackBuffer& buffer)
{
    if (buffer.sampleRate <= 0.0 || buffer.audio.getNumSamples() <= 0)
        return 0.0;

    return static_cast<double>(buffer.audio.getNumSamples()) / buffer.sampleRate;
}

float sampleAt(const DeckPlaybackEngine::PlaybackBuffer& buffer, int channel, double timeSeconds)
{
    if (buffer.sampleRate <= 0.0 || buffer.audio.getNumChannels() <= 0 || buffer.audio.getNumSamples() <= 0 || timeSeconds < 0.0)
        return 0.0f;

    const auto position = timeSeconds * buffer.sampleRate;
    const auto sampleIndex = static_cast<int>(std::floor(position));
    if (sampleIndex < 0 || sampleIndex >= buffer.audio.getNumSamples())
        return 0.0f;

    const auto sourceChannel = std::clamp(channel, 0, buffer.audio.getNumChannels() - 1);
    const auto nextIndex = std::min(sampleIndex + 1, buffer.audio.getNumSamples() - 1);
    const auto fraction = static_cast<float>(position - static_cast<double>(sampleIndex));
    const auto start = buffer.audio.getSample(sourceChannel, sampleIndex);
    const auto end = buffer.audio.getSample(sourceChannel, nextIndex);

    return start + ((end - start) * fraction);
}

void subtractStem(DeckPlaybackEngine::PlaybackBuffer& residual, const DeckPlaybackEngine::PlaybackBuffer& stem)
{
    if (residual.audio.getNumSamples() <= 0 || stem.audio.getNumSamples() <= 0 || residual.sampleRate <= 0.0)
        return;

    for (auto channel = 0; channel < residual.audio.getNumChannels(); ++channel)
    {
        auto* residualSamples = residual.audio.getWritePointer(channel);
        for (auto sample = 0; sample < residual.audio.getNumSamples(); ++sample)
        {
            const auto timeSeconds = static_cast<double>(sample) / residual.sampleRate;
            residualSamples[sample] -= sampleAt(stem, channel, timeSeconds);
        }
    }
}

double maxDurationSeconds(const DeckPlaybackEngine::PlaybackBuffer& a,
    const DeckPlaybackEngine::PlaybackBuffer& b,
    const DeckPlaybackEngine::PlaybackBuffer& c,
    const DeckPlaybackEngine::PlaybackBuffer& d)
{
    return std::max({ bufferDurationSeconds(a), bufferDurationSeconds(b), bufferDurationSeconds(c), bufferDurationSeconds(d) });
}

double gridBarToTrackSeconds(double gridBarPosition, double secondsPerBar, double firstBeatOffsetSeconds, int launchOffsetBars)
{
    if (secondsPerBar <= 0.0)
        return 0.0;

    return firstBeatOffsetSeconds + ((gridBarPosition - static_cast<double>(launchOffsetBars)) * secondsPerBar);
}

double trackSecondsToGridBar(double trackSeconds, double secondsPerBar, double firstBeatOffsetSeconds, int launchOffsetBars)
{
    if (secondsPerBar <= 0.0)
        return static_cast<double>(launchOffsetBars);

    return static_cast<double>(launchOffsetBars) + ((trackSeconds - firstBeatOffsetSeconds) / secondsPerBar);
}
} // namespace

DeckPlaybackEngine::DeckPlaybackEngine()
{
    formatManager.registerBasicFormats();
}

DeckPlaybackEngine::~DeckPlaybackEngine()
{
    const juce::ScopedLock scopedLock(lock);
    playing = false;
}

std::optional<LoadedAudio> readAudioFile(juce::AudioFormatManager& formatManager, const juce::File& file)
{
    if (! file.existsAsFile())
        return std::nullopt;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return std::nullopt;

    LoadedAudio loadedAudio;
    loadedAudio.audio.setSize(static_cast<int>(std::min<juce::int64>(reader->numChannels, 2)),
        static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, std::numeric_limits<int>::max())),
        false,
        true,
        false);
    loadedAudio.sampleRate = reader->sampleRate;
    reader->read(&loadedAudio.audio, 0, loadedAudio.audio.getNumSamples(), 0, true, true);

    return loadedAudio;
}

bool DeckPlaybackEngine::loadFile(const juce::File& file)
{
    auto loadedAudio = readAudioFile(formatManager, file);
    if (! loadedAudio.has_value())
        return false;

    PlaybackBuffer fullMix;
    fullMix.audio = std::move(loadedAudio->audio);
    fullMix.sampleRate = loadedAudio->sampleRate;

    {
        const juce::ScopedLock scopedLock(lock);
        // TODO(real audio engine): move deck loading to an engine command queue and publish
        // a prepared-source snapshot instead of swapping buffers directly from the UI domain.
        residualInstrumental = std::move(fullMix);
        drums = {};
        bass = {};
        vocal = {};
        loadedFile = file;
        currentPositionSeconds = 0.0;
        currentGridBarPosition = trackSecondsToGridBar(currentPositionSeconds, secondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
        lengthSeconds = bufferDurationSeconds(residualInstrumental);
        playing = false;
        loaded = lengthSeconds > 0.0;
    }

    return loaded;
}

bool DeckPlaybackEngine::loadStemSet(const juce::File& instrumentalFile,
    const juce::File& drumStemFile,
    const juce::File& bassStemFile,
    const juce::File& vocalStemFile)
{
    auto instrumentalAudio = readAudioFile(formatManager, instrumentalFile);
    if (! instrumentalAudio.has_value())
        return false;

    auto drumAudio = readAudioFile(formatManager, drumStemFile);
    auto bassAudio = readAudioFile(formatManager, bassStemFile);
    auto vocalAudio = readAudioFile(formatManager, vocalStemFile);

    PlaybackBuffer residual;
    residual.audio = std::move(instrumentalAudio->audio);
    residual.sampleRate = instrumentalAudio->sampleRate;

    PlaybackBuffer drumBuffer;
    if (drumAudio.has_value())
    {
        drumBuffer.audio = std::move(drumAudio->audio);
        drumBuffer.sampleRate = drumAudio->sampleRate;
    }

    PlaybackBuffer bassBuffer;
    if (bassAudio.has_value())
    {
        bassBuffer.audio = std::move(bassAudio->audio);
        bassBuffer.sampleRate = bassAudio->sampleRate;
    }

    PlaybackBuffer vocalBuffer;
    if (vocalAudio.has_value())
    {
        vocalBuffer.audio = std::move(vocalAudio->audio);
        vocalBuffer.sampleRate = vocalAudio->sampleRate;
    }

    // The supplied instrumental stem contains the rhythm bed. Keep the playback layers
    // independent by deriving a residual "music bed" before the audio thread sees it.
    subtractStem(residual, drumBuffer);
    subtractStem(residual, bassBuffer);

    {
        const juce::ScopedLock scopedLock(lock);
        // TODO(real audio engine): publish an immutable prepared-stem snapshot to the audio
        // thread instead of replacing these buffers under a lock.
        residualInstrumental = std::move(residual);
        drums = std::move(drumBuffer);
        bass = std::move(bassBuffer);
        vocal = std::move(vocalBuffer);
        loadedFile = instrumentalFile;
        currentPositionSeconds = 0.0;
        currentGridBarPosition = trackSecondsToGridBar(currentPositionSeconds, secondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
        lengthSeconds = maxDurationSeconds(residualInstrumental, drums, bass, vocal);
        playing = false;
        loaded = lengthSeconds > 0.0;
    }

    return loaded;
}

void DeckPlaybackEngine::start()
{
    const juce::ScopedLock scopedLock(lock);
    if (! loaded)
        return;

    currentPositionSeconds = gridBarToTrackSeconds(currentGridBarPosition, secondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);

    playing = true;
}

void DeckPlaybackEngine::stop()
{
    const juce::ScopedLock scopedLock(lock);
    playing = false;
}

void DeckPlaybackEngine::togglePlayback()
{
    if (isPlaying())
        stop();
    else
        start();
}

void DeckPlaybackEngine::configureGridPlayback(double newSecondsPerBar, double newFirstBeatOffsetSeconds, int newLaunchOffsetBars)
{
    const juce::ScopedLock scopedLock(lock);
    secondsPerBar = std::max(0.0, newSecondsPerBar);
    firstBeatOffsetSeconds = newFirstBeatOffsetSeconds;
    launchOffsetBars = newLaunchOffsetBars;
    currentPositionSeconds = gridBarToTrackSeconds(currentGridBarPosition, secondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
}

void DeckPlaybackEngine::setLaunchOffsetBars(int newLaunchOffsetBars)
{
    const juce::ScopedLock scopedLock(lock);
    launchOffsetBars = newLaunchOffsetBars;
    currentPositionSeconds = gridBarToTrackSeconds(currentGridBarPosition, secondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
}

void DeckPlaybackEngine::setStemEnabled(model::StemType stemType, bool enabled)
{
    const juce::ScopedLock scopedLock(lock);
    model::setStemEnabled(stemEnabled, stemType, enabled);
}

bool DeckPlaybackEngine::isPlaying() const
{
    const juce::ScopedLock scopedLock(lock);
    return playing;
}

double DeckPlaybackEngine::getCurrentPositionSeconds() const
{
    const juce::ScopedLock scopedLock(lock);
    return currentPositionSeconds;
}

double DeckPlaybackEngine::getCurrentGridBarPosition() const
{
    const juce::ScopedLock scopedLock(lock);
    return currentGridBarPosition;
}

double DeckPlaybackEngine::getLengthSeconds() const
{
    const juce::ScopedLock scopedLock(lock);
    return lengthSeconds;
}

juce::File DeckPlaybackEngine::getLoadedFile() const
{
    const juce::ScopedLock scopedLock(lock);
    return loadedFile;
}

void DeckPlaybackEngine::prepareToPlay(int, double sampleRate)
{
    const juce::ScopedLock scopedLock(lock);
    outputSampleRate = sampleRate;
}

void DeckPlaybackEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    const juce::ScopedLock scopedLock(lock);
    bufferToFill.clearActiveBufferRegion();

    if (! loaded || ! playing || outputSampleRate <= 0.0 || bufferToFill.buffer == nullptr)
        return;

    auto* outputBuffer = bufferToFill.buffer;
    const auto endSample = bufferToFill.startSample + bufferToFill.numSamples;
    constexpr auto masterGain = 0.90f;

    for (auto sample = bufferToFill.startSample; sample < endSample; ++sample)
    {
        currentPositionSeconds = gridBarToTrackSeconds(currentGridBarPosition, secondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
        if (currentPositionSeconds >= lengthSeconds)
        {
            playing = false;
            break;
        }

        for (auto channel = 0; channel < outputBuffer->getNumChannels(); ++channel)
        {
            const auto mixedSample = sampleAt(residualInstrumental, channel, currentPositionSeconds)
                + (stemEnabled.drums ? sampleAt(drums, channel, currentPositionSeconds) : 0.0f)
                + (stemEnabled.bass ? sampleAt(bass, channel, currentPositionSeconds) : 0.0f)
                + (stemEnabled.vocal ? sampleAt(vocal, channel, currentPositionSeconds) : 0.0f);

            outputBuffer->setSample(channel, sample, mixedSample * masterGain);
        }

        if (secondsPerBar > 0.0)
            currentGridBarPosition += (1.0 / outputSampleRate) / secondsPerBar;
        else
            currentPositionSeconds += 1.0 / outputSampleRate;
    }
}

void DeckPlaybackEngine::releaseResources()
{
    const juce::ScopedLock scopedLock(lock);
    outputSampleRate = 0.0;
}
} // namespace mixdesk::engine
