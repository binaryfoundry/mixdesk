#pragma once

#include "Model/PhraseModel.h"

#include <variant>

namespace mixdesk::engine
{
struct MovePhraseBlockCommand
{
    model::DeckId deckId {};
    std::size_t blockIndex {};
    int newStartBar {};
};

struct NudgeLaunchOffsetCommand
{
    model::DeckId deckId {};
    int deltaBars {};
};

struct SetDeckLaunchOffsetCommand
{
    model::DeckId deckId {};
    int launchOffsetBars {};
};

struct SetDeckRoleCommand
{
    model::DeckId deckId {};
    model::DeckRole role {};
};

struct SetDeckLoadedTrackCommand
{
    model::DeckId deckId {};
    model::LoadedTrack track;
    model::BeatGrid beatGrid;
    std::vector<model::StemWaveform> stemWaveforms;
    std::vector<model::PhraseBlock> phraseBlocks;
};

struct SetDeckPlayingCommand
{
    model::DeckId deckId {};
    bool isPlaying {};
};

struct SetDeckStemEnabledCommand
{
    model::DeckId deckId {};
    model::StemType stemType { model::StemType::Drums };
    bool enabled { true };
};

struct SetCurrentBarPositionCommand
{
    double barPosition {};
};

using WorkspaceCommand = std::variant<
    MovePhraseBlockCommand,
    NudgeLaunchOffsetCommand,
    SetDeckLaunchOffsetCommand,
    SetDeckRoleCommand,
    SetDeckLoadedTrackCommand,
    SetDeckPlayingCommand,
    SetDeckStemEnabledCommand,
    SetCurrentBarPositionCommand>;

class WorkspaceController
{
public:
    explicit WorkspaceController(model::WorkspaceState initialState);

    [[nodiscard]] model::WorkspaceState createSnapshot() const;
    void dispatch(const WorkspaceCommand& command);

private:
    void apply(const MovePhraseBlockCommand& command);
    void apply(const NudgeLaunchOffsetCommand& command);
    void apply(const SetDeckLaunchOffsetCommand& command);
    void apply(const SetDeckRoleCommand& command);
    void apply(const SetDeckLoadedTrackCommand& command);
    void apply(const SetDeckPlayingCommand& command);
    void apply(const SetDeckStemEnabledCommand& command);
    void apply(const SetCurrentBarPositionCommand& command);

    model::WorkspaceState state;
};
} // namespace mixdesk::engine
