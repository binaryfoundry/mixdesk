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

struct SetDeckStemVolumeCommand
{
    model::DeckId deckId {};
    model::StemType stemType { model::StemType::Drums };
    float volume { 1.0f };
};

struct SetCurrentBarPositionCommand
{
    double barPosition {};
};

struct SetMasterVolumeCommand
{
    float volume { 1.0f };
};

struct SetBpmCommand
{
    double bpm { 124.0 };
};

using WorkspaceCommand = std::variant<
    MovePhraseBlockCommand,
    NudgeLaunchOffsetCommand,
    SetDeckLaunchOffsetCommand,
    SetDeckRoleCommand,
    SetDeckLoadedTrackCommand,
    SetDeckPlayingCommand,
    SetDeckStemEnabledCommand,
    SetDeckStemVolumeCommand,
    SetCurrentBarPositionCommand,
    SetMasterVolumeCommand,
    SetBpmCommand>;

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
    void apply(const SetDeckStemVolumeCommand& command);
    void apply(const SetCurrentBarPositionCommand& command);
    void apply(const SetMasterVolumeCommand& command);
    void apply(const SetBpmCommand& command);

    model::WorkspaceState state;
};
} // namespace mixdesk::engine
