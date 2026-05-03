#pragma once

#include "Model/PhraseModel.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace mixdesk::engine
{
class WaveformAnalyzer
{
public:
    WaveformAnalyzer();

    [[nodiscard]] model::StemWaveform analyzeStem(const juce::File& stemFile,
        model::StemType stemType,
        double pointsPerSecond = 40.0);

private:
    juce::AudioFormatManager formatManager;
};
} // namespace mixdesk::engine
