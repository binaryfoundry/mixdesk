#pragma once

#include "Engine/BeatDetector.h"
#include "Engine/DeckPlaybackEngine.h"
#include "Engine/PhraseAnalyzer.h"
#include "Engine/TrackLoader.h"
#include "Engine/WaveformAnalyzer.h"
#include "Engine/WorkspaceController.h"
#include "UI/PhraseWorkspace.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <optional>

namespace mixdesk::app
{
class MainComponent final : public juce::AudioAppComponent,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

private:
    void timerCallback() override;
    void loadDeckOneFromTracksFolder();
    void togglePlayback();
    void configureDeckOneGridPlayback();
    void refreshWorkspaceSnapshot();

    engine::WorkspaceController workspaceController;
    engine::BeatDetector beatDetector;
    engine::PhraseAnalyzer phraseAnalyzer;
    engine::WaveformAnalyzer waveformAnalyzer;
    engine::DeckPlaybackEngine deckPlaybackEngine;
    std::unique_ptr<ui::PhraseWorkspace> phraseWorkspace;
    std::optional<model::BeatGrid> deckOneBeatGrid;
    int deckOneLaunchOffsetBars {};
};
} // namespace mixdesk::app
