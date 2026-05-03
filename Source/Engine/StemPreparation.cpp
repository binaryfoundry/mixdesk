#include "StemPreparation.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace mixdesk::engine
{
namespace
{
constexpr auto maxPrototypeChannels = 2;
constexpr auto mp3AlignmentSearchSamples = 4096;
constexpr auto minimumAlignmentConfidence = 0.08;
constexpr auto suspiciousRelativeError = 0.02;

juce::String stringFromView(std::string_view text)
{
    return juce::String(text.data(), static_cast<int>(text.size()));
}

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
    return stringFromView(model::toString(type));
}

juce::String stemName(const StemAudioBuffer& stem)
{
    return stem.debugName.isNotEmpty() ? stem.debugName : stemName(stem.type);
}

void nameStem(StemAudioBuffer& stem, juce::String name)
{
    stem.debugName = std::move(name);
}

float sampleAtIndex(const StemAudioBuffer& stem, int channel, int sampleIndex) noexcept
{
    if (! stem.hasAudio() || sampleIndex < 0 || sampleIndex >= stem.audio.getNumSamples())
        return 0.0f;

    const auto sourceChannel = std::clamp(channel, 0, stem.audio.getNumChannels() - 1);
    return stem.audio.getSample(sourceChannel, sampleIndex);
}

bool samePcmShape(const StemAudioBuffer& reference, const StemAudioBuffer& candidate, juce::String& message)
{
    if (! reference.hasAudio() || ! candidate.hasAudio())
    {
        message = "Missing audio for " + stemName(reference) + " or " + stemName(candidate);
        return false;
    }

    if (std::abs(reference.sampleRate - candidate.sampleRate) > 0.001)
    {
        message = stemName(candidate) + " sample rate does not match " + stemName(reference)
            + " (" + juce::String(candidate.sampleRate, 2) + " vs " + juce::String(reference.sampleRate, 2) + ")";
        return false;
    }

    if (reference.channelCount() != candidate.channelCount())
    {
        message = stemName(candidate) + " channel count does not match " + stemName(reference)
            + " (" + juce::String(candidate.channelCount()) + " vs " + juce::String(reference.channelCount()) + ")";
        return false;
    }

    return true;
}

bool exactPcmShape(const StemAudioBuffer& reference, const StemAudioBuffer& candidate, juce::String& message)
{
    if (! samePcmShape(reference, candidate, message))
        return false;

    if (reference.sampleCount() != candidate.sampleCount())
    {
        message = stemName(candidate) + " length does not match " + stemName(reference)
            + " (" + juce::String(candidate.sampleCount()) + " vs " + juce::String(reference.sampleCount()) + " samples)";
        return false;
    }

    return true;
}

bool exactPcmShape(const StemAudioBuffer& reference, std::initializer_list<const StemAudioBuffer*> candidates)
{
    for (const auto* candidate : candidates)
    {
        juce::String message;
        if (candidate == nullptr || ! exactPcmShape(reference, *candidate, message))
            return false;
    }

    return true;
}

StemAudioBuffer copyStemAs(const StemAudioBuffer& source, model::StemType type, juce::String debugName)
{
    StemAudioBuffer copy;
    copy.type = type;
    copy.debugName = std::move(debugName);
    copy.sampleRate = source.sampleRate;
    copy.sourceFile = source.sourceFile;

    if (source.hasAudio())
        copy.audio.makeCopyOf(source.audio, true);

    return copy;
}

StemAudioBuffer makeAlignedCopy(const StemAudioBuffer& reference,
    const StemAudioBuffer& source,
    int offsetSamples,
    model::StemType type,
    juce::String debugName)
{
    StemAudioBuffer aligned;
    aligned.type = type;
    aligned.debugName = std::move(debugName);
    aligned.sampleRate = reference.sampleRate;
    aligned.sourceFile = source.sourceFile;
    aligned.audio.setSize(reference.channelCount(), reference.sampleCount(), false, true, false);
    aligned.audio.clear();

    for (auto channel = 0; channel < aligned.channelCount(); ++channel)
    {
        auto* destination = aligned.audio.getWritePointer(channel);

        for (auto sample = 0; sample < aligned.sampleCount(); ++sample)
        {
            const auto sourceSample = sample + offsetSamples;
            destination[sample] = sampleAtIndex(source, channel, sourceSample);
        }
    }

    return aligned;
}

std::optional<StemAudioBuffer> alignForSubtraction(const StemAudioBuffer& reference,
    const StemAudioBuffer& source,
    model::StemType type,
    const juce::String& debugName,
    juce::String& report)
{
    if (! source.hasAudio())
        return std::nullopt;

    const auto alignment = alignStemToReference(reference, source, mp3AlignmentSearchSamples);
    report += "\n- Alignment " + debugName + ": " + alignment.message;

    juce::String shapeMessage;
    if (! samePcmShape(reference, source, shapeMessage))
        return std::nullopt;

    if (std::abs(reference.sampleCount() - source.sampleCount()) > mp3AlignmentSearchSamples)
    {
        report += "\n  warning: " + debugName + " length differs beyond the alignment window; skipping candidate.";
        return std::nullopt;
    }

    const auto trustedOffset = alignment.succeeded ? alignment.offsetSamples : 0;
    if (! alignment.succeeded)
        report += "\n  warning: low alignment confidence, using zero offset for " + debugName + ".";

    return makeAlignedCopy(reference, source, trustedOffset, type, debugName);
}

StemAudioBuffer subtractStems(model::StemType type,
    juce::String debugName,
    const StemAudioBuffer& base,
    std::initializer_list<const StemAudioBuffer*> subtractors)
{
    StemAudioBuffer destination;
    destination.type = type;
    destination.debugName = std::move(debugName);
    destination.sampleRate = base.sampleRate;
    destination.audio.setSize(base.channelCount(), base.sampleCount(), false, true, false);

    for (auto channel = 0; channel < base.channelCount(); ++channel)
        destination.audio.copyFrom(channel, 0, base.audio, channel, 0, base.sampleCount());

    for (const auto* subtractor : subtractors)
    {
        if (subtractor == nullptr || ! subtractor->hasAudio())
            continue;

        for (auto channel = 0; channel < destination.channelCount(); ++channel)
        {
            auto* destinationSamples = destination.audio.getWritePointer(channel);

            for (auto sample = 0; sample < destination.sampleCount(); ++sample)
                destinationSamples[sample] -= sampleAtIndex(*subtractor, channel, sample);
        }
    }

    return destination;
}

StemSignalMetrics calculateSignalMetrics(const StemAudioBuffer& stem)
{
    StemSignalMetrics metrics;
    if (! stem.hasAudio())
        return metrics;

    double sum {};
    double sumSquared {};
    auto count = 0.0;

    for (auto channel = 0; channel < stem.channelCount(); ++channel)
    {
        const auto* samples = stem.audio.getReadPointer(channel);
        for (auto sample = 0; sample < stem.sampleCount(); ++sample)
        {
            const auto value = static_cast<double>(samples[sample]);
            metrics.peakLevel = std::max(metrics.peakLevel, std::abs(value));
            sum += value;
            sumSquared += value * value;
            count += 1.0;
        }
    }

    if (count <= 0.0)
        return metrics;

    metrics.rmsLevel = std::sqrt(sumSquared / count);
    metrics.dcOffset = sum / count;
    metrics.crestFactor = metrics.rmsLevel > 0.0 ? metrics.peakLevel / metrics.rmsLevel : 0.0;
    metrics.clippingRisk = metrics.peakLevel >= 0.98;
    return metrics;
}

StemErrorMetrics calculateErrorMetrics(const StemAudioBuffer& reference,
    std::initializer_list<const StemAudioBuffer*> reconstructionStems)
{
    StemErrorMetrics metrics;
    if (! reference.hasAudio())
        return metrics;

    double errorSquared {};
    double referenceSquared {};
    auto count = 0.0;

    for (auto channel = 0; channel < reference.channelCount(); ++channel)
    {
        for (auto sample = 0; sample < reference.sampleCount(); ++sample)
        {
            auto reconstructed = 0.0f;
            for (const auto* stem : reconstructionStems)
                reconstructed += stem == nullptr ? 0.0f : sampleAtIndex(*stem, channel, sample);

            const auto referenceSample = sampleAtIndex(reference, channel, sample);
            const auto error = static_cast<double>(referenceSample - reconstructed);
            errorSquared += error * error;
            referenceSquared += static_cast<double>(referenceSample) * static_cast<double>(referenceSample);
            metrics.peakError = std::max(metrics.peakError, std::abs(error));
            count += 1.0;
        }
    }

    metrics.rmsError = std::sqrt(errorSquared / std::max(1.0, count));
    metrics.referenceRms = std::sqrt(referenceSquared / std::max(1.0, count));
    metrics.relativeRmsError = metrics.referenceRms > 0.0 ? metrics.rmsError / metrics.referenceRms : 0.0;
    return metrics;
}

double correlationBetween(const StemAudioBuffer& a, const StemAudioBuffer& b)
{
    juce::String message;
    if (! exactPcmShape(a, b, message))
        return 0.0;

    double dot {};
    double aSquared {};
    double bSquared {};

    for (auto channel = 0; channel < a.channelCount(); ++channel)
    {
        for (auto sample = 0; sample < a.sampleCount(); ++sample)
        {
            const auto aValue = static_cast<double>(sampleAtIndex(a, channel, sample));
            const auto bValue = static_cast<double>(sampleAtIndex(b, channel, sample));
            dot += aValue * bValue;
            aSquared += aValue * aValue;
            bSquared += bValue * bValue;
        }
    }

    const auto denominator = std::sqrt(aSquared * bSquared);
    return denominator > 0.0 ? std::clamp(dot / denominator, -1.0, 1.0) : 0.0;
}

juce::String formatSignalMetrics(const StemSignalMetrics& metrics)
{
    return "peak " + juce::String(metrics.peakLevel, 5)
        + ", RMS " + juce::String(metrics.rmsLevel, 5)
        + ", DC " + juce::String(metrics.dcOffset, 6)
        + ", crest " + juce::String(metrics.crestFactor, 3)
        + (metrics.clippingRisk ? ", clipping risk" : "");
}

juce::String formatErrorMetrics(const StemErrorMetrics& metrics)
{
    return "RMS error " + juce::String(metrics.rmsError, 8)
        + ", peak error " + juce::String(metrics.peakError, 8)
        + ", relative RMS " + juce::String(metrics.relativeRmsError, 8);
}

StemCandidateQuality makeQualityReport(const juce::String& candidateName,
    const StemAudioBuffer& candidate,
    const StemErrorMetrics& reconstructionError,
    double alternateCorrelation = 0.0)
{
    StemCandidateQuality quality;
    quality.candidateName = candidateName;
    quality.signal = calculateSignalMetrics(candidate);
    quality.reconstructionError = reconstructionError;
    quality.alternateCorrelation = alternateCorrelation;

    if (quality.signal.clippingRisk)
        quality.warning += "clipping risk; ";
    if (quality.reconstructionError.relativeRmsError > suspiciousRelativeError)
        quality.warning += "high reconstruction error; ";

    return quality;
}

void appendQuality(juce::String& report, const StemCandidateQuality& quality)
{
    report += "\n- " + quality.candidateName + ": " + formatSignalMetrics(quality.signal)
        + ", " + formatErrorMetrics(quality.reconstructionError);

    if (std::abs(quality.alternateCorrelation) > 0.0)
        report += ", alternate correlation " + juce::String(quality.alternateCorrelation, 5);

    if (quality.warning.isNotEmpty())
        report += " [" + quality.warning.trimCharactersAtEnd("; ") + "]";
}

const StemAudioBuffer* chooseByReconstructionError(const StemAudioBuffer* direct,
    const StemAudioBuffer* complement,
    const StemErrorMetrics& directError,
    const StemErrorMetrics& complementError,
    juce::String& chosenName)
{
    if (direct != nullptr && direct->hasAudio() && complement != nullptr && complement->hasAudio())
    {
        if (complementError.relativeRmsError <= directError.relativeRmsError)
        {
            chosenName = stemName(*complement);
            return complement;
        }

        chosenName = stemName(*direct);
        return direct;
    }

    if (complement != nullptr && complement->hasAudio())
    {
        chosenName = stemName(*complement);
        return complement;
    }

    if (direct != nullptr && direct->hasAudio())
    {
        chosenName = stemName(*direct);
        return direct;
    }

    chosenName = "Unavailable";
    return nullptr;
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
    result.message = "Reconstruction validated against " + stemName(reference)
        + ": RMS error " + juce::String(result.rmsError, 8)
        + ", peak error " + juce::String(result.peakError, 8)
        + ", relative RMS " + juce::String(result.relativeRmsError, 8);

    if (result.relativeRmsError > suspiciousRelativeError)
        result.message += " (warning: suspiciously high reconstruction error)";

    return result;
}

void clearAudioKeepMetadata(StemAudioBuffer& stem)
{
    stem.audio.setSize(0, 0);
    stem.sampleRate = 0.0;
}

void releaseAnalysisAudio(PreparedStemSet& stems)
{
    for (auto* stem : { &stems.drumsDirect,
             &stems.bassDirect,
             &stems.vocalsDirect,
             &stems.instrumentalOriginal,
             &stems.noBass,
             &stems.noDrums,
             &stems.noVocals,
             &stems.bassFromComplement,
             &stems.drumsFromComplement,
             &stems.vocalsFromComplement,
             &stems.reconciledBass,
             &stems.reconciledDrums,
             &stems.reconciledVocals,
             &stems.reconciledMusic })
    {
        clearAudioKeepMetadata(*stem);
    }
}

std::optional<std::string> pathIfPresent(const StemAudioBuffer& stem)
{
    if (! stem.sourceFile.existsAsFile())
        return std::nullopt;

    return stem.sourceFile.getFullPathName().toStdString();
}

std::optional<std::string> firstPathIfPresent(const StemAudioBuffer& primary, const StemAudioBuffer& fallback)
{
    if (auto path = pathIfPresent(primary))
        return path;

    return pathIfPresent(fallback);
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

StemDerivationResult StemReconciler::reconcile(PreparedStemSet& stems) const
{
    stems.debugReport = "Stem reconciliation report";
    stems.debugReport += "\nSource files:";
    stems.debugReport += "\n- FullMix: " + juce::String(stems.fullMix.hasAudio() ? "found" : "missing");
    stems.debugReport += "\n- BassDirect: " + juce::String(stems.bassDirect.hasAudio() ? "found" : "missing");
    stems.debugReport += "\n- DrumsDirect: " + juce::String(stems.drumsDirect.hasAudio() ? "found" : "missing");
    stems.debugReport += "\n- NoBass: " + juce::String(stems.noBass.hasAudio() ? "found" : "missing");
    stems.debugReport += "\n- NoDrums: " + juce::String(stems.noDrums.hasAudio() ? "found" : "missing");
    stems.debugReport += "\n- NoVocals: " + juce::String(stems.noVocals.hasAudio() ? "found" : "missing");

    if (! stems.fullMix.hasAudio())
    {
        const auto& fallbackReference = stems.noVocals.hasAudio() ? stems.noVocals : stems.instrumentalOriginal;
        if (fallbackReference.hasAudio() && stems.drumsDirect.hasAudio() && stems.bassDirect.hasAudio())
        {
            juce::String shapeMessage;
            if (! exactPcmShape(fallbackReference, stems.drumsDirect, shapeMessage))
                return { false, model::StemDerivationMethod::FromInstrumentalMinusDrumsBass, shapeMessage };
            if (! exactPcmShape(fallbackReference, stems.bassDirect, shapeMessage))
                return { false, model::StemDerivationMethod::FromInstrumentalMinusDrumsBass, shapeMessage };

            stems.drums = copyStemAs(stems.drumsDirect, model::StemType::Drums, "Drums");
            stems.bass = copyStemAs(stems.bassDirect, model::StemType::Bass, "Bass");
            stems.vocals = copyStemAs(stems.vocalsDirect, model::StemType::Vocals, "Vocals");
            stems.reconciledMusic = subtractStems(model::StemType::MusicResidual, "MusicResidual", fallbackReference, { &stems.drums, &stems.bass });
            stems.musicResidual = copyStemAs(stems.reconciledMusic, model::StemType::MusicResidual, "Music");
            stems.musicResidualDerivation = model::StemDerivationMethod::FromInstrumentalMinusDrumsBass;
            stems.musicResidualDerivationDetails = "FullMix unavailable; derived MusicResidual from NoVocals/InstrumentalOriginal - DrumsDirect - BassDirect.";
            stems.chosenBassSource = "BassDirect";
            stems.chosenDrumsSource = "DrumsDirect";
            stems.chosenVocalsSource = stems.vocals.hasAudio() ? "VocalsDirect" : "Unavailable";
            stems.debugReport += "\nChosen playable stems:\n- Bass: BassDirect\n- Drums: DrumsDirect\n- Vocals: " + stems.chosenVocalsSource
                + "\n- Music: MusicResidual fallback";
            return { true, stems.musicResidualDerivation, stems.musicResidualDerivationDetails };
        }

        return { false, model::StemDerivationMethod::Unavailable,
            "Stem reconciliation needs FullMix plus complement stems, or NoVocals/InstrumentalOriginal plus direct drums and bass." };
    }

    juce::String shapeMessage;
    if (stems.bassDirect.hasAudio() && ! samePcmShape(stems.fullMix, stems.bassDirect, shapeMessage))
        return { false, model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals, shapeMessage };
    if (stems.drumsDirect.hasAudio() && ! samePcmShape(stems.fullMix, stems.drumsDirect, shapeMessage))
        return { false, model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals, shapeMessage };
    if (stems.vocalsDirect.hasAudio() && ! samePcmShape(stems.fullMix, stems.vocalsDirect, shapeMessage))
        return { false, model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals, shapeMessage };
    if (stems.noBass.hasAudio() && ! samePcmShape(stems.fullMix, stems.noBass, shapeMessage))
        return { false, model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals, shapeMessage };
    if (stems.noDrums.hasAudio() && ! samePcmShape(stems.fullMix, stems.noDrums, shapeMessage))
        return { false, model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals, shapeMessage };
    if (stems.noVocals.hasAudio() && ! samePcmShape(stems.fullMix, stems.noVocals, shapeMessage))
        return { false, model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals, shapeMessage };

    auto alignedNoBass = alignForSubtraction(stems.fullMix, stems.noBass, model::StemType::InstrumentalOriginal, "NoBass", stems.debugReport);
    auto alignedNoDrums = alignForSubtraction(stems.fullMix, stems.noDrums, model::StemType::InstrumentalOriginal, "NoDrums", stems.debugReport);
    auto alignedNoVocals = alignForSubtraction(stems.fullMix, stems.noVocals, model::StemType::InstrumentalOriginal, "NoVocals", stems.debugReport);
    auto alignedBassDirect = alignForSubtraction(stems.fullMix, stems.bassDirect, model::StemType::Bass, "BassDirect", stems.debugReport);
    auto alignedDrumsDirect = alignForSubtraction(stems.fullMix, stems.drumsDirect, model::StemType::Drums, "DrumsDirect", stems.debugReport);
    auto alignedVocalsDirect = alignForSubtraction(stems.fullMix, stems.vocalsDirect, model::StemType::Vocals, "VocalsDirect", stems.debugReport);

    if (alignedNoBass.has_value())
    {
        stems.noBass = std::move(*alignedNoBass);
        stems.bassFromComplement = subtractStems(model::StemType::Bass, "BassFromComplement", stems.fullMix, { &stems.noBass });
    }

    if (alignedNoDrums.has_value())
    {
        stems.noDrums = std::move(*alignedNoDrums);
        stems.drumsFromComplement = subtractStems(model::StemType::Drums, "DrumsFromComplement", stems.fullMix, { &stems.noDrums });
    }

    if (alignedNoVocals.has_value())
    {
        stems.noVocals = std::move(*alignedNoVocals);
        stems.instrumentalOriginal = copyStemAs(stems.noVocals, model::StemType::InstrumentalOriginal, "InstrumentalOriginal/NoVocals");
        stems.vocalsFromComplement = subtractStems(model::StemType::Vocals, "VocalsFromComplement", stems.fullMix, { &stems.noVocals });
    }

    if (alignedBassDirect.has_value())
        stems.bassDirect = std::move(*alignedBassDirect);
    if (alignedDrumsDirect.has_value())
        stems.drumsDirect = std::move(*alignedDrumsDirect);
    if (alignedVocalsDirect.has_value())
        stems.vocalsDirect = std::move(*alignedVocalsDirect);

    stems.debugReport += "\nDerived candidates:";
    stems.debugReport += "\n- BassFromComplement: " + juce::String(stems.bassFromComplement.hasAudio() ? "generated" : "unavailable");
    stems.debugReport += "\n- DrumsFromComplement: " + juce::String(stems.drumsFromComplement.hasAudio() ? "generated" : "unavailable");
    stems.debugReport += "\n- VocalsFromComplement: " + juce::String(stems.vocalsFromComplement.hasAudio() ? "generated" : "unavailable");

    if (! stems.bassDirect.hasAudio() && ! stems.bassFromComplement.hasAudio())
        return { false, model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals, "Bass is unavailable: need BassDirect or NoBass." };
    if (! stems.drumsDirect.hasAudio() && ! stems.drumsFromComplement.hasAudio())
        return { false, model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals, "Drums are unavailable: need DrumsDirect or NoDrums." };
    if (! stems.vocalsDirect.hasAudio() && ! stems.vocalsFromComplement.hasAudio())
        return { false, model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals, "Vocals are unavailable: need NoVocals or VocalsDirect." };

    const auto bassDirectError = stems.bassDirect.hasAudio() && stems.noBass.hasAudio()
        ? calculateErrorMetrics(stems.fullMix, { &stems.bassDirect, &stems.noBass })
        : StemErrorMetrics {};
    const auto bassComplementError = stems.bassFromComplement.hasAudio() && stems.noBass.hasAudio()
        ? calculateErrorMetrics(stems.fullMix, { &stems.bassFromComplement, &stems.noBass })
        : StemErrorMetrics {};
    const auto bassCorrelation = stems.bassDirect.hasAudio() && stems.bassFromComplement.hasAudio()
        ? correlationBetween(stems.bassDirect, stems.bassFromComplement)
        : 0.0;

    if (stems.bassDirect.hasAudio())
        appendQuality(stems.debugReport, makeQualityReport("BassDirect", stems.bassDirect, bassDirectError, bassCorrelation));
    if (stems.bassFromComplement.hasAudio())
        appendQuality(stems.debugReport, makeQualityReport("BassFromComplement", stems.bassFromComplement, bassComplementError, bassCorrelation));

    const auto drumsDirectError = stems.drumsDirect.hasAudio() && stems.noDrums.hasAudio()
        ? calculateErrorMetrics(stems.fullMix, { &stems.drumsDirect, &stems.noDrums })
        : StemErrorMetrics {};
    const auto drumsComplementError = stems.drumsFromComplement.hasAudio() && stems.noDrums.hasAudio()
        ? calculateErrorMetrics(stems.fullMix, { &stems.drumsFromComplement, &stems.noDrums })
        : StemErrorMetrics {};
    const auto drumsCorrelation = stems.drumsDirect.hasAudio() && stems.drumsFromComplement.hasAudio()
        ? correlationBetween(stems.drumsDirect, stems.drumsFromComplement)
        : 0.0;

    if (stems.drumsDirect.hasAudio())
        appendQuality(stems.debugReport, makeQualityReport("DrumsDirect", stems.drumsDirect, drumsDirectError, drumsCorrelation));
    if (stems.drumsFromComplement.hasAudio())
        appendQuality(stems.debugReport, makeQualityReport("DrumsFromComplement", stems.drumsFromComplement, drumsComplementError, drumsCorrelation));

    const auto vocalsError = stems.vocalsFromComplement.hasAudio() && stems.noVocals.hasAudio()
        ? calculateErrorMetrics(stems.fullMix, { &stems.vocalsFromComplement, &stems.noVocals })
        : StemErrorMetrics {};
    if (stems.vocalsFromComplement.hasAudio())
        appendQuality(stems.debugReport, makeQualityReport("VocalsFromComplement", stems.vocalsFromComplement, vocalsError));

    const auto* chosenBass = chooseByReconstructionError(&stems.bassDirect,
        &stems.bassFromComplement,
        bassDirectError,
        bassComplementError,
        stems.chosenBassSource);
    const auto* chosenDrums = chooseByReconstructionError(&stems.drumsDirect,
        &stems.drumsFromComplement,
        drumsDirectError,
        drumsComplementError,
        stems.chosenDrumsSource);

    const StemAudioBuffer* chosenVocals = nullptr;
    if (stems.vocalsFromComplement.hasAudio())
    {
        chosenVocals = &stems.vocalsFromComplement;
        stems.chosenVocalsSource = "VocalsFromComplement";
    }
    else if (stems.vocalsDirect.hasAudio())
    {
        chosenVocals = &stems.vocalsDirect;
        stems.chosenVocalsSource = "VocalsDirect";
    }

    if (chosenBass == nullptr || chosenDrums == nullptr || chosenVocals == nullptr)
        return { false, model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals, "Could not choose a complete Bass/Drums/Vocals stem set." };

    stems.reconciledBass = copyStemAs(*chosenBass, model::StemType::Bass, "ReconciledBass");
    stems.reconciledDrums = copyStemAs(*chosenDrums, model::StemType::Drums, "ReconciledDrums");
    stems.reconciledVocals = copyStemAs(*chosenVocals, model::StemType::Vocals, "ReconciledVocals");
    stems.reconciledMusic = subtractStems(model::StemType::MusicResidual,
        "MusicResidual",
        stems.fullMix,
        { &stems.reconciledBass, &stems.reconciledDrums, &stems.reconciledVocals });

    stems.bass = copyStemAs(stems.reconciledBass, model::StemType::Bass, "Bass");
    stems.drums = copyStemAs(stems.reconciledDrums, model::StemType::Drums, "Drums");
    stems.vocals = copyStemAs(stems.reconciledVocals, model::StemType::Vocals, "Vocals");
    stems.musicResidual = copyStemAs(stems.reconciledMusic, model::StemType::MusicResidual, "Music");

    stems.musicResidualDerivation = model::StemDerivationMethod::FromFullMixMinusDrumsBassVocals;
    stems.musicResidualDerivationDetails = "Derived candidates from decoded, aligned floating-point PCM. MusicResidual = FullMix - ReconciledBass - ReconciledDrums - ReconciledVocals.";

    stems.debugReport += "\nChosen playable stems:";
    stems.debugReport += "\n- Bass: " + stems.chosenBassSource;
    stems.debugReport += "\n- Drums: " + stems.chosenDrumsSource;
    stems.debugReport += "\n- Vocals: " + stems.chosenVocalsSource;
    stems.debugReport += "\n- Music: MusicResidual";

    const auto finalError = calculateErrorMetrics(stems.fullMix, { &stems.drums, &stems.bass, &stems.vocals, &stems.musicResidual });
    stems.debugReport += "\nValidation:\n- Final reconstruction: " + formatErrorMetrics(finalError);
    if (finalError.relativeRmsError > suspiciousRelativeError)
        stems.debugReport += "\n- warning: final reconstruction error is suspiciously high.";

    // TODO(lossless source stems): prefer WAV/FLAC sources or write decoded cache files before
    // reconciliation so repeated imports avoid MP3 decode drift and lossy round-trips.
    // TODO(confidence-weighted blending): blend direct and complement candidates once reliable
    // subjective/objective quality scores exist.
    // TODO(frequency-dependent reconciliation): allow bass/drum candidate choice to vary by band.
    // TODO(transient-aware drum candidate selection): preserve drum attacks when choosing/blending.
    // TODO(bass low-frequency preservation): favour the cleanest candidate below the crossover.
    // TODO(vocal artifact detection): detect obvious vocal isolation artifacts before playback.
    // TODO(spectral quality metrics): add banded energy/correlation and residual artifact metrics.
    // TODO(visual debug panel in the phrase workspace): expose candidate choice and validation.

    return { true, stems.musicResidualDerivation, stems.musicResidualDerivationDetails };
}

StemPreparation::StemPreparation()
{
    formatManager.registerBasicFormats();
}

PreparedStemSetResult StemPreparation::loadPreparedStemSet(const juce::File& fullMixFile,
    const juce::File& noVocalsFile,
    const juce::File& drumStemFile,
    const juce::File& bassStemFile,
    const juce::File& vocalStemFile,
    const juce::File& noBassFile,
    const juce::File& noDrumFile)
{
    PreparedStemSetResult result;

    if (sourceLooksIndependent(fullMixFile, { noVocalsFile, drumStemFile, bassStemFile, vocalStemFile, noBassFile, noDrumFile }))
        if (auto audio = readStemAudioFile(formatManager, fullMixFile, model::StemType::FullMix))
            result.stems.fullMix = std::move(*audio);

    if (auto audio = readStemAudioFile(formatManager, noVocalsFile, model::StemType::InstrumentalOriginal))
    {
        nameStem(*audio, "NoVocals");
        result.stems.noVocals = std::move(*audio);
        result.stems.instrumentalOriginal.sourceFile = result.stems.noVocals.sourceFile;
        result.stems.instrumentalOriginal.debugName = "InstrumentalOriginal/NoVocals";
    }

    if (auto audio = readStemAudioFile(formatManager, noBassFile, model::StemType::InstrumentalOriginal))
    {
        nameStem(*audio, "NoBass");
        result.stems.noBass = std::move(*audio);
    }

    if (auto audio = readStemAudioFile(formatManager, noDrumFile, model::StemType::InstrumentalOriginal))
    {
        nameStem(*audio, "NoDrums");
        result.stems.noDrums = std::move(*audio);
    }

    if (auto audio = readStemAudioFile(formatManager, drumStemFile, model::StemType::Drums))
    {
        nameStem(*audio, "DrumsDirect");
        result.stems.drumsDirect = std::move(*audio);
    }

    if (auto audio = readStemAudioFile(formatManager, bassStemFile, model::StemType::Bass))
    {
        nameStem(*audio, "BassDirect");
        result.stems.bassDirect = std::move(*audio);
    }

    if (auto audio = readStemAudioFile(formatManager, vocalStemFile, model::StemType::Vocals))
    {
        nameStem(*audio, "VocalsDirect");
        result.stems.vocalsDirect = std::move(*audio);
    }

    const auto derivation = deriveMusicResidualStem(result.stems);
    result.succeeded = derivation.succeeded;
    result.message = derivation.message;

    if (! result.succeeded)
    {
        result.message += "\n" + result.stems.debugReport;
        return result;
    }

    result.validation = validateStemReconstruction(result.stems);
    if (result.validation.succeeded)
        result.message += "\n" + result.validation.message;

    if (result.stems.debugReport.isNotEmpty())
        result.message += "\n" + result.stems.debugReport;

    releaseAnalysisAudio(result.stems);
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
    stem.debugName = stemName(stemType);
    stem.sampleRate = reader->sampleRate;
    stem.sourceFile = file;

    const auto channelCount = std::clamp(static_cast<int>(reader->numChannels), 1, maxPrototypeChannels);
    const auto sampleCount = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, std::numeric_limits<int>::max()));
    stem.audio.setSize(channelCount, sampleCount, false, true, false);
    stem.audio.clear();

    // Raw playback/derivation data is deliberately not normalized. MP3 files can include
    // encoder delay, padding, and lossy artifacts, so all reconciliation happens only after
    // decoding to aligned floating-point PCM buffers. Wider sources are reduced to the first
    // two channels in this prototype before explicit format validation.
    if (! reader->read(&stem.audio, 0, sampleCount, 0, true, channelCount > 1))
        return std::nullopt;

    return stem;
}

StemDerivationResult deriveMusicResidualStem(PreparedStemSet& stems)
{
    return StemReconciler().reconcile(stems);
}

StemAlignmentResult alignStemToReference(const StemAudioBuffer& reference,
    const StemAudioBuffer& candidate,
    int maxOffsetSamples)
{
    StemAlignmentResult result;

    juce::String shapeMessage;
    if (! samePcmShape(reference, candidate, shapeMessage))
    {
        result.message = shapeMessage;
        return result;
    }

    if (std::abs(reference.sampleCount() - candidate.sampleCount()) > maxOffsetSamples)
    {
        result.message = stemName(candidate) + " length differs from " + stemName(reference)
            + " beyond the alignment window (" + juce::String(candidate.sampleCount())
            + " vs " + juce::String(reference.sampleCount()) + " samples)";
        return result;
    }

    const auto maxComparisons = 4096;
    const auto stride = std::max(1, reference.sampleCount() / maxComparisons);
    auto bestScore = -1.0;
    auto bestSignedScore = 0.0;
    auto bestOffset = 0;
    auto zeroOffsetScore = -1.0;
    auto zeroOffsetSignedScore = 0.0;

    for (auto offset = -maxOffsetSamples; offset <= maxOffsetSamples; ++offset)
    {
        double dot {};
        double referenceSquared {};
        double candidateSquared {};
        auto count = 0;

        for (auto channel = 0; channel < reference.channelCount(); ++channel)
        {
            for (auto sample = stride; sample < reference.sampleCount(); sample += stride)
            {
                const auto candidateSample = sample + offset;
                const auto previousCandidateSample = candidateSample - stride;
                if (candidateSample < 0 || candidateSample >= candidate.sampleCount()
                    || previousCandidateSample < 0 || previousCandidateSample >= candidate.sampleCount())
                {
                    continue;
                }

                const auto referenceValue = static_cast<double>(sampleAtIndex(reference, channel, sample)
                    - sampleAtIndex(reference, channel, sample - stride));
                const auto candidateValue = static_cast<double>(sampleAtIndex(candidate, channel, candidateSample)
                    - sampleAtIndex(candidate, channel, previousCandidateSample));

                dot += referenceValue * candidateValue;
                referenceSquared += referenceValue * referenceValue;
                candidateSquared += candidateValue * candidateValue;
                ++count;
            }
        }

        if (count == 0 || referenceSquared <= 0.0 || candidateSquared <= 0.0)
            continue;

        const auto signedScore = dot / std::sqrt(referenceSquared * candidateSquared);
        const auto score = signedScore;
        if (offset == 0)
        {
            zeroOffsetScore = score;
            zeroOffsetSignedScore = signedScore;
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestSignedScore = signedScore;
            bestOffset = offset;
        }
    }

    if (reference.sampleCount() == candidate.sampleCount()
        && zeroOffsetScore >= minimumAlignmentConfidence
        && (bestScore - zeroOffsetScore) < 0.15)
    {
        bestScore = zeroOffsetScore;
        bestSignedScore = zeroOffsetSignedScore;
        bestOffset = 0;
    }

    result.offsetSamples = bestOffset;
    result.confidence = std::max(0.0, bestScore);
    result.succeeded = result.confidence >= minimumAlignmentConfidence;
    result.message = stemName(candidate) + " offset " + juce::String(result.offsetSamples)
        + " samples, confidence " + juce::String(result.confidence, 4)
        + ", signed correlation " + juce::String(bestSignedScore, 4);

    if (! result.succeeded)
        result.message += " (warning: poor confidence)";

    return result;
}

StemValidationResult validateStemReconstruction(const PreparedStemSet& stems)
{
    if (! stems.musicResidual.hasAudio())
        return { false, model::StemType::MusicResidual, stems.musicResidualDerivation, 0.0, 0.0, 0.0, 0.0,
            "Music is unavailable" };

    if (stems.fullMix.hasAudio()
        && exactPcmShape(stems.fullMix, { &stems.drums, &stems.bass, &stems.vocals, &stems.musicResidual }))
    {
        return validateAgainstReference(stems, stems.fullMix, { &stems.drums, &stems.bass, &stems.vocals, &stems.musicResidual });
    }

    const auto& fallbackReference = stems.noVocals.hasAudio() ? stems.noVocals : stems.instrumentalOriginal;
    if (fallbackReference.hasAudio()
        && exactPcmShape(fallbackReference, { &stems.drums, &stems.bass, &stems.musicResidual }))
    {
        return validateAgainstReference(stems, fallbackReference, { &stems.drums, &stems.bass, &stems.musicResidual });
    }

    return { false, model::StemType::FullMix, stems.musicResidualDerivation, 0.0, 0.0, 0.0, 0.0,
        "No aligned reference stem available for reconstruction validation" };
}

model::StemSet createModelStemSet(const PreparedStemSet& stems)
{
    model::StemSet modelStems;
    modelStems.fullMixPath = pathIfPresent(stems.fullMix);
    modelStems.drumsPath = firstPathIfPresent(stems.drums, stems.drumsDirect);
    modelStems.bassPath = firstPathIfPresent(stems.bass, stems.bassDirect);
    modelStems.vocalsPath = firstPathIfPresent(stems.vocals, stems.vocalsDirect);
    modelStems.instrumentalOriginalPath = firstPathIfPresent(stems.instrumentalOriginal, stems.noVocals);
    modelStems.musicResidualPath = pathIfPresent(stems.musicResidual);
    modelStems.noBassPath = pathIfPresent(stems.noBass);
    modelStems.noDrumsPath = pathIfPresent(stems.noDrums);
    modelStems.noVocalsPath = firstPathIfPresent(stems.noVocals, stems.instrumentalOriginal);
    modelStems.musicResidualDerivation = stems.musicResidualDerivation;
    modelStems.musicResidualDerivationDetails = stems.musicResidualDerivationDetails.toStdString();
    modelStems.reconciliationDebugReport = stems.debugReport.toStdString();
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
        stemDurationSeconds(stems.musicResidual) });
}
} // namespace mixdesk::engine
