#include "PhraseModel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mixdesk::model
{
namespace
{
constexpr std::array publicVolumeStemTypes { StemType::Drums, StemType::Bass, StemType::MusicResidual, StemType::Vocals };

bool loadedDeckParticipates(const DeckTimeline& deck) noexcept
{
    return deck.loadedTrack.has_value();
}

std::size_t loadedDeckCount(const WorkspaceState& state) noexcept
{
    return static_cast<std::size_t>(std::count_if(state.decks.begin(),
        state.decks.end(),
        [](const DeckTimeline& deck) { return loadedDeckParticipates(deck); }));
}

void setEqualStemVolumesAcrossLoadedDecks(WorkspaceState& state, StemType stemType, std::size_t count) noexcept
{
    const auto equalVolume = count > 0 ? 1.0f / static_cast<float>(count) : 0.0f;

    for (auto& deck : state.decks)
        if (loadedDeckParticipates(deck))
            setStemVolume(deck.stemEnabled, stemType, equalVolume);
}
} // namespace

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

float stemVolume(const StemEnableState& stemState, StemType stemType) noexcept
{
    switch (stemType)
    {
        case StemType::FullMix: return areAllPublicStemVolumesUnity(stemState) ? 1.0f : 0.0f;
        case StemType::Drums: return stemState.drumsVolume;
        case StemType::Bass: return stemState.bassVolume;
        case StemType::Vocals: return stemState.vocalsVolume;
        case StemType::InstrumentalOriginal: return 0.0f;
        case StemType::MusicResidual: return stemState.musicVolume;
    }

    return 1.0f;
}

void setStemVolume(StemEnableState& stemState, StemType stemType, float volume) noexcept
{
    const auto clampedVolume = std::clamp(volume, 0.0f, 1.0f);

    switch (stemType)
    {
        case StemType::FullMix:
            stemState.drumsVolume = clampedVolume;
            stemState.bassVolume = clampedVolume;
            stemState.musicVolume = clampedVolume;
            stemState.vocalsVolume = clampedVolume;
            break;
        case StemType::Drums:
            stemState.drumsVolume = clampedVolume;
            break;
        case StemType::Bass:
            stemState.bassVolume = clampedVolume;
            break;
        case StemType::Vocals:
            stemState.vocalsVolume = clampedVolume;
            break;
        case StemType::InstrumentalOriginal:
            break;
        case StemType::MusicResidual:
            stemState.musicVolume = clampedVolume;
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

bool areAllPublicStemVolumesUnity(const StemEnableState& stemState) noexcept
{
    constexpr auto tolerance = 0.0001f;
    return std::abs(stemState.drumsVolume - 1.0f) <= tolerance
        && std::abs(stemState.bassVolume - 1.0f) <= tolerance
        && std::abs(stemState.musicVolume - 1.0f) <= tolerance
        && std::abs(stemState.vocalsVolume - 1.0f) <= tolerance;
}

void normalizeStemVolumesAcrossLoadedDecks(WorkspaceState& state, StemType stemType) noexcept
{
    if (! isPublicPlayableStem(stemType))
        return;

    const auto count = loadedDeckCount(state);
    if (count == 0)
        return;

    if (count == 1)
    {
        for (auto& deck : state.decks)
            if (loadedDeckParticipates(deck))
                setStemVolume(deck.stemEnabled, stemType, 1.0f);
        return;
    }

    auto total = 0.0f;
    for (const auto& deck : state.decks)
        if (loadedDeckParticipates(deck))
            total += stemVolume(deck.stemEnabled, stemType);

    if (total <= 0.0001f)
    {
        setEqualStemVolumesAcrossLoadedDecks(state, stemType, count);
        return;
    }

    const auto scale = 1.0f / total;
    for (auto& deck : state.decks)
        if (loadedDeckParticipates(deck))
            setStemVolume(deck.stemEnabled, stemType, stemVolume(deck.stemEnabled, stemType) * scale);
}

void setStemVolumeAcrossLoadedDecks(WorkspaceState& state, DeckId deckId, StemType stemType, float volume) noexcept
{
    auto* changedDeck = findDeck(state, deckId);
    if (changedDeck == nullptr)
        return;

    if (! isPublicPlayableStem(stemType) || ! loadedDeckParticipates(*changedDeck))
    {
        setStemVolume(changedDeck->stemEnabled, stemType, volume);
        return;
    }

    const auto count = loadedDeckCount(state);
    if (count <= 1)
    {
        setStemVolume(changedDeck->stemEnabled, stemType, 1.0f);
        return;
    }

    const auto changedVolume = std::clamp(volume, 0.0f, 1.0f);
    setStemVolume(changedDeck->stemEnabled, stemType, changedVolume);

    auto otherTotal = 0.0f;
    auto otherCount = std::size_t {};
    for (const auto& deck : state.decks)
    {
        if (deck.id == deckId || ! loadedDeckParticipates(deck))
            continue;

        otherTotal += stemVolume(deck.stemEnabled, stemType);
        ++otherCount;
    }

    const auto targetOtherTotal = 1.0f - changedVolume;
    if (otherCount == 0)
        return;

    if (otherTotal <= 0.0001f)
    {
        const auto equalOtherVolume = targetOtherTotal / static_cast<float>(otherCount);
        for (auto& deck : state.decks)
            if (deck.id != deckId && loadedDeckParticipates(deck))
                setStemVolume(deck.stemEnabled, stemType, equalOtherVolume);
        return;
    }

    const auto otherScale = targetOtherTotal / otherTotal;
    for (auto& deck : state.decks)
        if (deck.id != deckId && loadedDeckParticipates(deck))
            setStemVolume(deck.stemEnabled, stemType, stemVolume(deck.stemEnabled, stemType) * otherScale);
}

void balanceStemVolumesForLoadedDeck(WorkspaceState& state, DeckId loadedDeckId) noexcept
{
    auto* loadedDeck = findDeck(state, loadedDeckId);
    if (loadedDeck == nullptr || ! loadedDeckParticipates(*loadedDeck))
        return;

    const auto count = loadedDeckCount(state);
    if (count == 0)
        return;

    if (count == 1)
    {
        for (const auto stemType : publicVolumeStemTypes)
            setStemVolume(loadedDeck->stemEnabled, stemType, 1.0f);
        return;
    }

    const auto newDeckShare = 1.0f / static_cast<float>(count);
    const auto targetOtherTotal = 1.0f - newDeckShare;

    for (const auto stemType : publicVolumeStemTypes)
    {
        auto otherTotal = 0.0f;
        auto otherCount = std::size_t {};

        for (const auto& deck : state.decks)
        {
            if (deck.id == loadedDeckId || ! loadedDeckParticipates(deck))
                continue;

            otherTotal += stemVolume(deck.stemEnabled, stemType);
            ++otherCount;
        }

        setStemVolume(loadedDeck->stemEnabled, stemType, newDeckShare);

        if (otherCount == 0)
            continue;

        if (otherTotal <= 0.0001f)
        {
            const auto equalOtherVolume = targetOtherTotal / static_cast<float>(otherCount);
            for (auto& deck : state.decks)
                if (deck.id != loadedDeckId && loadedDeckParticipates(deck))
                    setStemVolume(deck.stemEnabled, stemType, equalOtherVolume);
        }
        else
        {
            const auto otherScale = targetOtherTotal / otherTotal;
            for (auto& deck : state.decks)
                if (deck.id != loadedDeckId && loadedDeckParticipates(deck))
                    setStemVolume(deck.stemEnabled, stemType, stemVolume(deck.stemEnabled, stemType) * otherScale);
        }
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

double beatGridSecondsPerBar(const BeatGrid& beatGrid) noexcept
{
    if (beatGrid.secondsPerBeat <= 0.0)
        return 0.0;

    return beatGrid.secondsPerBeat * static_cast<double>(std::max(1, beatGrid.beatsPerBar));
}

double trackTimeToGridBar(const BeatGrid& beatGrid, int launchOffsetBars, double trackTimeSeconds) noexcept
{
    const auto secondsPerBar = beatGridSecondsPerBar(beatGrid);
    if (secondsPerBar <= 0.0)
        return static_cast<double>(launchOffsetBars);

    return static_cast<double>(launchOffsetBars)
        + ((trackTimeSeconds - beatGrid.firstBeatOffsetSeconds) / secondsPerBar);
}

double gridBarToTrackTime(const BeatGrid& beatGrid, int launchOffsetBars, double gridBar) noexcept
{
    const auto secondsPerBar = beatGridSecondsPerBar(beatGrid);
    if (secondsPerBar <= 0.0)
        return 0.0;

    return beatGrid.firstBeatOffsetSeconds
        + ((gridBar - static_cast<double>(launchOffsetBars)) * secondsPerBar);
}

double trackAudioStartBar(const BeatGrid& beatGrid, int launchOffsetBars) noexcept
{
    return trackTimeToGridBar(beatGrid, launchOffsetBars, 0.0);
}

double trackDurationBars(const BeatGrid& beatGrid) noexcept
{
    const auto secondsPerBar = beatGridSecondsPerBar(beatGrid);
    return secondsPerBar > 0.0 ? beatGrid.durationSeconds / secondsPerBar : 0.0;
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
