#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

namespace mixdesk::engine
{
class DeckPlaybackEngine final : public juce::AudioSource
{
public:
    DeckPlaybackEngine();
    ~DeckPlaybackEngine() override;

    [[nodiscard]] bool loadFile(const juce::File& file);
    void start();
    void stop();
    void togglePlayback();

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] double getCurrentPositionSeconds() const;
    [[nodiscard]] double getLengthSeconds() const;
    [[nodiscard]] juce::File getLoadedFile() const;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

private:
    mutable juce::CriticalSection lock;
    juce::AudioFormatManager formatManager;
    juce::AudioTransportSource transportSource;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::File loadedFile;
};
} // namespace mixdesk::engine
