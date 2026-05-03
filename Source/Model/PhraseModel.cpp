#include "PhraseModel.h"

#include <algorithm>

namespace mixdesk::model
{
std::string_view toString(DeckId deckId) noexcept
{
    switch (deckId)
    {
        case DeckId::A: return "Deck A";
        case DeckId::B: return "Deck B";
        case DeckId::C: return "Deck C";
    }

    return "Deck";
}

std::string_view toString(DeckRole role) noexcept
{
    switch (role)
    {
        case DeckRole::Lead: return "Lead";
        case DeckRole::Incoming: return "Incoming";
        case DeckRole::RhythmLayer: return "Rhythm Layer";
        case DeckRole::VocalLayer: return "Vocal Layer";
        case DeckRole::TextureFx: return "Texture FX";
        case DeckRole::Exit: return "Exit";
    }

    return "Role";
}

std::string_view toString(PhraseType type) noexcept
{
    switch (type)
    {
        case PhraseType::Intro: return "Intro";
        case PhraseType::Groove: return "Groove";
        case PhraseType::Breakdown: return "Breakdown";
        case PhraseType::Build: return "Build";
        case PhraseType::Drop: return "Drop";
        case PhraseType::Outro: return "Outro";
        case PhraseType::Loop: return "Loop";
        case PhraseType::FX: return "FX";
    }

    return "Phrase";
}

std::string_view toString(StemType type) noexcept
{
    switch (type)
    {
        case StemType::Drums: return "Drums";
        case StemType::Bass: return "Bass";
        case StemType::Vocal: return "Vocal";
    }

    return "Stem";
}

bool isStemEnabled(const StemEnableState& stemState, StemType stemType) noexcept
{
    switch (stemType)
    {
        case StemType::Drums: return stemState.drums;
        case StemType::Bass: return stemState.bass;
        case StemType::Vocal: return stemState.vocal;
    }

    return true;
}

void setStemEnabled(StemEnableState& stemState, StemType stemType, bool enabled) noexcept
{
    switch (stemType)
    {
        case StemType::Drums:
            stemState.drums = enabled;
            break;
        case StemType::Bass:
            stemState.bass = enabled;
            break;
        case StemType::Vocal:
            stemState.vocal = enabled;
            break;
    }
}

DeckRole nextRole(DeckRole role) noexcept
{
    switch (role)
    {
        case DeckRole::Lead: return DeckRole::Incoming;
        case DeckRole::Incoming: return DeckRole::RhythmLayer;
        case DeckRole::RhythmLayer: return DeckRole::VocalLayer;
        case DeckRole::VocalLayer: return DeckRole::TextureFx;
        case DeckRole::TextureFx: return DeckRole::Exit;
        case DeckRole::Exit: return DeckRole::Lead;
    }

    return DeckRole::Lead;
}

DeckTimeline* findDeck(WorkspaceState& state, DeckId deckId) noexcept
{
    const auto iter = std::find_if(state.decks.begin(), state.decks.end(),
        [deckId](const DeckTimeline& deck) { return deck.id == deckId; });

    return iter == state.decks.end() ? nullptr : &*iter;
}

const DeckTimeline* findDeck(const WorkspaceState& state, DeckId deckId) noexcept
{
    const auto iter = std::find_if(state.decks.begin(), state.decks.end(),
        [deckId](const DeckTimeline& deck) { return deck.id == deckId; });

    return iter == state.decks.end() ? nullptr : &*iter;
}

std::size_t deckIndex(DeckId deckId) noexcept
{
    switch (deckId)
    {
        case DeckId::A: return 0;
        case DeckId::B: return 1;
        case DeckId::C: return 2;
    }

    return 0;
}
} // namespace mixdesk::model
