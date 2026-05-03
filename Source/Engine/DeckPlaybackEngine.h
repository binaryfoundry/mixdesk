#pragma once

#include "Engine/StemPreparation.h"
#include "Model/PhraseModel.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <signalsmith-stretch.h>

#include <array>

namespace mixdesk::engine
{
class DeckPlaybackEngine final : public juce::AudioSource
{
public:
    DeckPlaybackEngine();
    ~DeckPlaybackEngine() override;

    [[nodiscard]] bool loadFile(const juce::File& file);
    [[nodiscard]] bool loadPreparedStemSet(PreparedStemSet preparedStems);
    void start();
    void stop();
    void togglePlayback();
    void configureGridPlayback(double deckSecondsPerBar, double firstBeatOffsetSeconds, int launchOffsetBars);
    void setGlobalBpm(double bpm, int beatsPerBar);
    void setLaunchOffsetBars(int launchOffsetBars);
    void setStemEnabled(model::StemType stemType, bool enabled);
    void setMasterVolume(float volume);

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] double getCurrentPositionSeconds() const;
    [[nodiscard]] double getCurrentGridBarPosition() const;
    [[nodiscard]] double getLengthSeconds() const;
    [[nodiscard]] juce::File getLoadedFile() const;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

private:
    void resetTimeStretch(double playbackRate);
    void ensureStretchBuffers(int inputSamples, int outputSamples);

    mutable juce::CriticalSection lock;
    juce::AudioFormatManager formatManager;
    signalsmith::stretch::SignalsmithStretch<float> timeStretch;
    PreparedStemSet stems;
    juce::File loadedFile;
    double outputSampleRate {};
    double currentPositionSeconds {};
    double currentGridBarPosition {};
    double stretchInputPositionSeconds {};
    double stretchInputRemainderSamples {};
    double deckSecondsPerBar {};
    double globalSecondsPerBar {};
    double firstBeatOffsetSeconds {};
    double lengthSeconds {};
    int launchOffsetBars {};
    model::StemEnableState stemEnabled;
    float masterVolume { 0.90f };
    bool timeStretchConfigured {};
    bool timeStretchNeedsReset { true };
    bool playing {};
    bool loaded {};
    juce::AudioBuffer<float> stretchInputBuffer;
    juce::AudioBuffer<float> stretchOutputBuffer;
    std::array<float*, 2> stretchInputChannels {};
    std::array<float*, 2> stretchOutputChannels {};
};
} // namespace mixdesk::engine
