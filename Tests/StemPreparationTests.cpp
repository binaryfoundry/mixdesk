#include "Engine/StemPreparation.h"

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
    const auto playbackSample = mixdesk::engine::mixPreparedStemsAtSample(stems, enabled, channel, sample);

    return expect(result.succeeded, "reconciliation for FullMix preference succeeds")
        && expect(mixdesk::engine::canUseFullMixForPlayback(stems, enabled), "FullMix is selected when all public stems are enabled")
        && expect(std::abs(playbackSample - fullMixSample) < tolerance, "playback uses FullMix instead of reconstructed stems");
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

    if (failures == 0)
    {
        std::cout << "StemPreparationTests passed\n";
        return 0;
    }

    std::cerr << "StemPreparationTests failed: " << failures << '\n';
    return 1;
}
