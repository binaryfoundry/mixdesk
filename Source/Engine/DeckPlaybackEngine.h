#pragma once

#include "Model/PhraseModel.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace mixdesk::engine
{
class DeckPlaybackEngine final : public juce::AudioSource
{
public:
    DeckPlaybackEngine();
    ~DeckPlaybackEngine() override;

    [[nodiscard]] bool loadFile(const juce::File& file);
    [[nodiscard]] bool loadStemSet(const juce::File& instrumentalFile,
        const juce::File& drumStemFile,
        const juce::File& bassStemFile,
        const juce::File& vocalStemFile);
    void start();
    void stop();
    void togglePlayback();
    void configureGridPlayback(double secondsPerBar, double firstBeatOffsetSeconds, int launchOffsetBars);
    void setLaunchOffsetBars(int launchOffsetBars);
    void setStemEnabled(model::StemType stemType, bool enabled);

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] double getCurrentPositionSeconds() const;
    [[nodiscard]] double getCurrentGridBarPosition() const;
    [[nodiscard]] double getLengthSeconds() const;
    [[nodiscard]] juce::File getLoadedFile() const;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    struct PlaybackBuffer
    {
        juce::AudioBuffer<float> audio;
        double sampleRate {};
    };

private:
    mutable juce::CriticalSection lock;
    juce::AudioFormatManager formatManager;
    PlaybackBuffer residualInstrumental;
    PlaybackBuffer drums;
    PlaybackBuffer bass;
    PlaybackBuffer vocal;
    juce::File loadedFile;
    double outputSampleRate {};
    double currentPositionSeconds {};
    double currentGridBarPosition {};
    double secondsPerBar {};
    double firstBeatOffsetSeconds {};
    double lengthSeconds {};
    int launchOffsetBars {};
    model::StemEnableState stemEnabled;
    bool playing {};
    bool loaded {};
};
} // namespace mixdesk::engine
