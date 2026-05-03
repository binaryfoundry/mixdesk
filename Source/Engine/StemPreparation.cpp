#include "StemPreparation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace mixdesk::engine
{
namespace
{
constexpr auto maxPrototypeChannels = 2;

bool sameFile(const juce::File& a, const juce::File& b)
{
    return a.existsAsFile()
        && b.existsAsFile()
        && a.getFullPathName().equalsIgnoreCase(b.getFullPathName());
}

bool sourceLooksIndependent(const juce::File& candidate, const std::initializer_list<juce::File>& otherStems)
{
    if (! candidate.existsAsFile())
        return false;

    for (const auto& otherStem : otherStems)
        if (sameFile(candidate, otherStem))
            return false;

    return true;
}

juce::String stemName(model::StemType type)
{
    return juce::String(model::toString(type).data(), static_cast<int>(model::toString(type).size()));
}

float sampleAtIndex(const StemAudioBuffer& stem, int channel, int sampleIndex) noexcept
{
    if (! stem.hasAudio() || sampleIndex < 0 || sampleIndex >= stem.audio.getNumSamples())
        return 0.0f;

    const auto sourceChannel = std::clamp(channel, 0, stem.audio.getNumChannels() - 1);
    return stem.audio.getSample(sourceChannel, sampleIndex);
}

StemDerivationResult validateSourceSet(model::StemDerivationMethod method,
    const std::initializer_list<const StemAudioBuffer*>& sources)
{
    if (sources.size() == 0)
        return { false, method, "No source stems supplied" };

    const StemAudioBuffer* reference = nullptr;
    for (const auto* source : sources)
    {
        if (source == nullptr || ! source->hasAudio())
            return { false, method, "Missing source stem for " + juce::String(model::toString(method).data(),
                static_cast<int>(model::toString(method).size())) };

        if (reference == nullptr)
            reference = source;
    }

    if (reference == nullptr)
        return { false, method, "No source stem has audio" };

    for (const auto* source : sources)
    {
        if (std::abs(source->sampleRate - reference->sampleRate) > 0.001)
        {
            return { false, method, stemName(source->type) + " sample rate does not match "
                + stemName(reference->type) + " (" + juce::String(source->sampleRate, 2)
                + " vs " + juce::String(reference->sampleRate, 2) + ")" };
        }

        if (source->channelCount() != reference->channelCount())
        {
            return { false, method, stemName(source->type) + " channel count does not match "
                + stemName(reference->type) + " (" + juce::String(source->channelCount())
                + " vs " + juce::String(reference->channelCount()) + ")" };
        }

        if (source->sampleCount() != reference->sampleCount())
        {
            return { false, method, stemName(source->type) + " length does not match "
                + stemName(reference->type) + " (" + juce::String(source->sampleCount())
                + " vs " + juce::String(reference->sampleCount()) + " samples)" };
        }
    }

    return { true, method, "Source stems are sample-rate, channel-count, and sample-count aligned" };
}

void deriveResidualFrom(StemAudioBuffer& destination,
    const StemAudioBuffer& base,
    const std::initializer_list<const StemAudioBuffer*> subtractors)
{
    destination.type = model::StemType::MusicResidual;
    destination.sampleRate = base.sampleRate;
    destination.sourceFile = {};
    destination.audio.setSize(base.channelCount(), base.sampleCount(), false, true, false);

    for (auto channel = 0; channel < base.channelCount(); ++channel)
        destination.audio.copyFrom(channel, 0, base.audio, channel, 0, base.sampleCount());

    for (const auto* subtractor : subtractors)
    {
        if (subtractor == nullptr || ! subtractor->hasAudio())
            continue;

        for (auto channel = 0; channel < destination.channelCount(); ++channel)
        {
            auto* residualSamples = destination.audio.getWritePointer(channel);
            const auto* sourceSamples = subtractor->audio.getReadPointer(channel);

            for (auto sample = 0; sample < destination.sampleCount(); ++sample)
                residualSamples[sample] -= sourceSamples[sample];
        }
    }
}

bool validationSourcesAligned(const std::initializer_list<const StemAudioBuffer*>& sources)
{
    return validateSourceSet(model::StemDerivationMethod::Unavailable, sources).succeeded;
}

StemValidationResult validateAgainstReference(const PreparedStemSet& stems,
    const StemAudioBuffer& reference,
    std::initializer_list<const StemAudioBuffer*> reconstructionStems)
{
    StemValidationResult result;
    result.succeeded = true;
    result.referenceStem = reference.type;
    result.method = stems.musicResidualDerivation;

    const auto channelCount = reference.channelCount();
    const auto sampleCount = reference.sampleCount();
    result.channels.reserve(static_cast<std::size_t>(channelCount));

    double totalErrorSquared {};
    double totalReferenceSquared {};
    auto totalPeakError = 0.0;
    auto totalSampleCount = 0.0;

    for (auto channel = 0; channel < channelCount; ++channel)
    {
        double channelErrorSquared {};
        double channelReferenceSquared {};
        auto channelPeakError = 0.0;

        for (auto sample = 0; sample < sampleCount; ++sample)
        {
            auto reconstructed = 0.0f;
            for (const auto* stem : reconstructionStems)
                reconstructed += stem == nullptr ? 0.0f : sampleAtIndex(*stem, channel, sample);

            const auto referenceSample = sampleAtIndex(reference, channel, sample);
            const auto error = static_cast<double>(referenceSample - reconstructed);
            channelErrorSquared += error * error;
            channelReferenceSquared += static_cast<double>(referenceSample) * static_cast<double>(referenceSample);
            channelPeakError = std::max(channelPeakError, std::abs(error));
        }

        const auto count = static_cast<double>(std::max(1, sampleCount));
        const auto channelRmsError = std::sqrt(channelErrorSquared / count);
        const auto channelReferenceRms = std::sqrt(channelReferenceSquared / count);
        const auto channelRelative = channelReferenceRms > 0.0 ? channelRmsError / channelReferenceRms : 0.0;

        result.channels.push_back({ channel, channelRmsError, channelPeakError, channelReferenceRms, channelRelative });

        totalErrorSquared += channelErrorSquared;
        totalReferenceSquared += channelReferenceSquared;
        totalPeakError = std::max(totalPeakError, channelPeakError);
        totalSampleCount += count;
    }

    result.rmsError = std::sqrt(totalErrorSquared / std::max(1.0, totalSampleCount));
    result.peakError = totalPeakError;
    result.referenceRms = std::sqrt(totalReferenceSquared / std::max(1.0, totalSampleCount));
    result.relativeRmsError = result.referenceRms > 0.0 ? result.rmsError / result.referenceRms : 0.0;
    result.message = "Reconstruction validated against " + stemName(reference.type)
        + ": RMS error " + juce::String(result.rmsError, 8)
        + ", peak error " + juce::String(result.peakError, 8)
        + ", relative RMS " + juce::String(result.relativeRmsError, 8);

    return result;
}

std::optional<std::string> pathIfPresent(const StemAudioBuffer& stem)
{
    if (! stem.sourceFile.existsAsFile())
        return std::nullopt;

    return stem.sourceFile.getFullPathName().toStdString();
}
} // namespace

bool StemAudioBuffer::hasAudio() const noexcept
{
    return sampleRate > 0.0 && audio.getNumChannels() > 0 && audio.getNumSamples() > 0;
}

int StemAudioBuffer::channelCount() const noexcept
{
    return audio.getNumChannels();
}

int StemAudioBuffer::sampleCount() const noexcept
{
    return audio.getNumSamples();
}

const StemAudioBuffer* PreparedStemSet::bufferFor(model::StemType stemType) const noexcept
{
    switch (stemType)
    {
        case model::StemType::FullMix: return &fullMix;
        case model::StemType::Drums: return &drums;
        case model::StemType::Bass: return &bass;
        case model::StemType::Vocals: return &vocals;
        case model::StemType::InstrumentalOriginal: return &instrumentalOriginal;
        case model::StemType::MusicResidual: return &musicResidual;
    }

    return nullptr;
}

StemAudioBuffer* PreparedStemSet::bufferFor(model::StemType stemType) noexcept
{
    return const_cast<StemAudioBuffer*>(std::as_const(*this).bufferFor(stemType));
}

StemPreparation::StemPreparation()
{
    formatManager.registerBasicFormats();
}

PreparedStemSetResult StemPreparation::loadPreparedStemSet(const juce::File& fullMixFile,
    const juce::File& instrumentalFile,
    const juce::File& drumStemFile,
    const juce::File& bassStemFile,
    const juce::File& vocalStemFile)
{
    PreparedStemSetResult result;

    if (sourceLooksIndependent(fullMixFile, { instrumentalFile, drumStemFile, bassStemFile, vocalStemFile }))
        if (auto audio = readStemAudioFile(formatManager, fullMixFile, model::StemType::FullMix))
            result.stems.fullMix = std::move(*audio);

    if (auto audio = readStemAudioFile(formatManager, instrumentalFile, model::StemType::InstrumentalOriginal))
        result.stems.instrumentalOriginal = std::move(*audio);

    if (auto audio = readStemAudioFile(formatManager, drumStemFile, model::StemType::Drums))
        result.stems.drums = std::move(*audio);

    if (auto audio = readStemAudioFile(formatManager, bassStemFile, model::StemType::Bass))
        result.stems.bass = std::move(*audio);

    if (auto audio = readStemAudioFile(formatManager, vocalStemFile, model::StemType::Vocals))
        result.stems.vocals = std::move(*audio);

    const auto derivation = deriveMusicResidualStem(result.stems);
    result.succeeded = derivation.succeeded;
    result.message = derivation.message;

    if (! result.succeeded)
        return result;

    result.validation = validateStemReconstruction(result.stems);
    if (result.validation.succeeded)
        result.message += "\n" + result.validation.message;

    return result;
}

std::optional<StemAudioBuffer> readStemAudioFile(juce::AudioFormatManager& formatManager,
    const juce::File& file,
    model::StemType stemType)
{
    if (! file.existsAsFile())
        return std::nullopt;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return std::nullopt;

    StemAudioBuffer stem;
    stem.type = stemType;
    stem.sampleRate = reader->sampleRate;
    stem.sourceFile = file;

    const auto channelCount = std::clamp(static_cast<int>(reader->numChannels), 1, maxPrototypeChannels);
    const auto sampleCount = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, std::numeric_limits<int>::max()));
    stem.audio.setSize(channelCount, sampleCount, false, true, false);
    stem.audio.clear();

    // Raw playback/derivation data is deliberately not normalized. For this prototype,
    // files are imported as mono or stereo float buffers; wider sources are reduced to
    // their first two channels before format validation.
    if (! reader->read(&stem.audio, 0, sampleCount, 0, true, channelCount > 1))
        return std::nullopt;

    return stem;
}

StemDerivationResult deriveMusicResidualStem(PreparedStemSet& stems)
{
    juce::String preferredFailure;

    if (stems.fullMix.hasAudio() && stems.drums.hasAudio() && stems.bass.hasAudio() && stems.vocals.hasAudio())
    {
        const auto validation = validateSourceSet(model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals,
            { &stems.fullMix, &stems.drums, &stems.bass, &stems.vocals });

        if (validation.succeeded)
        {
            deriveResidualFrom(stems.musicResidual, stems.fullMix, { &stems.drums, &stems.bass, &stems.vocals });
            stems.musicResidualDerivation = validation.method;
            stems.musicResidualDerivationDetails = validation.message;
            return { true, validation.method, "Derived MusicResidual from FullMix - Drums - Bass - Vocals" };
        }

        preferredFailure = validation.message;
    }

    if (stems.instrumentalOriginal.hasAudio() && stems.drums.hasAudio() && stems.bass.hasAudio())
    {
        const auto validation = validateSourceSet(model::StemDerivationMethod::FromInstrumentalMinusDrumsBass,
            { &stems.instrumentalOriginal, &stems.drums, &stems.bass });

        if (validation.succeeded)
        {
            deriveResidualFrom(stems.musicResidual, stems.instrumentalOriginal, { &stems.drums, &stems.bass });
            stems.musicResidualDerivation = validation.method;
            stems.musicResidualDerivationDetails = preferredFailure.isNotEmpty()
                ? "Preferred full-mix derivation was not usable: " + preferredFailure + ". " + validation.message
                : validation.message;
            return { true, validation.method, "Derived MusicResidual from Instrumental - Drums - Bass"
                + (preferredFailure.isNotEmpty() ? " after full-mix derivation failed: " + preferredFailure : juce::String()) };
        }

        return { false, validation.method, validation.message };
    }

    if (preferredFailure.isNotEmpty())
        return { false, model::StemDerivationMethod::Unavailable, preferredFailure };

    return { false, model::StemDerivationMethod::Unavailable,
        "MusicResidual unavailable: need FullMix+Drums+Bass+Vocals or Instrumental+Drums+Bass" };
}

StemValidationResult validateStemReconstruction(const PreparedStemSet& stems)
{
    if (! stems.musicResidual.hasAudio())
        return { false, model::StemType::MusicResidual, stems.musicResidualDerivation, 0.0, 0.0, 0.0, 0.0,
            "MusicResidual is unavailable" };

    if (stems.fullMix.hasAudio()
        && validationSourcesAligned({ &stems.fullMix, &stems.drums, &stems.bass, &stems.musicResidual, &stems.vocals }))
    {
        return validateAgainstReference(stems, stems.fullMix, { &stems.drums, &stems.bass, &stems.musicResidual, &stems.vocals });
    }

    if (stems.instrumentalOriginal.hasAudio()
        && validationSourcesAligned({ &stems.instrumentalOriginal, &stems.drums, &stems.bass, &stems.musicResidual }))
    {
        return validateAgainstReference(stems, stems.instrumentalOriginal, { &stems.drums, &stems.bass, &stems.musicResidual });
    }

    return { false, model::StemType::FullMix, stems.musicResidualDerivation, 0.0, 0.0, 0.0, 0.0,
        "No aligned reference stem available for reconstruction validation" };
}

model::StemSet createModelStemSet(const PreparedStemSet& stems)
{
    model::StemSet modelStems;
    modelStems.fullMixPath = pathIfPresent(stems.fullMix);
    modelStems.drumsPath = pathIfPresent(stems.drums);
    modelStems.bassPath = pathIfPresent(stems.bass);
    modelStems.vocalsPath = pathIfPresent(stems.vocals);
    modelStems.instrumentalOriginalPath = pathIfPresent(stems.instrumentalOriginal);
    modelStems.musicResidualPath = pathIfPresent(stems.musicResidual);
    modelStems.musicResidualDerivation = stems.musicResidualDerivation;
    modelStems.musicResidualDerivationDetails = stems.musicResidualDerivationDetails.toStdString();
    return modelStems;
}

bool canUseFullMixForPlayback(const PreparedStemSet& stems, const model::StemEnableState& enabled) noexcept
{
    return stems.fullMix.hasAudio() && model::areAllPublicStemsEnabled(enabled);
}

float mixPreparedStemsAtSample(const PreparedStemSet& stems,
    const model::StemEnableState& enabled,
    int channel,
    int sampleIndex) noexcept
{
    if (canUseFullMixForPlayback(stems, enabled))
        return sampleAtIndex(stems.fullMix, channel, sampleIndex);

    return (enabled.drums ? sampleAtIndex(stems.drums, channel, sampleIndex) : 0.0f)
        + (enabled.bass ? sampleAtIndex(stems.bass, channel, sampleIndex) : 0.0f)
        + (enabled.music ? sampleAtIndex(stems.musicResidual, channel, sampleIndex) : 0.0f)
        + (enabled.vocals ? sampleAtIndex(stems.vocals, channel, sampleIndex) : 0.0f);
}

double stemDurationSeconds(const StemAudioBuffer& stem) noexcept
{
    if (! stem.hasAudio())
        return 0.0;

    return static_cast<double>(stem.sampleCount()) / stem.sampleRate;
}

double preparedStemSetDurationSeconds(const PreparedStemSet& stems) noexcept
{
    return std::max({ stemDurationSeconds(stems.fullMix),
        stemDurationSeconds(stems.drums),
        stemDurationSeconds(stems.bass),
        stemDurationSeconds(stems.vocals),
        stemDurationSeconds(stems.instrumentalOriginal),
        stemDurationSeconds(stems.musicResidual) });
}
} // namespace mixdesk::engine
