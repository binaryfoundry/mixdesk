#include "WorkspaceController.h"

#include <algorithm>

namespace mixdesk::engine
{
WorkspaceController::WorkspaceController(model::WorkspaceState initialState)
    : state(std::move(initialState))
{
}

model::WorkspaceState WorkspaceController::createSnapshot() const
{
    // TODO(real audio engine): replace this UI-thread copy with an immutable state snapshot
    // published from the engine/control domain. The audio thread should never be blocked by UI paint.
    return state;
}

void WorkspaceController::dispatch(const WorkspaceCommand& command)
{
    // TODO(real audio engine): enqueue commands here for the engine/control thread instead of mutating state directly.
    std::visit([this](const auto& concreteCommand) { apply(concreteCommand); }, command);
}

void WorkspaceController::apply(const MovePhraseBlockCommand& command)
{
    auto* deck = model::findDeck(state, command.deckId);
    if (deck == nullptr || command.blockIndex >= deck->blocks.size())
        return;

    deck->blocks[command.blockIndex].startBar = command.newStartBar;
}

void WorkspaceController::apply(const NudgeLaunchOffsetCommand& command)
{
    auto* deck = model::findDeck(state, command.deckId);
    if (deck == nullptr)
        return;

    deck->launchOffsetBars += command.deltaBars;
}

void WorkspaceController::apply(const SetDeckLaunchOffsetCommand& command)
{
    auto* deck = model::findDeck(state, command.deckId);
    if (deck == nullptr)
        return;

    deck->launchOffsetBars = command.launchOffsetBars;
}

void WorkspaceController::apply(const SetDeckRoleCommand& command)
{
    auto* deck = model::findDeck(state, command.deckId);
    if (deck == nullptr)
        return;

    deck->role = command.role;
}

void WorkspaceController::apply(const SetDeckLoadedTrackCommand& command)
{
    auto* deck = model::findDeck(state, command.deckId);
    if (deck == nullptr)
        return;

    const auto shouldAdoptTrackTempo = std::none_of(state.decks.begin(),
        state.decks.end(),
        [](const auto& existingDeck) { return existingDeck.loadedTrack.has_value(); });

    deck->loadedTrack = command.track;
    deck->beatGrid = command.beatGrid;
    deck->stemWaveforms = command.stemWaveforms;
    deck->blocks = command.phraseBlocks;

    if (shouldAdoptTrackTempo)
    {
        if (command.beatGrid.tempo > 0.0)
            state.bpm = command.beatGrid.tempo;
        else if (command.beatGrid.bpm > 0)
            state.bpm = static_cast<double>(command.beatGrid.bpm);

        state.beatsPerBar = std::max(1, command.beatGrid.beatsPerBar);
    }
}

void WorkspaceController::apply(const SetDeckPlayingCommand& command)
{
    auto* deck = model::findDeck(state, command.deckId);
    if (deck == nullptr)
        return;

    deck->isPlaying = command.isPlaying;
}

void WorkspaceController::apply(const SetDeckStemEnabledCommand& command)
{
    auto* deck = model::findDeck(state, command.deckId);
    if (deck == nullptr)
        return;

    if (command.stemType == model::StemType::Vocals && command.enabled)
    {
        model::assignStemToLoadedDeck(state, command.deckId, command.stemType);
        return;
    }

    model::setStemEnabled(deck->stemEnabled, command.stemType, command.enabled);
}

void WorkspaceController::apply(const SetDeckStemVolumeCommand& command)
{
    auto* deck = model::findDeck(state, command.deckId);
    if (deck == nullptr)
        return;

    model::setStemVolumeAcrossLoadedDecks(state, command.deckId, command.stemType, command.volume);
}

void WorkspaceController::apply(const SetCurrentBarPositionCommand& command)
{
    state.currentBarPosition = command.barPosition;
}

void WorkspaceController::apply(const SetMasterVolumeCommand& command)
{
    state.masterVolume = std::clamp(command.volume, 0.0f, 1.0f);
}

void WorkspaceController::apply(const SetBpmCommand& command)
{
    state.bpm = std::clamp(command.bpm, 40.0, 240.0);
}
} // namespace mixdesk::engine
