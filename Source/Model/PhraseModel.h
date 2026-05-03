#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mixdesk::model
{
enum class DeckId
{
    A,
    B,
    C
};

enum class DeckRole
{
    Lead,
    Incoming,
    RhythmLayer,
    VocalLayer,
    TextureFx,
    Exit
};

enum class PhraseType
{
    Intro,
    Groove,
    Breakdown,
    Build,
    Drop,
    Outro,
    Loop,
    FX
};

enum class StemType
{
    FullMix,
    Drums,
    Bass,
    Vocals,
    InstrumentalOriginal,
    MusicResidual
};

enum class SourceStemType
{
    FullMix,
    BassDirect,
    DrumsDirect,
    NoBass,
    NoDrums,
    NoVocals
};

enum class PlayableStemType
{
    Drums,
    Bass,
    Vocals,
    Music,
    FullMix
};

enum class InternalStemType
{
    BassFromComplement,
    DrumsFromComplement,
    VocalsFromComplement,
    MusicResidual,
    ReconciledBass,
    ReconciledDrums,
    ReconciledVocals,
    ReconciledMusic
};

enum class StemDerivationMethod
{
    FromFullMixMinusDrumsBassVocals,
    FromInstrumentalMinusDrumsBass,
    Unavailable
};

struct StemEnableState
{
    bool drums { true };
    bool bass { true };
    bool music { true };
    bool vocals { true };
    float drumsVolume { 1.0f };
    float bassVolume { 1.0f };
    float musicVolume { 1.0f };
    float vocalsVolume { 1.0f };
};

struct StemSet
{
    std::optional<std::string> fullMixPath;
    std::optional<std::string> drumsPath;
    std::optional<std::string> bassPath;
    std::optional<std::string> vocalsPath;
    std::optional<std::string> instrumentalOriginalPath;
    std::optional<std::string> musicResidualPath;
    std::optional<std::string> noBassPath;
    std::optional<std::string> noDrumsPath;
    std::optional<std::string> noVocalsPath;
    StemDerivationMethod musicResidualDerivation { StemDerivationMethod::Unavailable };
    std::string musicResidualDerivationDetails;
    std::string reconciliationDebugReport;
};

struct PhraseBlock
{
    PhraseType type { PhraseType::Intro };
    int startBar { 0 };
    int lengthBars { 8 };
    float energy { 0.5f };
    bool hasBass { false };
    bool hasVocal { false };
    bool hasDrums { false };
    bool hasMelody { false };
};

struct StemWaveform
{
    StemType type { StemType::Drums };
    double durationSeconds { 0.0 };
    double pointsPerSecond { 40.0 };
    std::vector<float> peaks;
};

struct BeatGrid
{
    double tempo { 0.0 };
    int bpm { 0 };
    double firstBeatOffsetSeconds { 0.0 };
    double secondsPerBeat { 0.0 };
    int beatsPerBar { 4 };
    double durationSeconds { 0.0 };
    std::vector<double> beatTimesSeconds;
};

struct LoadedTrack
{
    std::string name;
    std::string audioPath;
    std::string fullMixPath;
    std::string instrumentalStemPath;
    std::string drumStemPath;
    std::string bassStemPath;
    std::string vocalStemPath;
    std::string musicResidualStemPath;
    std::string noBassStemPath;
    std::string noDrumStemPath;
    std::string noVocalsStemPath;
    StemSet stems;
    std::string key;
    double durationSeconds { 0.0 };
};

struct DeckTimeline
{
    DeckId id { DeckId::A };
    DeckRole role { DeckRole::Lead };
    std::vector<PhraseBlock> blocks;
    std::optional<LoadedTrack> loadedTrack;
    std::optional<BeatGrid> beatGrid;
    std::vector<StemWaveform> stemWaveforms;
    StemEnableState stemEnabled;
    int launchOffsetBars { 0 };
    float volume { 1.0f };
    bool lowCutEnabled { false };
    bool isPlaying { false };
};

struct WorkspaceState
{
    double bpm { 124.0 };
    int beatsPerBar { 4 };
    int barsPerPhrase { 8 };
    double currentBarPosition { 0.0 };
    float masterVolume { 0.90f };
    std::vector<DeckTimeline> decks;
};

std::string_view toString(DeckId deckId) noexcept;
std::string_view toString(DeckRole role) noexcept;
std::string_view toString(PhraseType type) noexcept;
std::string_view toString(StemType type) noexcept;
std::string_view toString(SourceStemType type) noexcept;
std::string_view toString(PlayableStemType type) noexcept;
std::string_view toString(InternalStemType type) noexcept;
std::string_view toString(StemDerivationMethod method) noexcept;

bool isStemEnabled(const StemEnableState& stemState, StemType stemType) noexcept;
void setStemEnabled(StemEnableState& stemState, StemType stemType, bool enabled) noexcept;
float stemVolume(const StemEnableState& stemState, StemType stemType) noexcept;
void setStemVolume(StemEnableState& stemState, StemType stemType, float volume) noexcept;
bool isPublicPlayableStem(StemType stemType) noexcept;
bool areAllPublicStemsEnabled(const StemEnableState& stemState) noexcept;
bool areAllPublicStemVolumesUnity(const StemEnableState& stemState) noexcept;
void normalizeStemVolumesAcrossLoadedDecks(WorkspaceState& state, StemType stemType) noexcept;
void setStemVolumeAcrossLoadedDecks(WorkspaceState& state, DeckId deckId, StemType stemType, float volume) noexcept;
void balanceStemVolumesForLoadedDeck(WorkspaceState& state, DeckId loadedDeckId) noexcept;
DeckRole nextRole(DeckRole role) noexcept;

DeckTimeline* findDeck(WorkspaceState& state, DeckId deckId) noexcept;
const DeckTimeline* findDeck(const WorkspaceState& state, DeckId deckId) noexcept;
std::size_t deckIndex(DeckId deckId) noexcept;
} // namespace mixdesk::model
