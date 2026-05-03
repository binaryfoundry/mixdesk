#include "TrackLoader.h"

#include <array>
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

    const auto bassEntry = entriesObject->getProperty("bass");
    auto* bassObject = bassEntry.getDynamicObject();
    const auto bassStemFile = resolveSiblingFile(metadataFile, bassObject == nullptr ? juce::var() : bassObject->getProperty("file"));

    const auto vocalEntry = entriesObject->getProperty("vocals");
    auto* vocalObject = vocalEntry.getDynamicObject();
    const auto vocalStemFile = resolveSiblingFile(metadataFile, vocalObject == nullptr ? juce::var() : vocalObject->getProperty("file"));

    std::set<juce::String> knownStemFileNames;
    for (const auto& stemName : { "drum", "instrumental", "vocals", "bass" })
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
        const auto instrumentalEntry = entriesObject->getProperty("instrumental");
        if (auto* instrumentalObject = instrumentalEntry.getDynamicObject())
            primaryAudioFile = resolveSiblingFile(metadataFile, instrumentalObject->getProperty("file"));
    }

    if (! primaryAudioFile.existsAsFile())
        primaryAudioFile = drumStemFile;

    const auto trackName = readStringProperty(root, "track_name");
    const auto duration = readDoubleProperty(root, "duration");
    const auto metadataBpm = readDoubleProperty(root, "bpm");
    const auto drumKey = readStringProperty(drumObject, "key");

    model::LoadedTrack loadedTrack;
    loadedTrack.name = trackName.isNotEmpty() ? trackName.toStdString() : metadataFile.getParentDirectory().getFileName().toStdString();
    loadedTrack.audioPath = primaryAudioFile.getFullPathName().toStdString();
    loadedTrack.drumStemPath = drumStemFile.getFullPathName().toStdString();
    loadedTrack.bassStemPath = bassStemFile.existsAsFile() ? bassStemFile.getFullPathName().toStdString() : std::string();
    loadedTrack.vocalStemPath = vocalStemFile.existsAsFile() ? vocalStemFile.getFullPathName().toStdString() : std::string();
    loadedTrack.key = drumKey.toStdString();
    loadedTrack.durationSeconds = duration;

    return TrackBundle { loadedTrack, metadataFile, primaryAudioFile, drumStemFile, bassStemFile, vocalStemFile, metadataBpm };
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
} // namespace mixdesk::engine
