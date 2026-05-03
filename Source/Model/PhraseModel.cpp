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
        case StemType::FullMix: return "Full Mix";
        case StemType::Drums: return "Drums";
        case StemType::Bass: return "Bass";
        case StemType::Vocals: return "Vocals";
        case StemType::InstrumentalOriginal: return "Instrumental Original";
        case StemType::MusicResidual: return "Music";
    }

    return "Stem";
}

std::string_view toString(SourceStemType type) noexcept
{
    switch (type)
    {
        case SourceStemType::FullMix: return "Full Mix";
        case SourceStemType::BassDirect: return "Bass Direct";
        case SourceStemType::DrumsDirect: return "Drums Direct";
        case SourceStemType::NoBass: return "No Bass";
        case SourceStemType::NoDrums: return "No Drums";
        case SourceStemType::NoVocals: return "No Vocals";
    }

    return "Source Stem";
}

std::string_view toString(PlayableStemType type) noexcept
{
    switch (type)
    {
        case PlayableStemType::Drums: return "Drums";
        case PlayableStemType::Bass: return "Bass";
        case PlayableStemType::Vocals: return "Vocals";
        case PlayableStemType::Music: return "Music";
        case PlayableStemType::FullMix: return "Full Mix";
    }

    return "Playable Stem";
}

std::string_view toString(InternalStemType type) noexcept
{
    switch (type)
    {
        case InternalStemType::BassFromComplement: return "Bass From Complement";
        case InternalStemType::DrumsFromComplement: return "Drums From Complement";
        case InternalStemType::VocalsFromComplement: return "Vocals From Complement";
        case InternalStemType::MusicResidual: return "Music Residual";
        case InternalStemType::ReconciledBass: return "Reconciled Bass";
        case InternalStemType::ReconciledDrums: return "Reconciled Drums";
        case InternalStemType::ReconciledVocals: return "Reconciled Vocals";
        case InternalStemType::ReconciledMusic: return "Reconciled Music";
    }

    return "Internal Stem";
}

std::string_view toString(StemDerivationMethod method) noexcept
{
    switch (method)
    {
        case StemDerivationMethod::FromFullMixMinusDrumsBassVocals: return "FullMix - Drums - Bass - Vocals";
        case StemDerivationMethod::FromInstrumentalMinusDrumsBass: return "Instrumental - Drums - Bass";
        case StemDerivationMethod::Unavailable: return "Unavailable";
    }

    return "Unavailable";
}

bool isStemEnabled(const StemEnableState& stemState, StemType stemType) noexcept
{
    switch (stemType)
    {
        case StemType::FullMix: return areAllPublicStemsEnabled(stemState);
        case StemType::Drums: return stemState.drums;
        case StemType::Bass: return stemState.bass;
        case StemType::Vocals: return stemState.vocals;
        case StemType::InstrumentalOriginal: return false;
        case StemType::MusicResidual: return stemState.music;
    }

    return true;
}

void setStemEnabled(StemEnableState& stemState, StemType stemType, bool enabled) noexcept
{
    switch (stemType)
    {
        case StemType::FullMix:
            stemState.drums = enabled;
            stemState.bass = enabled;
            stemState.music = enabled;
            stemState.vocals = enabled;
            break;
        case StemType::Drums:
            stemState.drums = enabled;
            break;
        case StemType::Bass:
            stemState.bass = enabled;
            break;
        case StemType::Vocals:
            stemState.vocals = enabled;
            break;
        case StemType::InstrumentalOriginal:
            break;
        case StemType::MusicResidual:
            stemState.music = enabled;
            break;
    }
}

bool isPublicPlayableStem(StemType stemType) noexcept
{
    switch (stemType)
    {
        case StemType::Drums:
        case StemType::Bass:
        case StemType::Vocals:
        case StemType::MusicResidual:
            return true;
        case StemType::FullMix:
        case StemType::InstrumentalOriginal:
            return false;
    }

    return false;
}

bool areAllPublicStemsEnabled(const StemEnableState& stemState) noexcept
{
    return stemState.drums && stemState.bass && stemState.music && stemState.vocals;
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
