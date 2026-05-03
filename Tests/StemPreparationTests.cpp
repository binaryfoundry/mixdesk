#include "Engine/StemPreparation.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr auto testSampleRate = 48000.0;
constexpr auto channelCount = 2;
constexpr auto sampleCount = 16;
constexpr auto tolerance = 0.000001f;

using mixdesk::engine::PreparedStemSet;
using mixdesk::engine::StemAudioBuffer;
using mixdesk::model::StemType;

StemAudioBuffer makeStem(StemType type, float baseValue)
{
    StemAudioBuffer stem;
    stem.type = type;
    stem.sampleRate = testSampleRate;
    stem.audio.setSize(channelCount, sampleCount);

    for (auto channel = 0; channel < channelCount; ++channel)
        for (auto sample = 0; sample < sampleCount; ++sample)
            stem.audio.setSample(channel, sample, baseValue
                + (static_cast<float>(channel) * 0.01f)
                + (static_cast<float>(sample) * 0.001f));

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

bool buffersApproximatelyEqual(const StemAudioBuffer& actual, const StemAudioBuffer& expected)
{
    if (actual.sampleRate != expected.sampleRate
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

PreparedStemSet makeSeparatedStemSet()
{
    PreparedStemSet stems;
    stems.drums = makeStem(StemType::Drums, 0.03f);
    stems.bass = makeStem(StemType::Bass, 0.07f);
    stems.musicResidual = makeStem(StemType::MusicResidual, 0.11f);
    stems.vocals = makeStem(StemType::Vocals, 0.05f);
    stems.instrumentalOriginal = sumStem(StemType::InstrumentalOriginal, { &stems.drums, &stems.bass, &stems.musicResidual });
    stems.fullMix = sumStem(StemType::FullMix, { &stems.drums, &stems.bass, &stems.musicResidual, &stems.vocals });
    return stems;
}

bool testFullMixDerivation()
{
    auto expected = makeSeparatedStemSet();
    PreparedStemSet stems;
    stems.fullMix = expected.fullMix;
    stems.drums = expected.drums;
    stems.bass = expected.bass;
    stems.vocals = expected.vocals;

    const auto result = mixdesk::engine::deriveMusicResidualStem(stems);
    const auto validation = mixdesk::engine::validateStemReconstruction(stems);

    return expect(result.succeeded, "full-mix derivation succeeds")
        && expect(result.method == mixdesk::model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals,
            "full-mix derivation method selected")
        && expect(buffersApproximatelyEqual(stems.musicResidual, expected.musicResidual),
            "full-mix derived music equals original music")
        && expect(validation.succeeded && validation.relativeRmsError < 0.000001,
            "full-mix reconstruction validates");
}

bool testInstrumentalFallbackDerivation()
{
    auto expected = makeSeparatedStemSet();
    PreparedStemSet stems;
    stems.instrumentalOriginal = expected.instrumentalOriginal;
    stems.drums = expected.drums;
    stems.bass = expected.bass;
    stems.vocals = expected.vocals;

    const auto result = mixdesk::engine::deriveMusicResidualStem(stems);
    const auto validation = mixdesk::engine::validateStemReconstruction(stems);

    return expect(result.succeeded, "instrumental fallback derivation succeeds")
        && expect(result.method == mixdesk::model::StemDerivationMethod::FromInstrumentalMinusDrumsBass,
            "instrumental fallback method selected")
        && expect(buffersApproximatelyEqual(stems.musicResidual, expected.musicResidual),
            "instrumental fallback derived music equals original music")
        && expect(validation.succeeded && validation.referenceStem == StemType::InstrumentalOriginal,
            "instrumental fallback validates against original instrumental");
}

bool testPlaybackDoesNotDoubleCountInstrumental()
{
    auto stems = makeSeparatedStemSet();
    stems.fullMix = {};
    const auto result = mixdesk::engine::deriveMusicResidualStem(stems);

    mixdesk::model::StemEnableState enabled;
    const auto channel = 0;
    const auto sample = 4;
    const auto reconstructed = mixdesk::engine::mixPreparedStemsAtSample(stems, enabled, channel, sample);
    const auto expected = stems.drums.audio.getSample(channel, sample)
        + stems.bass.audio.getSample(channel, sample)
        + stems.musicResidual.audio.getSample(channel, sample)
        + stems.vocals.audio.getSample(channel, sample);
    const auto doubleCounted = stems.drums.audio.getSample(channel, sample)
        + stems.bass.audio.getSample(channel, sample)
        + stems.instrumentalOriginal.audio.getSample(channel, sample)
        + stems.vocals.audio.getSample(channel, sample);

    return expect(result.succeeded, "fallback derivation for playback selection succeeds")
        && expect(std::abs(reconstructed - expected) < tolerance, "playback reconstructs public stems")
        && expect(std::abs(reconstructed - doubleCounted) > 0.001f, "playback does not add InstrumentalOriginal");
}

bool testMismatchedFormatsFailClearly()
{
    auto rateMismatch = makeSeparatedStemSet();
    rateMismatch.instrumentalOriginal = {};
    rateMismatch.vocals.sampleRate = 44100.0;
    const auto rateResult = mixdesk::engine::deriveMusicResidualStem(rateMismatch);

    auto lengthMismatch = makeSeparatedStemSet();
    lengthMismatch.instrumentalOriginal = {};
    lengthMismatch.vocals.audio.setSize(channelCount, sampleCount - 1, true, true, false);
    const auto lengthResult = mixdesk::engine::deriveMusicResidualStem(lengthMismatch);

    return expect(! rateResult.succeeded && rateResult.message.containsIgnoreCase("sample rate"),
        "sample-rate mismatch fails clearly")
        && expect(! lengthResult.succeeded && lengthResult.message.containsIgnoreCase("length"),
            "length mismatch fails clearly");
}
} // namespace

int main()
{
    auto failures = 0;

    failures += testFullMixDerivation() ? 0 : 1;
    failures += testInstrumentalFallbackDerivation() ? 0 : 1;
    failures += testPlaybackDoesNotDoubleCountInstrumental() ? 0 : 1;
    failures += testMismatchedFormatsFailClearly() ? 0 : 1;

    if (failures == 0)
    {
        std::cout << "StemPreparationTests passed\n";
        return 0;
    }

    std::cerr << "StemPreparationTests failed: " << failures << '\n';
    return 1;
}
