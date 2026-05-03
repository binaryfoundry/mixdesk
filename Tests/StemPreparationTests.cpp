#include "Engine/StemPreparation.h"
#include "Engine/WorkspaceController.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr auto testSampleRate = 48000.0;
constexpr auto channelCount = 2;
constexpr auto sampleCount = 256;
constexpr auto tolerance = 0.00001f;

using mixdesk::engine::PreparedStemSet;
using mixdesk::engine::StemAudioBuffer;
using mixdesk::model::StemType;

StemAudioBuffer makeStem(StemType type, float scale, float phase)
{
    StemAudioBuffer stem;
    stem.type = type;
    stem.sampleRate = testSampleRate;
    stem.audio.setSize(channelCount, sampleCount);

    for (auto channel = 0; channel < channelCount; ++channel)
    {
        for (auto sample = 0; sample < sampleCount; ++sample)
        {
            const auto t = static_cast<float>(sample + 1);
            const auto c = static_cast<float>(channel) * 0.17f;
            const auto value = (std::sin((t * 0.071f) + phase + c) * scale)
                + (std::cos((t * 0.013f) + phase) * scale * 0.25f);
            stem.audio.setSample(channel, sample, value);
        }
    }

    return stem;
}

StemAudioBuffer sumStem(StemType type, const std::vector<const StemAudioBuffer*>& stems)
{
    StemAudioBuffer result;
    result.type = type;
    result.sampleRate = testSampleRate;
    result.audio.setSize(channelCount, sampleCount);
    result.audio.clear();

    for (const auto* stem : stems)
    {
        for (auto channel = 0; channel < channelCount; ++channel)
            for (auto sample = 0; sample < sampleCount; ++sample)
                result.audio.addSample(channel, sample, stem->audio.getSample(channel, sample));
    }

    return result;
}

StemAudioBuffer subtractStem(StemType type, const StemAudioBuffer& base, const StemAudioBuffer& subtractor)
{
    StemAudioBuffer result;
    result.type = type;
    result.sampleRate = base.sampleRate;
    result.audio.makeCopyOf(base.audio, true);

    for (auto channel = 0; channel < result.audio.getNumChannels(); ++channel)
        for (auto sample = 0; sample < result.audio.getNumSamples(); ++sample)
            result.audio.addSample(channel, sample, -subtractor.audio.getSample(channel, sample));

    return result;
}

StemAudioBuffer delayedCopy(const StemAudioBuffer& source, int delaySamples)
{
    StemAudioBuffer result;
    result.type = source.type;
    result.debugName = source.debugName;
    result.sampleRate = source.sampleRate;
    result.audio.setSize(source.channelCount(), source.sampleCount());
    result.audio.clear();

    for (auto channel = 0; channel < source.channelCount(); ++channel)
        for (auto sample = 0; sample < source.sampleCount(); ++sample)
            if (sample + delaySamples >= 0 && sample + delaySamples < source.sampleCount())
                result.audio.setSample(channel, sample + delaySamples, source.audio.getSample(channel, sample));

    return result;
}

bool buffersApproximatelyEqual(const StemAudioBuffer& actual, const StemAudioBuffer& expected)
{
    if (std::abs(actual.sampleRate - expected.sampleRate) > 0.001
        || actual.audio.getNumChannels() != expected.audio.getNumChannels()
        || actual.audio.getNumSamples() != expected.audio.getNumSamples())
    {
        return false;
    }

    for (auto channel = 0; channel < actual.audio.getNumChannels(); ++channel)
        for (auto sample = 0; sample < actual.audio.getNumSamples(); ++sample)
            if (std::abs(actual.audio.getSample(channel, sample) - expected.audio.getSample(channel, sample)) > tolerance)
                return false;

    return true;
}

bool expect(bool condition, const std::string& name)
{
    if (condition)
        return true;

    std::cerr << "FAIL: " << name << '\n';
    return false;
}

PreparedStemSet makeComplementStemSet()
{
    PreparedStemSet stems;
    auto drums = makeStem(StemType::Drums, 0.05f, 0.1f);
    auto bass = makeStem(StemType::Bass, 0.07f, 0.9f);
    auto vocals = makeStem(StemType::Vocals, 0.04f, 1.7f);
    auto music = makeStem(StemType::MusicResidual, 0.06f, 2.5f);

    stems.fullMix = sumStem(StemType::FullMix, { &drums, &bass, &vocals, &music });
    stems.drumsDirect = drums;
    stems.bassDirect = bass;
    stems.vocalsDirect = vocals;
    stems.noBass = subtractStem(StemType::InstrumentalOriginal, stems.fullMix, bass);
    stems.noDrums = subtractStem(StemType::InstrumentalOriginal, stems.fullMix, drums);
    stems.noVocals = subtractStem(StemType::InstrumentalOriginal, stems.fullMix, vocals);
    stems.instrumentalOriginal = stems.noVocals;
    stems.musicResidual = music;

    stems.drumsDirect.debugName = "DrumsDirect";
    stems.bassDirect.debugName = "BassDirect";
    stems.vocalsDirect.debugName = "VocalsDirect";
    stems.noBass.debugName = "NoBass";
    stems.noDrums.debugName = "NoDrums";
    stems.noVocals.debugName = "NoVocals";

    return stems;
}

bool testComplementDerivationAndFinalReconstruction()
{
    auto expected = makeComplementStemSet();
    PreparedStemSet stems;
    stems.fullMix = expected.fullMix;
    stems.bassDirect = expected.bassDirect;
    stems.drumsDirect = expected.drumsDirect;
    stems.noBass = expected.noBass;
    stems.noDrums = expected.noDrums;
    stems.noVocals = expected.noVocals;

    const auto result = mixdesk::engine::deriveMusicResidualStem(stems);
    const auto validation = mixdesk::engine::validateStemReconstruction(stems);

    return expect(result.succeeded, "complement reconciliation succeeds")
        && expect(buffersApproximatelyEqual(stems.bassFromComplement, expected.bassDirect),
            "BassFromComplement equals original bass")
        && expect(buffersApproximatelyEqual(stems.drumsFromComplement, expected.drumsDirect),
            "DrumsFromComplement equals original drums")
        && expect(buffersApproximatelyEqual(stems.vocalsFromComplement, expected.vocalsDirect),
            "VocalsFromComplement equals original vocals")
        && expect(buffersApproximatelyEqual(stems.musicResidual, expected.musicResidual),
            "MusicResidual equals original music")
        && expect(validation.succeeded && validation.relativeRmsError < 0.000001,
            "final reconstruction validates against FullMix");
}

bool testAlignmentDetectsSmallOffset()
{
    auto stems = makeComplementStemSet();
    auto shiftedNoBass = delayedCopy(stems.noBass, 5);
    shiftedNoBass.debugName = "ShiftedNoBass";

    const auto alignment = mixdesk::engine::alignStemToReference(stems.fullMix, shiftedNoBass, 16);

    return expect(alignment.succeeded, "alignment succeeds for small offset")
        && expect(alignment.offsetSamples == 5, "alignment reports expected offset");
}

bool testMismatchedFormatsFailClearly()
{
    auto rateMismatch = makeComplementStemSet();
    rateMismatch.noBass.sampleRate = 44100.0;
    const auto rateResult = mixdesk::engine::deriveMusicResidualStem(rateMismatch);

    auto channelMismatch = makeComplementStemSet();
    channelMismatch.noDrums.audio.setSize(1, sampleCount, true, true, false);
    const auto channelResult = mixdesk::engine::deriveMusicResidualStem(channelMismatch);

    return expect(! rateResult.succeeded && rateResult.message.containsIgnoreCase("sample rate"),
        "sample-rate mismatch fails clearly")
        && expect(! channelResult.succeeded && channelResult.message.containsIgnoreCase("channel count"),
            "channel-count mismatch fails clearly");
}

bool testPlaybackDoesNotUseComplementStemsAsPublicMix()
{
    auto stems = makeComplementStemSet();
    const auto result = mixdesk::engine::deriveMusicResidualStem(stems);

    mixdesk::model::StemEnableState enabled;
    enabled.music = false;

    const auto channel = 0;
    const auto sample = 33;
    const auto reconstructed = mixdesk::engine::mixPreparedStemsAtSample(stems, enabled, channel, sample);
    const auto expected = stems.drums.audio.getSample(channel, sample)
        + stems.bass.audio.getSample(channel, sample)
        + stems.vocals.audio.getSample(channel, sample);
    const auto invalidNoVocalsPath = stems.drums.audio.getSample(channel, sample)
        + stems.bass.audio.getSample(channel, sample)
        + stems.noVocals.audio.getSample(channel, sample);

    return expect(result.succeeded, "reconciliation for playback graph succeeds")
        && expect(std::abs(reconstructed - expected) < tolerance, "playback sums only public enabled stems")
        && expect(std::abs(reconstructed - invalidNoVocalsPath) > 0.001f,
            "playback does not use NoVocals as a public stem");
}

bool testFullMixPreferredWhenAllPublicStemsEnabled()
{
    auto stems = makeComplementStemSet();
    const auto result = mixdesk::engine::deriveMusicResidualStem(stems);

    const auto channel = 0;
    const auto sample = 40;
    const auto fullMixSample = stems.fullMix.audio.getSample(channel, sample);
    stems.drums.audio.setSample(channel, sample, 123.0f);

    mixdesk::model::StemEnableState enabled;
    const auto usesFullMixAtUnity = mixdesk::engine::canUseFullMixForPlayback(stems, enabled);
    const auto playbackSample = mixdesk::engine::mixPreparedStemsAtSample(stems, enabled, channel, sample);
    enabled.drumsVolume = 0.5f;
    const auto adjustedSample = mixdesk::engine::mixPreparedStemsAtSample(stems, enabled, channel, sample);
    const auto expectedAdjustedSample = (stems.drums.audio.getSample(channel, sample) * enabled.drumsVolume)
        + stems.bass.audio.getSample(channel, sample)
        + stems.musicResidual.audio.getSample(channel, sample)
        + stems.vocals.audio.getSample(channel, sample);

    return expect(result.succeeded, "reconciliation for FullMix preference succeeds")
        && expect(usesFullMixAtUnity, "FullMix is selected when all public stems are enabled")
        && expect(std::abs(playbackSample - fullMixSample) < tolerance, "playback uses FullMix instead of reconstructed stems")
        && expect(! mixdesk::engine::canUseFullMixForPlayback(stems, enabled), "stem volume change disables FullMix shortcut")
        && expect(std::abs(adjustedSample - expectedAdjustedSample) < tolerance, "stem volume changes reconstructed playback level");
}

float totalStemVolume(const mixdesk::model::WorkspaceState& state, StemType stemType)
{
    auto total = 0.0f;
    for (const auto& deck : state.decks)
        if (deck.loadedTrack.has_value())
            total += mixdesk::model::stemVolume(deck.stemEnabled, stemType);
    return total;
}

bool testStemVolumesBalanceAcrossLoadedDecks()
{
    mixdesk::model::WorkspaceState state;
    state.decks.resize(3);
    state.decks[0].id = mixdesk::model::DeckId::A;
    state.decks[1].id = mixdesk::model::DeckId::B;
    state.decks[2].id = mixdesk::model::DeckId::C;

    state.decks[0].loadedTrack = mixdesk::model::LoadedTrack {};
    mixdesk::model::balanceStemVolumesForLoadedDeck(state, mixdesk::model::DeckId::A);

    state.decks[1].loadedTrack = mixdesk::model::LoadedTrack {};
    mixdesk::model::balanceStemVolumesForLoadedDeck(state, mixdesk::model::DeckId::B);

    mixdesk::model::setStemVolumeAcrossLoadedDecks(state, mixdesk::model::DeckId::A, StemType::Drums, 0.8f);

    const auto twoDeckTotal = totalStemVolume(state, StemType::Drums);
    const auto deckADrums = mixdesk::model::stemVolume(state.decks[0].stemEnabled, StemType::Drums);
    const auto deckBDrums = mixdesk::model::stemVolume(state.decks[1].stemEnabled, StemType::Drums);

    state.decks[2].loadedTrack = mixdesk::model::LoadedTrack {};
    mixdesk::model::balanceStemVolumesForLoadedDeck(state, mixdesk::model::DeckId::C);
    const auto threeDeckTotal = totalStemVolume(state, StemType::Drums);

    mixdesk::model::setStemVolumeAcrossLoadedDecks(state, mixdesk::model::DeckId::C, StemType::Drums, 1.0f);
    const auto deckCFullTotal = totalStemVolume(state, StemType::Drums);
    const auto deckCDrums = mixdesk::model::stemVolume(state.decks[2].stemEnabled, StemType::Drums);
    const auto otherDrums = mixdesk::model::stemVolume(state.decks[0].stemEnabled, StemType::Drums)
        + mixdesk::model::stemVolume(state.decks[1].stemEnabled, StemType::Drums);

    return expect(std::abs(twoDeckTotal - 1.0f) < tolerance, "two loaded deck stem total is one")
        && expect(std::abs(deckADrums - 0.8f) < tolerance, "raising one stem keeps requested level")
        && expect(std::abs(deckBDrums - 0.2f) < tolerance, "raising one stem ducks matching stem on other deck")
        && expect(std::abs(threeDeckTotal - 1.0f) < tolerance, "loading third deck keeps stem total at one")
        && expect(std::abs(deckCFullTotal - 1.0f) < tolerance, "full stem ownership still totals one")
        && expect(std::abs(deckCDrums - 1.0f) < tolerance, "raised deck owns full stem")
        && expect(std::abs(otherDrums) < tolerance, "other matching stems are ducked to zero");
}

bool testStemVolumeChangesRenormalizeOtherDecks()
{
    mixdesk::model::WorkspaceState state;
    state.decks.resize(3);
    state.decks[0].id = mixdesk::model::DeckId::A;
    state.decks[1].id = mixdesk::model::DeckId::B;
    state.decks[2].id = mixdesk::model::DeckId::C;

    for (auto& deck : state.decks)
        deck.loadedTrack = mixdesk::model::LoadedTrack {};

    mixdesk::model::setStemVolume(state.decks[0].stemEnabled, StemType::Drums, 0.4f);
    mixdesk::model::setStemVolume(state.decks[1].stemEnabled, StemType::Drums, 0.4f);
    mixdesk::model::setStemVolume(state.decks[2].stemEnabled, StemType::Drums, 0.2f);

    mixdesk::model::setStemVolumeAcrossLoadedDecks(state, mixdesk::model::DeckId::A, StemType::Drums, 0.1f);
    const auto lowerDeckA = mixdesk::model::stemVolume(state.decks[0].stemEnabled, StemType::Drums);
    const auto raisedDeckBAfterLower = mixdesk::model::stemVolume(state.decks[1].stemEnabled, StemType::Drums);
    const auto raisedDeckCAfterLower = mixdesk::model::stemVolume(state.decks[2].stemEnabled, StemType::Drums);
    const auto lowerTotal = totalStemVolume(state, StemType::Drums);

    mixdesk::model::setStemVolumeAcrossLoadedDecks(state, mixdesk::model::DeckId::A, StemType::Drums, 0.7f);
    const auto raisedDeckA = mixdesk::model::stemVolume(state.decks[0].stemEnabled, StemType::Drums);
    const auto duckedDeckB = mixdesk::model::stemVolume(state.decks[1].stemEnabled, StemType::Drums);
    const auto duckedDeckC = mixdesk::model::stemVolume(state.decks[2].stemEnabled, StemType::Drums);
    const auto raisedTotal = totalStemVolume(state, StemType::Drums);

    mixdesk::model::WorkspaceState singleDeckState;
    singleDeckState.decks.resize(1);
    singleDeckState.decks[0].id = mixdesk::model::DeckId::A;
    singleDeckState.decks[0].loadedTrack = mixdesk::model::LoadedTrack {};
    mixdesk::model::setStemVolumeAcrossLoadedDecks(singleDeckState, mixdesk::model::DeckId::A, StemType::Drums, 0.3f);
    const auto singleDeckNormalized = mixdesk::model::stemVolume(singleDeckState.decks[0].stemEnabled, StemType::Drums);

    return expect(std::abs(lowerDeckA - 0.1f) < tolerance, "lowering keeps requested deck volume")
        && expect(std::abs(raisedDeckBAfterLower - 0.6f) < tolerance, "lowering raises deck B to keep total normalized")
        && expect(std::abs(raisedDeckCAfterLower - 0.3f) < tolerance, "lowering raises deck C to keep total normalized")
        && expect(std::abs(lowerTotal - 1.0f) < tolerance, "lowering keeps total normalized")
        && expect(std::abs(raisedDeckA - 0.7f) < tolerance, "raising keeps requested deck volume")
        && expect(std::abs(duckedDeckB - 0.2f) < tolerance, "raising ducks deck B proportionally")
        && expect(std::abs(duckedDeckC - 0.1f) < tolerance, "raising ducks deck C proportionally")
        && expect(std::abs(raisedTotal - 1.0f) < tolerance, "raising caps total at one")
        && expect(std::abs(singleDeckNormalized - 1.0f) < tolerance, "single loaded deck stays normalized");
}

bool testVocalStemButtonAssignsVoiceToOneDeck()
{
    mixdesk::model::WorkspaceState state;
    state.decks.resize(3);
    state.decks[0].id = mixdesk::model::DeckId::A;
    state.decks[1].id = mixdesk::model::DeckId::B;
    state.decks[2].id = mixdesk::model::DeckId::C;

    for (auto& deck : state.decks)
        deck.loadedTrack = mixdesk::model::LoadedTrack {};

    mixdesk::model::setStemVolume(state.decks[0].stemEnabled, StemType::Vocals, 0.2f);
    mixdesk::model::setStemVolume(state.decks[1].stemEnabled, StemType::Vocals, 0.5f);
    mixdesk::model::setStemVolume(state.decks[2].stemEnabled, StemType::Vocals, 0.3f);

    mixdesk::engine::WorkspaceController controller { state };
    controller.dispatch(mixdesk::engine::SetDeckStemEnabledCommand {
        mixdesk::model::DeckId::B,
        StemType::Vocals,
        true });

    const auto snapshot = controller.createSnapshot();

    return expect(! mixdesk::model::isStemEnabled(snapshot.decks[0].stemEnabled, StemType::Vocals),
               "voice button disables deck A vocals")
        && expect(mixdesk::model::isStemEnabled(snapshot.decks[1].stemEnabled, StemType::Vocals),
            "voice button keeps selected deck vocals enabled")
        && expect(! mixdesk::model::isStemEnabled(snapshot.decks[2].stemEnabled, StemType::Vocals),
            "voice button disables deck C vocals")
        && expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[0].stemEnabled, StemType::Vocals)) < tolerance,
            "voice button zeros deck A vocal volume")
        && expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[1].stemEnabled, StemType::Vocals) - 1.0f) < tolerance,
            "voice button gives selected deck full vocal volume")
        && expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[2].stemEnabled, StemType::Vocals)) < tolerance,
            "voice button zeros deck C vocal volume");
}

mixdesk::engine::SetDeckLoadedTrackCommand makeLoadCommand(mixdesk::model::DeckId deckId, double tempo, int beatsPerBar)
{
    mixdesk::model::LoadedTrack track;
    track.name = "test";

    mixdesk::model::BeatGrid beatGrid;
    beatGrid.tempo = tempo;
    beatGrid.bpm = static_cast<int>(std::round(tempo));
    beatGrid.secondsPerBeat = 60.0 / tempo;
    beatGrid.beatsPerBar = beatsPerBar;

    return { deckId, track, beatGrid, {}, {} };
}

bool testFirstLoadedTrackClaimsAllStemVolumes()
{
    mixdesk::model::WorkspaceState state;
    state.decks.resize(3);
    state.decks[0].id = mixdesk::model::DeckId::A;
    state.decks[1].id = mixdesk::model::DeckId::B;
    state.decks[2].id = mixdesk::model::DeckId::C;

    mixdesk::engine::WorkspaceController controller(state);
    controller.dispatch(mixdesk::engine::WorkspaceCommand { makeLoadCommand(mixdesk::model::DeckId::B, 124.0, 4) });

    const auto snapshot = controller.createSnapshot();
    auto ok = true;

    for (const auto stemType : { StemType::Drums, StemType::Bass, StemType::MusicResidual, StemType::Vocals })
    {
        const auto stemName = std::string(mixdesk::model::toString(stemType));

        ok = expect(! mixdesk::model::isStemEnabled(snapshot.decks[0].stemEnabled, stemType),
                 "first load disables deck A " + stemName)
            && ok;
        ok = expect(mixdesk::model::isStemEnabled(snapshot.decks[1].stemEnabled, stemType),
                 "first load enables deck B " + stemName)
            && ok;
        ok = expect(! mixdesk::model::isStemEnabled(snapshot.decks[2].stemEnabled, stemType),
                 "first load disables deck C " + stemName)
            && ok;
        ok = expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[0].stemEnabled, stemType)) < tolerance,
                 "first load zeros deck A " + stemName)
            && ok;
        ok = expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[1].stemEnabled, stemType) - 1.0f) < tolerance,
                 "first load gives deck B full " + stemName)
            && ok;
        ok = expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[2].stemEnabled, stemType)) < tolerance,
                 "first load zeros deck C " + stemName)
            && ok;
    }

    return ok;
}

bool testWorkspaceBpmOnlyAdoptsFirstLoadedTrack()
{
    mixdesk::model::WorkspaceState state;
    state.bpm = 126.0;
    state.beatsPerBar = 4;
    state.decks.resize(3);
    state.decks[0].id = mixdesk::model::DeckId::A;
    state.decks[1].id = mixdesk::model::DeckId::B;
    state.decks[2].id = mixdesk::model::DeckId::C;

    mixdesk::engine::WorkspaceController controller(state);
    controller.dispatch(mixdesk::engine::WorkspaceCommand { makeLoadCommand(mixdesk::model::DeckId::A, 124.25, 4) });
    auto snapshot = controller.createSnapshot();
    const auto firstLoadAdoptedTempo = std::abs(snapshot.bpm - 124.25) < 0.0001 && snapshot.beatsPerBar == 4;

    controller.dispatch(mixdesk::engine::WorkspaceCommand { makeLoadCommand(mixdesk::model::DeckId::B, 130.0, 3) });
    snapshot = controller.createSnapshot();

    return expect(firstLoadAdoptedTempo, "first loaded track sets workspace tempo")
        && expect(std::abs(snapshot.bpm - 124.25) < 0.0001, "later track load does not change workspace BPM")
        && expect(snapshot.beatsPerBar == 4, "later track load does not change workspace bar definition")
        && expect(snapshot.decks[1].beatGrid.has_value() && std::abs(snapshot.decks[1].beatGrid->tempo - 130.0) < 0.0001,
            "later deck still keeps its own beatgrid");
}

bool testLoadingTrackDoesNotChangeStemVolumes()
{
    mixdesk::model::WorkspaceState state;
    state.decks.resize(3);
    state.decks[0].id = mixdesk::model::DeckId::A;
    state.decks[1].id = mixdesk::model::DeckId::B;
    state.decks[2].id = mixdesk::model::DeckId::C;
    state.decks[0].loadedTrack = mixdesk::model::LoadedTrack {};

    mixdesk::model::setStemVolume(state.decks[0].stemEnabled, StemType::Drums, 0.72f);
    mixdesk::model::setStemVolume(state.decks[0].stemEnabled, StemType::Bass, 0.35f);
    mixdesk::model::setStemVolume(state.decks[1].stemEnabled, StemType::Drums, 0.18f);
    mixdesk::model::setStemVolume(state.decks[1].stemEnabled, StemType::Bass, 0.64f);
    mixdesk::model::setStemVolume(state.decks[2].stemEnabled, StemType::Drums, 0.10f);
    mixdesk::model::setStemVolume(state.decks[2].stemEnabled, StemType::Bass, 0.01f);

    const auto beforeA = state.decks[0].stemEnabled;
    const auto beforeB = state.decks[1].stemEnabled;
    const auto beforeC = state.decks[2].stemEnabled;

    mixdesk::engine::WorkspaceController controller(state);
    controller.dispatch(mixdesk::engine::WorkspaceCommand { makeLoadCommand(mixdesk::model::DeckId::B, 124.0, 4) });
    const auto snapshot = controller.createSnapshot();

    return expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[0].stemEnabled, StemType::Drums)
                      - mixdesk::model::stemVolume(beforeA, StemType::Drums)) < tolerance,
            "later loading preserves deck A drum volume")
        && expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[0].stemEnabled, StemType::Bass)
                      - mixdesk::model::stemVolume(beforeA, StemType::Bass)) < tolerance,
            "later loading preserves deck A bass volume")
        && expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[1].stemEnabled, StemType::Drums)
                      - mixdesk::model::stemVolume(beforeB, StemType::Drums)) < tolerance,
            "later loading preserves loaded deck drum volume")
        && expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[1].stemEnabled, StemType::Bass)
                      - mixdesk::model::stemVolume(beforeB, StemType::Bass)) < tolerance,
            "later loading preserves loaded deck bass volume")
        && expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[2].stemEnabled, StemType::Drums)
                      - mixdesk::model::stemVolume(beforeC, StemType::Drums)) < tolerance,
            "later loading preserves deck C drum volume")
        && expect(std::abs(mixdesk::model::stemVolume(snapshot.decks[2].stemEnabled, StemType::Bass)
                      - mixdesk::model::stemVolume(beforeC, StemType::Bass)) < tolerance,
            "later loading preserves deck C bass volume");
}

bool testBeatGridMapsAudioStartBeforeFirstBeat()
{
    mixdesk::model::BeatGrid beatGrid;
    beatGrid.tempo = 120.0;
    beatGrid.bpm = 120;
    beatGrid.secondsPerBeat = 0.5;
    beatGrid.beatsPerBar = 4;
    beatGrid.firstBeatOffsetSeconds = 0.25;
    beatGrid.durationSeconds = 10.0;

    constexpr auto launchOffsetBars = 16;
    constexpr auto expectedAudioStartBar = 15.875;

    const auto audioStartBar = mixdesk::model::trackAudioStartBar(beatGrid, launchOffsetBars);
    const auto firstBeatTime = mixdesk::model::gridBarToTrackTime(beatGrid, launchOffsetBars, launchOffsetBars);
    const auto audioStartTime = mixdesk::model::gridBarToTrackTime(beatGrid, launchOffsetBars, audioStartBar);
    const auto durationBars = mixdesk::model::trackDurationBars(beatGrid);

    return expect(std::abs(audioStartBar - expectedAudioStartBar) < 0.0001,
            "audio bed starts before musical launch when track has lead-in")
        && expect(std::abs(firstBeatTime - beatGrid.firstBeatOffsetSeconds) < 0.0001,
            "launch offset still maps to first beat")
        && expect(std::abs(audioStartTime) < 0.0001,
            "audio start bar maps back to file time zero")
        && expect(std::abs(durationBars - 5.0) < 0.0001,
            "duration in bars uses the track beatgrid");
}
} // namespace

int main()
{
    auto failures = 0;

    failures += testComplementDerivationAndFinalReconstruction() ? 0 : 1;
    failures += testAlignmentDetectsSmallOffset() ? 0 : 1;
    failures += testMismatchedFormatsFailClearly() ? 0 : 1;
    failures += testPlaybackDoesNotUseComplementStemsAsPublicMix() ? 0 : 1;
    failures += testFullMixPreferredWhenAllPublicStemsEnabled() ? 0 : 1;
    failures += testStemVolumesBalanceAcrossLoadedDecks() ? 0 : 1;
    failures += testStemVolumeChangesRenormalizeOtherDecks() ? 0 : 1;
    failures += testVocalStemButtonAssignsVoiceToOneDeck() ? 0 : 1;
    failures += testFirstLoadedTrackClaimsAllStemVolumes() ? 0 : 1;
    failures += testWorkspaceBpmOnlyAdoptsFirstLoadedTrack() ? 0 : 1;
    failures += testLoadingTrackDoesNotChangeStemVolumes() ? 0 : 1;
    failures += testBeatGridMapsAudioStartBeforeFirstBeat() ? 0 : 1;

    if (failures == 0)
    {
        std::cout << "StemPreparationTests passed\n";
        return 0;
    }

    std::cerr << "StemPreparationTests failed: " << failures << '\n';
    return 1;
}
