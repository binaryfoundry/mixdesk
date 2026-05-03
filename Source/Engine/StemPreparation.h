#pragma once

#include "Model/PhraseModel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <optional>
#include <vector>

namespace mixdesk::engine
{
struct StemAudioBuffer
{
    model::StemType type { model::StemType::FullMix };
    juce::String debugName;
    juce::AudioBuffer<float> audio;
    double sampleRate {};
    juce::File sourceFile;

    [[nodiscard]] bool hasAudio() const noexcept;
    [[nodiscard]] int channelCount() const noexcept;
    [[nodiscard]] int sampleCount() const noexcept;
};

struct PreparedStemSet
{
    StemAudioBuffer fullMix { model::StemType::FullMix };

    // Public playable stems. These are the only stem buffers the real-time path should mix.
    StemAudioBuffer drums { model::StemType::Drums };
    StemAudioBuffer bass { model::StemType::Bass };
    StemAudioBuffer vocals { model::StemType::Vocals };
    StemAudioBuffer musicResidual { model::StemType::MusicResidual };

    // Source/complement buffers used during offline import/preprocessing. They are analysis
    // material, not public DJ-facing performance stems.
    StemAudioBuffer drumsDirect { model::StemType::Drums };
    StemAudioBuffer bassDirect { model::StemType::Bass };
    StemAudioBuffer vocalsDirect { model::StemType::Vocals };
    StemAudioBuffer instrumentalOriginal { model::StemType::InstrumentalOriginal };
    StemAudioBuffer noBass { model::StemType::InstrumentalOriginal };
    StemAudioBuffer noDrums { model::StemType::InstrumentalOriginal };
    StemAudioBuffer noVocals { model::StemType::InstrumentalOriginal };

    // Internal candidates produced during reconciliation. loadPreparedStemSet() releases their
    // audio before returning to keep the playback snapshot small; tests and debug tools can call
    // deriveMusicResidualStem() directly to inspect them.
    StemAudioBuffer bassFromComplement { model::StemType::Bass };
    StemAudioBuffer drumsFromComplement { model::StemType::Drums };
    StemAudioBuffer vocalsFromComplement { model::StemType::Vocals };
    StemAudioBuffer reconciledBass { model::StemType::Bass };
    StemAudioBuffer reconciledDrums { model::StemType::Drums };
    StemAudioBuffer reconciledVocals { model::StemType::Vocals };
    StemAudioBuffer reconciledMusic { model::StemType::MusicResidual };

    model::StemDerivationMethod musicResidualDerivation { model::StemDerivationMethod::Unavailable };
    juce::String musicResidualDerivationDetails;
    juce::String chosenBassSource;
    juce::String chosenDrumsSource;
    juce::String chosenVocalsSource;
    juce::String debugReport;

    [[nodiscard]] const StemAudioBuffer* bufferFor(model::StemType stemType) const noexcept;
    [[nodiscard]] StemAudioBuffer* bufferFor(model::StemType stemType) noexcept;
};

struct StemDerivationResult
{
    bool succeeded {};
    model::StemDerivationMethod method { model::StemDerivationMethod::Unavailable };
    juce::String message;
};

struct StemChannelValidationMetrics
{
    int channel {};
    double rmsError {};
    double peakError {};
    double referenceRms {};
    double relativeRmsError {};
};

struct StemValidationResult
{
    bool succeeded {};
    model::StemType referenceStem { model::StemType::FullMix };
    model::StemDerivationMethod method { model::StemDerivationMethod::Unavailable };
    double rmsError {};
    double peakError {};
    double referenceRms {};
    double relativeRmsError {};
    juce::String message;
    std::vector<StemChannelValidationMetrics> channels;
};

struct StemAlignmentResult
{
    bool succeeded {};
    int offsetSamples {};
    double confidence {};
    juce::String message;
};

struct StemSignalMetrics
{
    double peakLevel {};
    double rmsLevel {};
    double dcOffset {};
    double crestFactor {};
    bool clippingRisk {};
};

struct StemErrorMetrics
{
    double rmsError {};
    double peakError {};
    double referenceRms {};
    double relativeRmsError {};
};

struct StemCandidateQuality
{
    juce::String candidateName;
    StemSignalMetrics signal;
    StemErrorMetrics reconstructionError;
    double alternateCorrelation {};
    juce::String warning;
};

struct PreparedStemSetResult
{
    bool succeeded {};
    PreparedStemSet stems;
    StemValidationResult validation;
    juce::String message;
};

class StemReconciler
{
public:
    [[nodiscard]] StemDerivationResult reconcile(PreparedStemSet& stems) const;
};

class StemPreparation
{
public:
    StemPreparation();

    [[nodiscard]] PreparedStemSetResult loadPreparedStemSet(const juce::File& fullMixFile,
        const juce::File& noVocalsFile,
        const juce::File& drumStemFile,
        const juce::File& bassStemFile,
        const juce::File& vocalStemFile,
        const juce::File& noBassFile = {},
        const juce::File& noDrumFile = {});

private:
    juce::AudioFormatManager formatManager;
};

[[nodiscard]] std::optional<StemAudioBuffer> readStemAudioFile(juce::AudioFormatManager& formatManager,
    const juce::File& file,
    model::StemType stemType);

// TODO(real audio engine): run source decoding, alignment, reconciliation, residual generation,
// and cache writing during import/analysis/preprocessing or background cache generation. Never
// perform this sample subtraction or metric work in the performance path.
[[nodiscard]] StemDerivationResult deriveMusicResidualStem(PreparedStemSet& stems);
[[nodiscard]] StemAlignmentResult alignStemToReference(const StemAudioBuffer& reference,
    const StemAudioBuffer& candidate,
    int maxOffsetSamples = 2048);
[[nodiscard]] StemValidationResult validateStemReconstruction(const PreparedStemSet& stems);
[[nodiscard]] model::StemSet createModelStemSet(const PreparedStemSet& stems);
[[nodiscard]] bool canUseFullMixForPlayback(const PreparedStemSet& stems, const model::StemEnableState& enabled) noexcept;
[[nodiscard]] float mixPreparedStemsAtSample(const PreparedStemSet& stems,
    const model::StemEnableState& enabled,
    int channel,
    int sampleIndex) noexcept;
[[nodiscard]] double stemDurationSeconds(const StemAudioBuffer& stem) noexcept;
[[nodiscard]] double preparedStemSetDurationSeconds(const PreparedStemSet& stems) noexcept;
} // namespace mixdesk::engine
