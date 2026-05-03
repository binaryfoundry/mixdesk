#include "DeckPlaybackEngine.h"

namespace mixdesk::engine
{
DeckPlaybackEngine::DeckPlaybackEngine()
{
    formatManager.registerBasicFormats();
}

DeckPlaybackEngine::~DeckPlaybackEngine()
{
    const juce::ScopedLock scopedLock(lock);
    transportSource.setSource(nullptr);
}

bool DeckPlaybackEngine::loadFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return false;

    auto source = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    const auto sourceSampleRate = source->getAudioFormatReader()->sampleRate;

    {
        const juce::ScopedLock scopedLock(lock);
        // TODO(real audio engine): move deck loading to an engine command queue and publish
        // a prepared-source snapshot instead of swapping sources directly from the UI domain.
        transportSource.stop();
        transportSource.setSource(nullptr);
        readerSource = std::move(source);
        transportSource.setSource(readerSource.get(), 0, nullptr, sourceSampleRate);
        loadedFile = file;
    }

    return true;
}

void DeckPlaybackEngine::start()
{
    const juce::ScopedLock scopedLock(lock);
    transportSource.start();
}

void DeckPlaybackEngine::stop()
{
    const juce::ScopedLock scopedLock(lock);
    transportSource.stop();
}

void DeckPlaybackEngine::togglePlayback()
{
    if (isPlaying())
        stop();
    else
        start();
}

bool DeckPlaybackEngine::isPlaying() const
{
    const juce::ScopedLock scopedLock(lock);
    return transportSource.isPlaying();
}

double DeckPlaybackEngine::getCurrentPositionSeconds() const
{
    const juce::ScopedLock scopedLock(lock);
    return transportSource.getCurrentPosition();
}

double DeckPlaybackEngine::getLengthSeconds() const
{
    const juce::ScopedLock scopedLock(lock);
    return transportSource.getLengthInSeconds();
}

juce::File DeckPlaybackEngine::getLoadedFile() const
{
    const juce::ScopedLock scopedLock(lock);
    return loadedFile;
}

void DeckPlaybackEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    const juce::ScopedLock scopedLock(lock);
    transportSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void DeckPlaybackEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    const juce::ScopedLock scopedLock(lock);

    if (readerSource == nullptr)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    transportSource.getNextAudioBlock(bufferToFill);
}

void DeckPlaybackEngine::releaseResources()
{
    const juce::ScopedLock scopedLock(lock);
    transportSource.releaseResources();
}
} // namespace mixdesk::engine
