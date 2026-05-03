#pragma once

#include "Model/PhraseModel.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace mixdesk::engine
{
struct TempoSettings
{
    double minTempo { 90.0 };
    double maxTempo { 180.0 };
};

struct BeatDetectionResult
{
    bool succeeded {};
    model::BeatGrid beatGrid;
    juce::String message;
};

class BeatDetector
{
public:
    BeatDetector();

    [[nodiscard]] BeatDetectionResult analyzeBuffer(const juce::AudioBuffer<float>& audio,
        double sampleRate,
        TempoSettings tempoSettings = {},
        double durationSeconds = 0.0);

    [[nodiscard]] BeatDetectionResult analyzeDrumStem(const juce::File& drumStemFile,
        TempoSettings tempoSettings = {});

private:
    juce::AudioFormatManager formatManager;
};
} // namespace mixdesk::engine
