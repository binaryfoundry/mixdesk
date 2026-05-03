#pragma once

#include "Model/PhraseModel.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace mixdesk::engine
{
struct TrackBundle
{
    model::LoadedTrack loadedTrack;
    juce::File metadataFile;
    juce::File primaryAudioFile;
    juce::File instrumentalStemFile;
    juce::File drumStemFile;
    juce::File bassStemFile;
    juce::File vocalStemFile;
    double metadataBpm {};
};

[[nodiscard]] std::optional<TrackBundle> loadTrackBundleFromMixdeskJson(const juce::File& metadataFile);
[[nodiscard]] std::optional<juce::File> findFirstMixdeskJson(const juce::File& rootDirectory);
} // namespace mixdesk::engine
