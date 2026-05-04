#include "TrackLoader.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <set>

namespace mixdesk::engine
{
namespace
{
constexpr std::array audioExtensions {
    ".aac",
    ".aif",
    ".aiff",
    ".alac",
    ".flac",
    ".m4a",
    ".mp3",
    ".ogg",
    ".opus",
    ".wav",
    ".wma",
};

bool isAudioFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();

    for (const auto* audioExtension : audioExtensions)
        if (extension == audioExtension)
            return true;

    return false;
}

juce::File resolveSiblingFile(const juce::File& metadataFile, const juce::var& fileValue)
{
    if (! fileValue.isString())
        return {};

    const auto fileName = fileValue.toString();
    return fileName.isEmpty() ? juce::File() : metadataFile.getSiblingFile(fileName);
}

juce::File resolveEntryFile(const juce::File& metadataFile,
    const juce::DynamicObject* entriesObject,
    std::initializer_list<const char*> entryNames)
{
    if (entriesObject == nullptr)
        return {};

    for (const auto* entryName : entryNames)
    {
        const auto entry = entriesObject->getProperty(entryName);
        if (auto* entryObject = entry.getDynamicObject())
        {
            const auto file = resolveSiblingFile(metadataFile, entryObject->getProperty("file"));
            if (file.existsAsFile())
                return file;
        }
    }

    return {};
}

juce::String readStringProperty(const juce::DynamicObject* object, const juce::Identifier& propertyName)
{
    if (object == nullptr)
        return {};

    const auto value = object->getProperty(propertyName);
    return value.isString() ? value.toString() : juce::String();
}

double readDoubleProperty(const juce::DynamicObject* object, const juce::Identifier& propertyName)
{
    if (object == nullptr)
        return 0.0;

    const auto value = object->getProperty(propertyName);
    return value.isDouble() || value.isInt() ? static_cast<double>(value) : 0.0;
}

std::optional<model::BeatGrid> readBeatGrid(const juce::DynamicObject* root)
{
    if (root == nullptr)
        return std::nullopt;

    const auto beatGridValue = root->getProperty("beat_grid");
    const auto* beatGridObject = beatGridValue.getDynamicObject();
    if (beatGridObject == nullptr)
        return std::nullopt;

    model::BeatGrid beatGrid;
    beatGrid.tempo = readDoubleProperty(beatGridObject, "tempo");
    beatGrid.bpm = static_cast<int>(std::round(readDoubleProperty(beatGridObject, "bpm")));
    beatGrid.firstBeatOffsetSeconds = readDoubleProperty(beatGridObject, "first_beat_offset_seconds");
    beatGrid.secondsPerBeat = readDoubleProperty(beatGridObject, "seconds_per_beat");
    beatGrid.beatsPerBar = std::max(1, static_cast<int>(std::round(readDoubleProperty(beatGridObject, "beats_per_bar"))));
    beatGrid.durationSeconds = readDoubleProperty(beatGridObject, "duration_seconds");

    const auto beatTimesValue = beatGridObject->getProperty("beat_times_seconds");
    if (auto* beatTimes = beatTimesValue.getArray())
    {
        beatGrid.beatTimesSeconds.reserve(static_cast<std::size_t>(beatTimes->size()));
        for (const auto& beatTime : *beatTimes)
            if (beatTime.isDouble() || beatTime.isInt())
                beatGrid.beatTimesSeconds.push_back(static_cast<double>(beatTime));
    }

    if (beatGrid.tempo <= 0.0 && beatGrid.bpm > 0)
        beatGrid.tempo = static_cast<double>(beatGrid.bpm);

    if (beatGrid.secondsPerBeat <= 0.0 && beatGrid.tempo > 0.0)
        beatGrid.secondsPerBeat = 60.0 / beatGrid.tempo;

    if (beatGrid.bpm <= 0 && beatGrid.tempo > 0.0)
        beatGrid.bpm = static_cast<int>(std::round(beatGrid.tempo));

    if (beatGrid.tempo <= 0.0 || beatGrid.secondsPerBeat <= 0.0)
        return std::nullopt;

    return beatGrid;
}

juce::File pickPrimaryAudioFile(const juce::File& directory, const std::set<juce::String>& knownStemFileNames)
{
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*"))
    {
        if (! isAudioFile(file))
            continue;

        if (! knownStemFileNames.contains(file.getFileName()))
            return file;
    }

    return {};
}

juce::String formatDuration(double durationSeconds)
{
    if (durationSeconds <= 0.0)
        return {};

    const auto totalSeconds = static_cast<int>(std::round(durationSeconds));
    const auto minutes = totalSeconds / 60;
    const auto seconds = totalSeconds % 60;
    return juce::String(minutes) + ":" + juce::String(seconds).paddedLeft('0', 2);
}

juce::String makeDisplayName(const TrackBundle& bundle)
{
    juce::String displayName = bundle.loadedTrack.name;

    if (displayName.isEmpty())
        displayName = bundle.metadataFile.getParentDirectory().getFileName();

    juce::StringArray details;
    if (bundle.loadedTrack.key.empty() == false)
        details.add(bundle.loadedTrack.key);
    if (bundle.metadataBpm > 0.0)
        details.add(juce::String(static_cast<int>(std::round(bundle.metadataBpm))) + " BPM");

    const auto duration = formatDuration(bundle.loadedTrack.durationSeconds);
    if (duration.isNotEmpty())
        details.add(duration);

    if (! details.isEmpty())
        displayName += "  (" + details.joinIntoString(" | ") + ")";

    return displayName;
}
} // namespace

std::optional<TrackBundle> loadTrackBundleFromMixdeskJson(const juce::File& metadataFile)
{
    if (! metadataFile.existsAsFile())
        return std::nullopt;

    auto parsed = juce::JSON::parse(metadataFile);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return std::nullopt;

    const auto entries = root->getProperty("entries");
    auto* entriesObject = entries.getDynamicObject();
    if (entriesObject == nullptr)
        return std::nullopt;

    const auto drumEntry = entriesObject->getProperty("drum");
    auto* drumObject = drumEntry.getDynamicObject();
    const auto drumStemFile = resolveSiblingFile(metadataFile, drumObject == nullptr ? juce::var() : drumObject->getProperty("file"));
    if (! drumStemFile.existsAsFile())
        return std::nullopt;

    const auto instrumentalEntry = entriesObject->getProperty("instrumental");
    auto* instrumentalObject = instrumentalEntry.getDynamicObject();
    auto instrumentalStemFile = resolveEntryFile(metadataFile, entriesObject, { "instrumental", "no_vocals" });
    auto noVocalsStemFile = instrumentalStemFile;

    const auto bassEntry = entriesObject->getProperty("bass");
    auto* bassObject = bassEntry.getDynamicObject();
    const auto bassStemFile = resolveSiblingFile(metadataFile, bassObject == nullptr ? juce::var() : bassObject->getProperty("file"));

    const auto vocalEntry = entriesObject->getProperty("vocals");
    auto* vocalObject = vocalEntry.getDynamicObject();
    const auto vocalStemFile = resolveSiblingFile(metadataFile, vocalObject == nullptr ? juce::var() : vocalObject->getProperty("file"));

    const auto noBassStemFile = resolveEntryFile(metadataFile, entriesObject, { "no_bass", "nobass" });
    const auto noDrumStemFile = resolveEntryFile(metadataFile, entriesObject, { "no_drum", "no_drums", "nodrum", "nodrums" });
    if (! noVocalsStemFile.existsAsFile())
        noVocalsStemFile = resolveEntryFile(metadataFile, entriesObject, { "no_vocals", "no_vocal", "novocals", "novocal" });

    std::set<juce::String> knownStemFileNames;
    for (const auto& stemName : { "drum", "instrumental", "vocals", "bass", "no_bass", "no_drum", "no_vocals" })
    {
        const auto stemEntry = entriesObject->getProperty(stemName);
        if (auto* stemObject = stemEntry.getDynamicObject())
        {
            const auto stemFile = resolveSiblingFile(metadataFile, stemObject->getProperty("file"));
            if (stemFile.existsAsFile())
                knownStemFileNames.insert(stemFile.getFileName());
        }
    }

    auto primaryAudioFile = pickPrimaryAudioFile(metadataFile.getParentDirectory(), knownStemFileNames);
    if (! primaryAudioFile.existsAsFile())
    {
        if (instrumentalObject != nullptr)
            primaryAudioFile = instrumentalStemFile;
    }

    if (! primaryAudioFile.existsAsFile())
        primaryAudioFile = drumStemFile;

    if (! instrumentalStemFile.existsAsFile())
        instrumentalStemFile = primaryAudioFile;

    const auto trackName = readStringProperty(root, "track_name");
    const auto duration = readDoubleProperty(root, "duration");
    const auto metadataBpm = readDoubleProperty(root, "bpm");
    auto metadataBeatGrid = readBeatGrid(root);
    const auto drumKey = readStringProperty(drumObject, "key");

    model::LoadedTrack loadedTrack;
    loadedTrack.name = trackName.isNotEmpty() ? trackName.toStdString() : metadataFile.getParentDirectory().getFileName().toStdString();
    loadedTrack.audioPath = primaryAudioFile.getFullPathName().toStdString();
    loadedTrack.fullMixPath = primaryAudioFile.existsAsFile() ? primaryAudioFile.getFullPathName().toStdString() : std::string();
    loadedTrack.instrumentalStemPath = instrumentalStemFile.existsAsFile() ? instrumentalStemFile.getFullPathName().toStdString() : std::string();
    loadedTrack.drumStemPath = drumStemFile.getFullPathName().toStdString();
    loadedTrack.bassStemPath = bassStemFile.existsAsFile() ? bassStemFile.getFullPathName().toStdString() : std::string();
    loadedTrack.vocalStemPath = vocalStemFile.existsAsFile() ? vocalStemFile.getFullPathName().toStdString() : std::string();
    loadedTrack.noBassStemPath = noBassStemFile.existsAsFile() ? noBassStemFile.getFullPathName().toStdString() : std::string();
    loadedTrack.noDrumStemPath = noDrumStemFile.existsAsFile() ? noDrumStemFile.getFullPathName().toStdString() : std::string();
    loadedTrack.noVocalsStemPath = noVocalsStemFile.existsAsFile() ? noVocalsStemFile.getFullPathName().toStdString() : std::string();
    loadedTrack.stems.fullMixPath = primaryAudioFile.existsAsFile() ? std::optional<std::string>(primaryAudioFile.getFullPathName().toStdString()) : std::nullopt;
    loadedTrack.stems.instrumentalOriginalPath = instrumentalStemFile.existsAsFile() ? std::optional<std::string>(instrumentalStemFile.getFullPathName().toStdString()) : std::nullopt;
    loadedTrack.stems.drumsPath = drumStemFile.existsAsFile() ? std::optional<std::string>(drumStemFile.getFullPathName().toStdString()) : std::nullopt;
    loadedTrack.stems.bassPath = bassStemFile.existsAsFile() ? std::optional<std::string>(bassStemFile.getFullPathName().toStdString()) : std::nullopt;
    loadedTrack.stems.vocalsPath = vocalStemFile.existsAsFile() ? std::optional<std::string>(vocalStemFile.getFullPathName().toStdString()) : std::nullopt;
    loadedTrack.stems.noBassPath = noBassStemFile.existsAsFile() ? std::optional<std::string>(noBassStemFile.getFullPathName().toStdString()) : std::nullopt;
    loadedTrack.stems.noDrumsPath = noDrumStemFile.existsAsFile() ? std::optional<std::string>(noDrumStemFile.getFullPathName().toStdString()) : std::nullopt;
    loadedTrack.stems.noVocalsPath = noVocalsStemFile.existsAsFile() ? std::optional<std::string>(noVocalsStemFile.getFullPathName().toStdString()) : std::nullopt;
    loadedTrack.key = drumKey.toStdString();
    loadedTrack.durationSeconds = duration;

    return TrackBundle { loadedTrack,
        metadataFile,
        primaryAudioFile,
        instrumentalStemFile,
        drumStemFile,
        bassStemFile,
        vocalStemFile,
        noBassStemFile,
        noDrumStemFile,
        noVocalsStemFile,
        metadataBpm,
        std::move(metadataBeatGrid) };
}

std::optional<juce::File> findFirstMixdeskJson(const juce::File& rootDirectory)
{
    if (! rootDirectory.isDirectory())
        return std::nullopt;

    const auto matches = rootDirectory.findChildFiles(juce::File::findFiles, true, "mixdesk.json");
    if (! matches.isEmpty())
        return matches.getFirst();

    return std::nullopt;
}

std::vector<TrackCatalogEntry> findTrackCatalog(const juce::File& rootDirectory)
{
    std::vector<TrackCatalogEntry> catalog;
    if (! rootDirectory.isDirectory())
        return catalog;

    const auto matches = rootDirectory.findChildFiles(juce::File::findFiles, true, "mixdesk.json");
    catalog.reserve(static_cast<std::size_t>(matches.size()));

    for (const auto& metadataFile : matches)
    {
        auto bundle = loadTrackBundleFromMixdeskJson(metadataFile);
        if (! bundle.has_value())
            continue;

        catalog.push_back({
            metadataFile,
            makeDisplayName(*bundle),
            juce::String(bundle->loadedTrack.name),
            juce::String(bundle->loadedTrack.key),
            bundle->metadataBpm,
            bundle->loadedTrack.durationSeconds
        });
    }

    std::sort(catalog.begin(), catalog.end(),
        [](const TrackCatalogEntry& a, const TrackCatalogEntry& b)
        {
            return a.displayName.compareIgnoreCase(b.displayName) < 0;
        });

    return catalog;
}
} // namespace mixdesk::engine
