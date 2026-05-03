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
    StemAudioBuffer drums { model::StemType::Drums };
    StemAudioBuffer bass { model::StemType::Bass };
    StemAudioBuffer vocals { model::StemType::Vocals };
    StemAudioBuffer instrumentalOriginal { model::StemType::InstrumentalOriginal };
    StemAudioBuffer musicResidual { model::StemType::MusicResidual };
    model::StemDerivationMethod musicResidualDerivation { model::StemDerivationMethod::Unavailable };
    juce::String musicResidualDerivationDetails;

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

struct PreparedStemSetResult
{
    bool succeeded {};
    PreparedStemSet stems;
    StemValidationResult validation;
    juce::String message;
};

class StemPreparation
{
public:
    StemPreparation();

    [[nodiscard]] PreparedStemSetResult loadPreparedStemSet(const juce::File& fullMixFile,
        const juce::File& instrumentalFile,
        const juce::File& drumStemFile,
        const juce::File& bassStemFile,
        const juce::File& vocalStemFile);

private:
    juce::AudioFormatManager formatManager;
};

[[nodiscard]] std::optional<StemAudioBuffer> readStemAudioFile(juce::AudioFormatManager& formatManager,
    const juce::File& file,
    model::StemType stemType);

// TODO(real audio engine): run residual generation during import/analysis/preprocessing or
// background cache generation. Never perform this sample subtraction in the performance path.
[[nodiscard]] StemDerivationResult deriveMusicResidualStem(PreparedStemSet& stems);
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
