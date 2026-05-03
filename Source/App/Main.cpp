#include "MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace mixdesk::app
{
class MixdeskApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Mixdesk Phrase Workspace"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(std::move(name), juce::Colour(0xff101318), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setResizeLimits(1000, 620, 3840, 2160);
            setContentOwned(new MainComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};
} // namespace mixdesk::app

START_JUCE_APPLICATION(mixdesk::app::MixdeskApplication)
