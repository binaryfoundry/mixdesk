#pragma once

#include "Model/PhraseModel.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

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
    juce::File noBassStemFile;
    juce::File noDrumStemFile;
    juce::File noVocalsStemFile;
    double metadataBpm {};
    std::optional<model::BeatGrid> metadataBeatGrid;
};

struct TrackCatalogEntry
{
    juce::File metadataFile;
    juce::String displayName;
    juce::String trackName;
    juce::String key;
    double bpm {};
    double durationSeconds {};
};

[[nodiscard]] std::optional<TrackBundle> loadTrackBundleFromMixdeskJson(const juce::File& metadataFile);
[[nodiscard]] std::optional<juce::File> findFirstMixdeskJson(const juce::File& rootDirectory);
[[nodiscard]] std::vector<TrackCatalogEntry> findTrackCatalog(const juce::File& rootDirectory);
} // namespace mixdesk::engine
