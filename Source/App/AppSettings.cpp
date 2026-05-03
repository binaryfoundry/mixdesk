#include "AppSettings.h"

namespace mixdesk::app
{
namespace
{
constexpr auto tracksRootKey = "tracksRoot";
const juce::File defaultTracksRoot { "D:\\tracks" };

juce::File defaultSettingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Mixdesk")
        .getChildFile("mixdesk.ini");
}

std::map<juce::String, juce::String> readIniValues(const juce::File& file)
{
    std::map<juce::String, juce::String> values;

    if (! file.existsAsFile())
        return values;

    const auto lines = juce::StringArray::fromLines(file.loadFileAsString());
    for (const auto& rawLine : lines)
    {
        const auto line = rawLine.trim();
        if (line.isEmpty() || line.startsWithChar('#') || line.startsWithChar(';'))
            continue;

        const auto separator = line.indexOfChar('=');
        if (separator <= 0)
            continue;

        const auto key = line.substring(0, separator).trim();
        const auto value = line.substring(separator + 1).trim().unquoted();
        if (key.isNotEmpty())
            values[key] = value;
    }

    return values;
}

void writeIniValues(const juce::File& file, const std::map<juce::String, juce::String>& values)
{
    file.getParentDirectory().createDirectory();

    juce::String text;
    text << "# Mixdesk settings\n";
    for (const auto& [key, value] : values)
        text << key << "=" << value << "\n";

    file.replaceWithText(text);
}
} // namespace

AppSettings::AppSettings()
    : settingsFile(defaultSettingsFile()),
      values(readIniValues(settingsFile))
{
    if (! values.contains(tracksRootKey))
    {
        values[tracksRootKey] = defaultTracksRoot.getFullPathName();
        writeIniValues(settingsFile, values);
    }
}

juce::File AppSettings::getTracksRoot() const
{
    const auto iter = values.find(tracksRootKey);
    return juce::File(iter == values.end() ? defaultTracksRoot.getFullPathName() : iter->second);
}

void AppSettings::setTracksRoot(const juce::File& root)
{
    values[tracksRootKey] = root.getFullPathName();
    writeIniValues(settingsFile, values);
}

juce::File AppSettings::getSettingsFile() const
{
    return settingsFile;
}
} // namespace mixdesk::app
