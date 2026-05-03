#pragma once

#include "App/AppSettings.h"
#include "Engine/DeckPlaybackEngine.h"
#include "Engine/TrackLoader.h"
#include "Engine/WorkspaceController.h"
#include "UI/PhraseWorkspace.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
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
    void configureDeckGridPlayback(model::DeckId deckId);
    void setTransportGridBarPosition(double gridBarPosition);
    void updateTransportTempoFromSnapshot();
    void updateDeckPlayingState();
    void syncStemControlsToPlayback();
    [[nodiscard]] engine::DeckPlaybackEngine& playbackEngineFor(model::DeckId deckId) noexcept;
    [[nodiscard]] const engine::DeckPlaybackEngine& playbackEngineFor(model::DeckId deckId) const noexcept;
    void refreshWorkspaceSnapshot();

    AppSettings appSettings;
    engine::WorkspaceController workspaceController;
    std::array<engine::DeckPlaybackEngine, 3> deckPlaybackEngines;
    std::unique_ptr<ui::PhraseWorkspace> phraseWorkspace;
    std::array<std::optional<model::BeatGrid>, 3> deckBeatGrids;
    std::array<int, 3> deckLaunchOffsetBars {};
    std::atomic<int> nextTrackLoadRequestId { 1 };
    std::atomic<double> audioSampleRate {};
    std::atomic<double> transportSecondsPerBar {};
    std::atomic<double> transportGridBarPosition {};
    int activeTrackLoadRequestId {};
};
} // namespace mixdesk::app
