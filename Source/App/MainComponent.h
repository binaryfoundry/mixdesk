#pragma once

#include "App/AppSettings.h"
#include "Engine/DeckPlaybackEngine.h"
#include "Engine/TrackLoader.h"
#include "Engine/WorkspaceController.h"
#include "UI/PhraseWorkspace.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <optional>
#include <atomic>
#include <vector>

namespace mixdesk::app
{
struct TrackLoadResult;

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
    void showTrackSelectionDialog(model::DeckId deckId, int launchOffsetBars);
    void beginLoadTrack(model::DeckId deckId, int launchOffsetBars, const juce::File& metadataFile);
    void applyLoadedTrackResult(std::shared_ptr<TrackLoadResult> result);
    void togglePlayback();
    void configureDeckOneGridPlayback();
    void refreshWorkspaceSnapshot();

    AppSettings appSettings;
    engine::WorkspaceController workspaceController;
    engine::DeckPlaybackEngine deckPlaybackEngine;
    std::unique_ptr<ui::PhraseWorkspace> phraseWorkspace;
    std::optional<model::BeatGrid> deckOneBeatGrid;
    std::atomic<int> nextTrackLoadRequestId { 1 };
    int activeTrackLoadRequestId {};
    int deckOneLaunchOffsetBars {};
};
} // namespace mixdesk::app
