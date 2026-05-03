#pragma once

#include <juce_core/juce_core.h>

#include <map>

namespace mixdesk::app
{
class AppSettings
{
public:
    AppSettings();

    [[nodiscard]] juce::File getTracksRoot() const;
    void setTracksRoot(const juce::File& root);
    [[nodiscard]] juce::File getSettingsFile() const;

private:
    juce::File settingsFile;
    std::map<juce::String, juce::String> values;
};
} // namespace mixdesk::app
