#include "DeckPlaybackEngine.h"

#include <algorithm>
#include <cmath>

namespace mixdesk::engine
{
namespace
{
double bufferDurationSeconds(const StemAudioBuffer& buffer)
{
    return stemDurationSeconds(buffer);
}

float sampleAt(const StemAudioBuffer& buffer, int channel, double timeSeconds)
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

double playbackRateFor(double deckSecondsPerBar, double globalSecondsPerBar)
{
    if (deckSecondsPerBar <= 0.0 || globalSecondsPerBar <= 0.0)
        return 1.0;

    return std::clamp(deckSecondsPerBar / globalSecondsPerBar, 0.25, 4.0);
}

double bpmToSecondsPerBar(double bpm, int beatsPerBar)
{
    if (bpm <= 0.0)
        return 0.0;

    return (60.0 / bpm) * static_cast<double>(std::max(1, beatsPerBar));
}

float mixedSampleAt(const PreparedStemSet& stems,
    const model::StemEnableState& stemEnabled,
    int channel,
    double timeSeconds)
{
    if (canUseFullMixForPlayback(stems, stemEnabled))
        return sampleAt(stems.fullMix, channel, timeSeconds);

    return (stemEnabled.drums ? sampleAt(stems.drums, channel, timeSeconds) : 0.0f)
        + (stemEnabled.bass ? sampleAt(stems.bass, channel, timeSeconds) : 0.0f)
        + (stemEnabled.music ? sampleAt(stems.musicResidual, channel, timeSeconds) : 0.0f)
        + (stemEnabled.vocals ? sampleAt(stems.vocals, channel, timeSeconds) : 0.0f);
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

bool DeckPlaybackEngine::loadFile(const juce::File& file)
{
    auto loadedAudio = readStemAudioFile(formatManager, file, model::StemType::FullMix);
    if (! loadedAudio.has_value())
        return false;

    {
        const juce::ScopedLock scopedLock(lock);
        // TODO(real audio engine): move deck loading to an engine command queue and publish
        // a prepared-source snapshot instead of swapping buffers directly from the UI domain.
        stems = {};
        stems.fullMix = std::move(*loadedAudio);
        loadedFile = file;
        currentPositionSeconds = 0.0;
        currentGridBarPosition = trackSecondsToGridBar(currentPositionSeconds, deckSecondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
        stretchInputPositionSeconds = currentPositionSeconds;
        stretchInputRemainderSamples = 0.0;
        lengthSeconds = bufferDurationSeconds(stems.fullMix);
        playing = false;
        loaded = lengthSeconds > 0.0;
        timeStretchNeedsReset = true;
    }

    return loaded;
}

bool DeckPlaybackEngine::loadPreparedStemSet(PreparedStemSet preparedStems)
{
    const auto preparedLengthSeconds = preparedStemSetDurationSeconds(preparedStems);
    if (preparedLengthSeconds <= 0.0)
        return false;

    {
        const juce::ScopedLock scopedLock(lock);
        // TODO(real audio engine): publish an immutable prepared-stem snapshot to the audio
        // thread instead of replacing these buffers under a lock. MusicResidual has already
        // been derived offline by StemPreparation before this point.
        stems = std::move(preparedStems);
        loadedFile = stems.fullMix.sourceFile.existsAsFile() ? stems.fullMix.sourceFile : stems.instrumentalOriginal.sourceFile;
        currentPositionSeconds = 0.0;
        currentGridBarPosition = trackSecondsToGridBar(currentPositionSeconds, deckSecondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
        stretchInputPositionSeconds = currentPositionSeconds;
        stretchInputRemainderSamples = 0.0;
        lengthSeconds = preparedLengthSeconds;
        playing = false;
        loaded = lengthSeconds > 0.0;
        timeStretchNeedsReset = true;
    }

    return loaded;
}

void DeckPlaybackEngine::start()
{
    const juce::ScopedLock scopedLock(lock);
    if (! loaded)
        return;

    currentPositionSeconds = gridBarToTrackSeconds(currentGridBarPosition, deckSecondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
    stretchInputPositionSeconds = currentPositionSeconds;
    stretchInputRemainderSamples = 0.0;
    timeStretchNeedsReset = true;

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

void DeckPlaybackEngine::configureGridPlayback(double newDeckSecondsPerBar, double newFirstBeatOffsetSeconds, int newLaunchOffsetBars)
{
    const juce::ScopedLock scopedLock(lock);
    deckSecondsPerBar = std::max(0.0, newDeckSecondsPerBar);
    if (globalSecondsPerBar <= 0.0)
        globalSecondsPerBar = deckSecondsPerBar;

    firstBeatOffsetSeconds = newFirstBeatOffsetSeconds;
    launchOffsetBars = newLaunchOffsetBars;
    currentPositionSeconds = gridBarToTrackSeconds(currentGridBarPosition, deckSecondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
    stretchInputPositionSeconds = currentPositionSeconds;
    stretchInputRemainderSamples = 0.0;
    timeStretchNeedsReset = true;
}

void DeckPlaybackEngine::setGlobalBpm(double bpm, int beatsPerBar)
{
    const juce::ScopedLock scopedLock(lock);
    const auto newGlobalSecondsPerBar = bpmToSecondsPerBar(bpm, beatsPerBar);
    if (newGlobalSecondsPerBar > 0.0)
        globalSecondsPerBar = newGlobalSecondsPerBar;
}

void DeckPlaybackEngine::setLaunchOffsetBars(int newLaunchOffsetBars)
{
    const juce::ScopedLock scopedLock(lock);
    launchOffsetBars = newLaunchOffsetBars;
    currentPositionSeconds = gridBarToTrackSeconds(currentGridBarPosition, deckSecondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
    stretchInputPositionSeconds = currentPositionSeconds;
    stretchInputRemainderSamples = 0.0;
    timeStretchNeedsReset = true;
}

void DeckPlaybackEngine::setStemEnabled(model::StemType stemType, bool enabled)
{
    const juce::ScopedLock scopedLock(lock);
    model::setStemEnabled(stemEnabled, stemType, enabled);
}

void DeckPlaybackEngine::setMasterVolume(float volume)
{
    const juce::ScopedLock scopedLock(lock);
    masterVolume = std::clamp(volume, 0.0f, 1.0f);
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

void DeckPlaybackEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    const juce::ScopedLock scopedLock(lock);
    outputSampleRate = sampleRate;
    if (outputSampleRate > 0.0)
    {
        timeStretch.presetDefault(2, static_cast<float>(outputSampleRate));
        ensureStretchBuffers(std::max(1, samplesPerBlockExpected * 2), std::max(1, samplesPerBlockExpected));
        timeStretchConfigured = true;
        timeStretchNeedsReset = true;
    }
}

void DeckPlaybackEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    const juce::ScopedLock scopedLock(lock);
    bufferToFill.clearActiveBufferRegion();

    if (! loaded || ! playing || outputSampleRate <= 0.0 || bufferToFill.buffer == nullptr || ! timeStretchConfigured)
        return;

    auto* outputBuffer = bufferToFill.buffer;
    const auto endSample = bufferToFill.startSample + bufferToFill.numSamples;
    const auto playbackRate = playbackRateFor(deckSecondsPerBar, globalSecondsPerBar);
    const auto exactInputSamples = (static_cast<double>(bufferToFill.numSamples) * playbackRate) + stretchInputRemainderSamples;
    const auto inputSamples = std::max(1, static_cast<int>(std::floor(exactInputSamples)));
    stretchInputRemainderSamples = exactInputSamples - static_cast<double>(inputSamples);

    currentPositionSeconds = gridBarToTrackSeconds(currentGridBarPosition, deckSecondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
    if (currentPositionSeconds >= lengthSeconds)
    {
        playing = false;
        return;
    }

    ensureStretchBuffers(inputSamples, bufferToFill.numSamples);

    if (timeStretchNeedsReset)
        resetTimeStretch(playbackRate);

    for (auto inputSample = 0; inputSample < inputSamples; ++inputSample)
    {
        const auto timeSeconds = stretchInputPositionSeconds + (static_cast<double>(inputSample) / outputSampleRate);
        stretchInputBuffer.setSample(0, inputSample, mixedSampleAt(stems, stemEnabled, 0, timeSeconds));
        stretchInputBuffer.setSample(1, inputSample, mixedSampleAt(stems, stemEnabled, 1, timeSeconds));
    }

    stretchInputPositionSeconds += static_cast<double>(inputSamples) / outputSampleRate;
    timeStretch.process(stretchInputChannels.data(), inputSamples, stretchOutputChannels.data(), bufferToFill.numSamples);

    for (auto sample = bufferToFill.startSample; sample < endSample; ++sample)
    {
        const auto outputSampleIndex = sample - bufferToFill.startSample;
        for (auto channel = 0; channel < outputBuffer->getNumChannels(); ++channel)
        {
            const auto sourceChannel = std::min(channel, 1);
            outputBuffer->setSample(channel, sample, stretchOutputBuffer.getSample(sourceChannel, outputSampleIndex) * masterVolume);
        }
    }

    if (globalSecondsPerBar > 0.0)
    {
        currentGridBarPosition += (static_cast<double>(bufferToFill.numSamples) / outputSampleRate) / globalSecondsPerBar;
        currentPositionSeconds = gridBarToTrackSeconds(currentGridBarPosition, deckSecondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
    }
}

void DeckPlaybackEngine::releaseResources()
{
    const juce::ScopedLock scopedLock(lock);
    outputSampleRate = 0.0;
}

void DeckPlaybackEngine::resetTimeStretch(double)
{
    timeStretch.reset();
    stretchInputPositionSeconds = gridBarToTrackSeconds(currentGridBarPosition, deckSecondsPerBar, firstBeatOffsetSeconds, launchOffsetBars);
    stretchInputRemainderSamples = 0.0;
    timeStretchNeedsReset = false;
}

void DeckPlaybackEngine::ensureStretchBuffers(int inputSamples, int outputSamples)
{
    stretchInputBuffer.setSize(2, std::max(1, inputSamples), false, false, true);
    stretchOutputBuffer.setSize(2, std::max(1, outputSamples), false, false, true);
    stretchInputChannels = { stretchInputBuffer.getWritePointer(0), stretchInputBuffer.getWritePointer(1) };
    stretchOutputChannels = { stretchOutputBuffer.getWritePointer(0), stretchOutputBuffer.getWritePointer(1) };
}
} // namespace mixdesk::engine
