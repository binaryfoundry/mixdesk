#pragma once

#include "Model/PhraseModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <unordered_map>

namespace mixdesk::ui
{
class PhraseWorkspace final : public juce::Component
{
public:
    using PhraseMoveCallback = std::function<void(model::DeckId, std::size_t, int)>;
    using LaunchNudgeCallback = std::function<void(model::DeckId, int)>;
    using TrackMoveCallback = std::function<void(model::DeckId, int)>;
    using RoleChangeCallback = std::function<void(model::DeckId, model::DeckRole)>;
    using PlaybackToggleCallback = std::function<void()>;
    using StemToggleCallback = std::function<void(model::DeckId, model::StemType, bool)>;
    using MasterVolumeCallback = std::function<void(float)>;
    using BpmChangeCallback = std::function<void(double)>;

    PhraseWorkspace();

    void setStateSnapshot(model::WorkspaceState newState);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;

    PhraseMoveCallback onPhraseBlockMoved;
    LaunchNudgeCallback onLaunchOffsetNudged;
    TrackMoveCallback onTrackLaunchOffsetMoved;
    RoleChangeCallback onRoleChangeRequested;
    PlaybackToggleCallback onPlaybackToggleRequested;
    StemToggleCallback onStemToggleRequested;
    MasterVolumeCallback onMasterVolumeChanged;
    BpmChangeCallback onBpmChanged;

private:
    enum class SnapMode
    {
        Bar,
        Phrase
    };

    struct HitBlock
    {
        std::size_t deckIndex {};
        std::size_t blockIndex {};
        model::DeckId deckId {};
    };

    struct HitTrack
    {
        std::size_t deckIndex {};
        model::DeckId deckId {};
    };

    struct HitStemToggle
    {
        std::size_t deckIndex {};
        model::DeckId deckId {};
        model::StemType stemType { model::StemType::Drums };
    };

    struct ActiveDrag
    {
        int sourceIndex {};
        std::size_t deckIndex {};
        std::size_t blockIndex {};
        model::DeckId deckId {};
        float dragStartX {};
        int originalStartBar {};
        int previewStartBar {};
    };

    struct ActiveTrackDrag
    {
        int sourceIndex {};
        std::size_t deckIndex {};
        model::DeckId deckId {};
        float dragStartX {};
        int originalLaunchOffsetBars {};
        int previewLaunchOffsetBars {};
    };

    struct PointerContact
    {
        juce::Point<float> downPosition;
        juce::Point<float> currentPosition;
        bool isTouch {};
    };

    struct ConflictFlags
    {
        bool bass {};
        bool vocal {};
    };

    juce::Rectangle<float> getHeaderBounds() const;
    juce::Rectangle<float> getTransportButtonBounds() const;
    juce::Rectangle<float> getBpmBounds() const;
    juce::Rectangle<float> getBpmTrackBounds() const;
    juce::Rectangle<float> getMasterVolumeBounds() const;
    juce::Rectangle<float> getMasterVolumeTrackBounds() const;
    juce::Rectangle<float> getTimelineBounds() const;
    juce::Rectangle<float> getGridBounds() const;
    juce::Rectangle<float> getControlBounds() const;
    juce::Rectangle<float> getDeckLabelBounds(std::size_t deckIndex) const;
    juce::Rectangle<float> getStemToggleBounds(std::size_t deckIndex, model::StemType stemType) const;
    juce::Rectangle<float> getLaneBounds(std::size_t deckIndex) const;
    juce::Rectangle<float> getBlockBounds(std::size_t deckIndex, std::size_t blockIndex) const;
    juce::Rectangle<float> getLoadedTrackBounds(std::size_t deckIndex) const;
    juce::Rectangle<float> getLoadedTrackTitleBounds(std::size_t deckIndex) const;
    juce::Rectangle<float> getLoadedTrackWaveformBounds(std::size_t deckIndex) const;

    float xForBar(double bar) const;
    double barForX(float x) const;
    double beatTimeToBar(const model::DeckTimeline& deck, double beatTimeSeconds) const;
    double getTrackLengthBars(const model::DeckTimeline& deck) const;
    double visibleEndBar() const;

    void drawHeader(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawLanes(juce::Graphics& g);
    void drawLoadedTrackBeds(juce::Graphics& g);
    void drawStemWaveforms(juce::Graphics& g, const model::DeckTimeline& deck, juce::Rectangle<float> bed);
    void drawBeatMarkers(juce::Graphics& g);
    void drawPhraseMarkers(juce::Graphics& g);
    void drawPhraseBlocks(juce::Graphics& g);
    void drawPlayhead(juce::Graphics& g);
    void drawControlRail(juce::Graphics& g);

    std::optional<HitBlock> hitTestBlock(juce::Point<float> position) const;
    std::optional<HitTrack> hitTestLoadedTrack(juce::Point<float> position) const;
    std::optional<HitStemToggle> hitTestStemToggle(juce::Point<float> position) const;
    std::optional<model::DeckId> hitTestDeck(juce::Point<float> position) const;

    int snapStartBar(double rawStartBar) const;
    void setSnapMode(SnapMode newMode);
    void nudgeSelectedLaunchOffset(int deltaBars);
    void cycleSelectedRole();
    void updateButtonText();

    ConflictFlags detectConflictForBlock(std::size_t deckIndex, std::size_t blockIndex) const;
    float volumeForX(float x) const;
    void setMasterVolumeFromPoint(juce::Point<float> position);
    double bpmForX(float x) const;
    void setBpmFromPoint(juce::Point<float> position);
    bool isWorkspacePlaying() const;
    void zoomAround(float componentX, float scaleFactor);
    void updatePinchZoomFromPointers();

    model::WorkspaceState state;
    model::DeckId selectedDeck { model::DeckId::B };
    std::optional<std::size_t> selectedBlockIndex;
    std::optional<ActiveDrag> activeDrag;
    std::optional<ActiveTrackDrag> activeTrackDrag;
    std::optional<int> activeBpmDragSource;
    std::optional<int> activeVolumeDragSource;
    std::unordered_map<int, PointerContact> activePointers;

    SnapMode snapMode { SnapMode::Bar };
    double viewStartBar { -4.0 };
    float pixelsPerBar { 28.0f };

    juce::TextButton launch8Button { "Launch +8" };
    juce::TextButton launch16Button { "Launch +16" };
    juce::TextButton launch32Button { "Launch +32" };
    juce::TextButton snapButton { "Snap: Bar" };
    juce::TextButton roleButton { "Role" };

    static constexpr float minPixelsPerBar = 12.0f;
    static constexpr float maxPixelsPerBar = 72.0f;
    static constexpr int headerHeight = 64;
    static constexpr int controlHeight = 0;
    static constexpr float outerPadding = 18.0f;
    static constexpr float leftLabelWidth = 270.0f;
};
} // namespace mixdesk::ui
