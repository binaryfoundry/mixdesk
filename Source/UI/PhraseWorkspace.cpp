#include "PhraseWorkspace.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mixdesk::ui
{
namespace
{
constexpr auto laneGap = 10.0f;
constexpr auto loadedTrackTitleHeight = 34.0f;
constexpr auto loadedTrackTitleGap = 6.0f;
constexpr auto minGlobalBpm = 80.0;
constexpr auto maxGlobalBpm = 180.0;
constexpr std::array stemToggleOrder { model::StemType::Drums, model::StemType::Bass, model::StemType::MusicResidual, model::StemType::Vocals };

juce::Colour backgroundColour() { return juce::Colour(0xff101318); }
juce::Colour panelColour() { return juce::Colour(0xff171c22); }
juce::Colour laneColour() { return juce::Colour(0xff1d242b); }
juce::Colour laneSelectedColour() { return juce::Colour(0xff24313a); }
juce::Colour textColour() { return juce::Colour(0xffeef3f5); }
juce::Colour mutedTextColour() { return juce::Colour(0xff9aa8ad); }
juce::Colour gridLineColour() { return juce::Colour(0xff313941); }
juce::Colour strongGridLineColour() { return juce::Colour(0xff5d6972); }
juce::Colour phraseBoundaryColour() { return juce::Colour(0xff8da0aa); }
juce::Colour drumWaveformColour() { return juce::Colour(0xff35c4ff); }
juce::Colour bassWaveformColour() { return juce::Colour(0xffffc247); }
juce::Colour musicWaveformColour() { return juce::Colour(0xff7fd86b); }
juce::Colour vocalWaveformColour() { return juce::Colour(0xffff6fb2); }

bool isMultipleOf(int value, int interval) noexcept
{
    if (interval <= 0)
        return false;

    return value % interval == 0;
}

double effectiveStartBar(const model::DeckTimeline& deck, const model::PhraseBlock& block) noexcept
{
    return static_cast<double>(deck.launchOffsetBars + block.startBar);
}

double effectiveEndBar(const model::DeckTimeline& deck, const model::PhraseBlock& block) noexcept
{
    return effectiveStartBar(deck, block) + static_cast<double>(std::max(1, block.lengthBars));
}

bool rangesOverlap(double startA, double endA, double startB, double endB) noexcept
{
    return std::max(startA, startB) < std::min(endA, endB);
}

bool deckIsActive(const model::DeckTimeline& deck) noexcept
{
    return deck.volume > 0.02f;
}

bool activeBass(const model::DeckTimeline& deck, const model::PhraseBlock& block) noexcept
{
    return deckIsActive(deck)
        && block.hasBass
        && model::isStemEnabled(deck.stemEnabled, model::StemType::Bass)
        && ! deck.lowCutEnabled;
}

bool activeVocal(const model::DeckTimeline& deck, const model::PhraseBlock& block) noexcept
{
    return deckIsActive(deck)
        && block.hasVocal
        && model::isStemEnabled(deck.stemEnabled, model::StemType::Vocals);
}

juce::Colour phraseColour(model::PhraseType type)
{
    switch (type)
    {
        case model::PhraseType::Intro: return juce::Colour(0xff2a9d8f);
        case model::PhraseType::Groove: return juce::Colour(0xff63b85c);
        case model::PhraseType::Breakdown: return juce::Colour(0xff6d8fd8);
        case model::PhraseType::Build: return juce::Colour(0xffe0a92f);
        case model::PhraseType::Drop: return juce::Colour(0xffe85d4f);
        case model::PhraseType::Outro: return juce::Colour(0xff8f9aa3);
        case model::PhraseType::Loop: return juce::Colour(0xff2cb7d0);
        case model::PhraseType::FX: return juce::Colour(0xffc46ad8);
    }

    return juce::Colours::white;
}

juce::Colour stemColour(model::StemType type)
{
    switch (type)
    {
        case model::StemType::FullMix: return textColour();
        case model::StemType::Drums: return drumWaveformColour();
        case model::StemType::Bass: return bassWaveformColour();
        case model::StemType::Vocals: return vocalWaveformColour();
        case model::StemType::InstrumentalOriginal: return mutedTextColour();
        case model::StemType::MusicResidual: return musicWaveformColour();
    }

    return juce::Colours::white;
}

int stemToggleIndex(model::StemType stemType)
{
    for (auto index = 0; index < static_cast<int>(stemToggleOrder.size()); ++index)
        if (stemToggleOrder[static_cast<std::size_t>(index)] == stemType)
            return index;

    return 0;
}

juce::String stemShortLabel(model::StemType stemType)
{
    switch (stemType)
    {
        case model::StemType::FullMix: return "F";
        case model::StemType::Drums: return "D";
        case model::StemType::Bass: return "B";
        case model::StemType::Vocals: return "V";
        case model::StemType::InstrumentalOriginal: return "I";
        case model::StemType::MusicResidual: return "M";
    }

    return "?";
}

juce::String fixedOneDecimal(double value)
{
    return juce::String(value, 1);
}

juce::String formatDuration(double durationSeconds)
{
    if (durationSeconds <= 0.0)
        return "--:--";

    const auto totalSeconds = static_cast<int>(std::round(durationSeconds));
    const auto minutes = totalSeconds / 60;
    const auto seconds = totalSeconds % 60;

    return juce::String(minutes) + ":" + juce::String(seconds).paddedLeft('0', 2);
}

juce::String trackBpmText(const model::DeckTimeline& deck, double fallbackBpm)
{
    if (deck.beatGrid.has_value() && deck.beatGrid->bpm > 0)
        return juce::String(deck.beatGrid->bpm);

    if (fallbackBpm > 0.0)
        return juce::String(static_cast<int>(std::round(fallbackBpm)));

    return "--";
}

double trackDurationSeconds(const model::DeckTimeline& deck)
{
    if (deck.loadedTrack.has_value() && deck.loadedTrack->durationSeconds > 0.0)
        return deck.loadedTrack->durationSeconds;

    if (deck.beatGrid.has_value() && deck.beatGrid->durationSeconds > 0.0)
        return deck.beatGrid->durationSeconds;

    return 0.0;
}

juce::String asJuceString(std::string_view text)
{
    return juce::String(text.data(), static_cast<int>(text.size()));
}

juce::Font makeFont(float height, int styleFlags = juce::Font::plain)
{
    return juce::Font(juce::FontOptions(height, styleFlags));
}
} // namespace

PhraseWorkspace::PhraseWorkspace()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);

    launch8Button.onClick = [this] { nudgeSelectedLaunchOffset(8); };
    launch16Button.onClick = [this] { nudgeSelectedLaunchOffset(16); };
    launch32Button.onClick = [this] { nudgeSelectedLaunchOffset(32); };
    // TODO(MIDI/HID controller support): expose these commands through a controller mapping layer.
    snapButton.onClick = [this]
    {
        setSnapMode(snapMode == SnapMode::Bar ? SnapMode::Phrase : SnapMode::Bar);
    };
    roleButton.onClick = [this] { cycleSelectedRole(); };

    launch8Button.setTooltip("Move the selected deck launch by 8 bars");
    launch16Button.setTooltip("Move the selected deck launch by 16 bars");
    launch32Button.setTooltip("Move the selected deck launch by 32 bars");
    snapButton.setTooltip("Toggle bar or phrase snapping for block drags");
    roleButton.setTooltip("Cycle the selected deck role");

    updateButtonText();
}

void PhraseWorkspace::setStateSnapshot(model::WorkspaceState newState)
{
    state = std::move(newState);
    updateButtonText();
    repaint();
}

void PhraseWorkspace::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour());
    drawHeader(g);
    drawGrid(g);
    drawLanes(g);
    drawLoadedTrackBeds(g);
    drawPhraseBlocks(g);
    drawPhraseMarkers(g);
    drawBeatMarkers(g);
    drawPlayhead(g);
}

void PhraseWorkspace::resized()
{
}

void PhraseWorkspace::mouseDown(const juce::MouseEvent& event)
{
    const auto sourceIndex = event.source.getIndex();
    activePointers.insert_or_assign(sourceIndex, PointerContact { event.position, event.position, event.source.isTouch() });

    if (getTransportButtonBounds().contains(event.position))
    {
        selectedBlockIndex.reset();
        activeDrag.reset();
        activeTrackDrag.reset();

        if (onPlaybackToggleRequested)
            onPlaybackToggleRequested();

        repaint();
        return;
    }

    if (getMasterVolumeBounds().contains(event.position))
    {
        selectedBlockIndex.reset();
        activeDrag.reset();
        activeTrackDrag.reset();
        activeVolumeDragSource = sourceIndex;
        setMasterVolumeFromPoint(event.position);
        repaint();
        return;
    }

    if (getBpmBounds().contains(event.position))
    {
        selectedBlockIndex.reset();
        activeDrag.reset();
        activeTrackDrag.reset();
        activeBpmDragSource = sourceIndex;
        setBpmFromPoint(event.position);
        repaint();
        return;
    }

    if (const auto hit = hitTestStemToggle(event.position))
    {
        selectedDeck = hit->deckId;
        selectedBlockIndex.reset();
        activeDrag.reset();
        activeTrackDrag.reset();

        if (hit->deckIndex < state.decks.size())
        {
            auto& deck = state.decks[hit->deckIndex];
            const auto enabled = ! model::isStemEnabled(deck.stemEnabled, hit->stemType);
            model::setStemEnabled(deck.stemEnabled, hit->stemType, enabled);

            if (onStemToggleRequested)
                onStemToggleRequested(hit->deckId, hit->stemType, enabled);
        }

        repaint();
        return;
    }

    if (const auto hit = hitTestBlock(event.position))
    {
        selectedDeck = hit->deckId;
        selectedBlockIndex = hit->blockIndex;
        activeTrackDrag.reset();

        const auto& block = state.decks[hit->deckIndex].blocks[hit->blockIndex];
        activeDrag = ActiveDrag {
            sourceIndex,
            hit->deckIndex,
            hit->blockIndex,
            hit->deckId,
            event.position.x,
            block.startBar,
            block.startBar
        };

        repaint();
        return;
    }

    if (const auto hit = hitTestLoadedTrack(event.position))
    {
        selectedDeck = hit->deckId;
        selectedBlockIndex.reset();
        activeDrag.reset();

        const auto& deck = state.decks[hit->deckIndex];
        activeTrackDrag = ActiveTrackDrag {
            sourceIndex,
            hit->deckIndex,
            hit->deckId,
            event.position.x,
            deck.launchOffsetBars,
            deck.launchOffsetBars
        };

        repaint();
        return;
    }

    if (const auto deckId = hitTestDeck(event.position))
    {
        selectedDeck = *deckId;
        selectedBlockIndex.reset();
        activeDrag.reset();
        activeTrackDrag.reset();
        repaint();
    }
}

void PhraseWorkspace::mouseDrag(const juce::MouseEvent& event)
{
    const auto sourceIndex = event.source.getIndex();
    if (auto iter = activePointers.find(sourceIndex); iter != activePointers.end())
        iter->second.currentPosition = event.position;

    if (activeDrag.has_value() && activeDrag->sourceIndex == sourceIndex)
    {
        auto& drag = *activeDrag;
        if (drag.deckIndex < state.decks.size() && drag.blockIndex < state.decks[drag.deckIndex].blocks.size())
        {
            const auto deltaBars = static_cast<double>(event.position.x - drag.dragStartX) / static_cast<double>(pixelsPerBar);
            const auto snappedStart = snapStartBar(static_cast<double>(drag.originalStartBar) + deltaBars);

            if (snappedStart != drag.previewStartBar)
            {
                drag.previewStartBar = snappedStart;
                state.decks[drag.deckIndex].blocks[drag.blockIndex].startBar = snappedStart;
                repaint();
            }
        }
    }

    if (activeTrackDrag.has_value() && activeTrackDrag->sourceIndex == sourceIndex)
    {
        auto& drag = *activeTrackDrag;
        if (drag.deckIndex < state.decks.size())
        {
            const auto deltaBars = static_cast<double>(event.position.x - drag.dragStartX) / static_cast<double>(pixelsPerBar);
            const auto snappedOffset = snapStartBar(static_cast<double>(drag.originalLaunchOffsetBars) + deltaBars);

            if (snappedOffset != drag.previewLaunchOffsetBars)
            {
                drag.previewLaunchOffsetBars = snappedOffset;
                state.decks[drag.deckIndex].launchOffsetBars = snappedOffset;

                if (onTrackLaunchOffsetMoved)
                    onTrackLaunchOffsetMoved(drag.deckId, snappedOffset);

                repaint();
            }
        }
    }

    if (activeVolumeDragSource.has_value() && *activeVolumeDragSource == sourceIndex)
    {
        setMasterVolumeFromPoint(event.position);
        repaint();
    }

    if (activeBpmDragSource.has_value() && *activeBpmDragSource == sourceIndex)
    {
        setBpmFromPoint(event.position);
        repaint();
    }

    updatePinchZoomFromPointers();
}

void PhraseWorkspace::mouseUp(const juce::MouseEvent& event)
{
    const auto sourceIndex = event.source.getIndex();

    if (activeDrag.has_value() && activeDrag->sourceIndex == sourceIndex)
    {
        const auto drag = *activeDrag;
        if (drag.deckIndex < state.decks.size() && drag.blockIndex < state.decks[drag.deckIndex].blocks.size())
        {
            const auto finalStartBar = state.decks[drag.deckIndex].blocks[drag.blockIndex].startBar;
            if (onPhraseBlockMoved)
                onPhraseBlockMoved(drag.deckId, drag.blockIndex, finalStartBar);
        }

        activeDrag.reset();
    }

    if (activeTrackDrag.has_value() && activeTrackDrag->sourceIndex == sourceIndex)
        activeTrackDrag.reset();

    if (activeVolumeDragSource.has_value() && *activeVolumeDragSource == sourceIndex)
        activeVolumeDragSource.reset();

    if (activeBpmDragSource.has_value() && *activeBpmDragSource == sourceIndex)
        activeBpmDragSource.reset();

    activePointers.erase(sourceIndex);
}

void PhraseWorkspace::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
        zoomAround(event.position.x, 1.0f + (wheel.deltaY * 0.8f));
        return;
    }

    const auto horizontalDelta = std::abs(wheel.deltaX) > 0.001f ? wheel.deltaX : wheel.deltaY;
    viewStartBar -= static_cast<double>(horizontalDelta) * 10.0;
    repaint();
}

void PhraseWorkspace::mouseMagnify(const juce::MouseEvent& event, float scaleFactor)
{
    zoomAround(event.position.x, scaleFactor);
}

juce::Rectangle<float> PhraseWorkspace::getHeaderBounds() const
{
    return getLocalBounds().removeFromTop(headerHeight).toFloat();
}

juce::Rectangle<float> PhraseWorkspace::getTransportButtonBounds() const
{
    const auto header = getHeaderBounds();
    const auto size = 42.0f;
    return { header.getX() + outerPadding, header.getCentreY() - (size * 0.5f), size, size };
}

juce::Rectangle<float> PhraseWorkspace::getBpmBounds() const
{
    const auto header = getHeaderBounds();
    return { header.getRight() - outerPadding - 452.0f, header.getY() + 11.0f, 220.0f, header.getHeight() - 22.0f };
}

juce::Rectangle<float> PhraseWorkspace::getBpmTrackBounds() const
{
    return getBpmBounds().reduced(14.0f, 0.0f).withTrimmedTop(24.0f).withHeight(10.0f);
}

juce::Rectangle<float> PhraseWorkspace::getMasterVolumeBounds() const
{
    const auto header = getHeaderBounds();
    return { header.getRight() - outerPadding - 220.0f, header.getY() + 11.0f, 220.0f, header.getHeight() - 22.0f };
}

juce::Rectangle<float> PhraseWorkspace::getMasterVolumeTrackBounds() const
{
    return getMasterVolumeBounds().reduced(14.0f, 0.0f).withTrimmedTop(24.0f).withHeight(10.0f);
}

juce::Rectangle<float> PhraseWorkspace::getTimelineBounds() const
{
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromTop(static_cast<float>(headerHeight));
    bounds.removeFromBottom(static_cast<float>(controlHeight));
    return bounds.reduced(outerPadding, 10.0f);
}

juce::Rectangle<float> PhraseWorkspace::getGridBounds() const
{
    auto timeline = getTimelineBounds();
    timeline.removeFromLeft(leftLabelWidth);
    return timeline;
}

juce::Rectangle<float> PhraseWorkspace::getControlBounds() const
{
    return getLocalBounds().removeFromBottom(controlHeight).toFloat();
}

juce::Rectangle<float> PhraseWorkspace::getDeckLabelBounds(std::size_t deckIndex) const
{
    auto timeline = getTimelineBounds();
    auto labelArea = timeline.removeFromLeft(leftLabelWidth);
    const auto lane = getLaneBounds(deckIndex);

    return { labelArea.getX(), lane.getY(), labelArea.getWidth() - 10.0f, lane.getHeight() };
}

juce::Rectangle<float> PhraseWorkspace::getStemToggleBounds(std::size_t deckIndex, model::StemType stemType) const
{
    auto bounds = getDeckLabelBounds(deckIndex).reduced(14.0f, 12.0f);
    const auto buttonWidth = 54.0f;
    const auto buttonHeight = 30.0f;
    const auto gap = 8.0f;
    const auto index = stemToggleIndex(stemType);

    return { bounds.getX() + (static_cast<float>(index) * (buttonWidth + gap)),
        bounds.getBottom() - buttonHeight,
        buttonWidth,
        buttonHeight };
}

juce::Rectangle<float> PhraseWorkspace::getLaneBounds(std::size_t deckIndex) const
{
    auto timeline = getTimelineBounds();
    timeline.removeFromLeft(leftLabelWidth);

    const auto laneCount = std::max<std::size_t>(1, state.decks.size());
    const auto totalGap = laneGap * static_cast<float>(laneCount - 1);
    const auto laneHeight = (timeline.getHeight() - totalGap) / static_cast<float>(laneCount);
    const auto y = timeline.getY() + (static_cast<float>(deckIndex) * (laneHeight + laneGap));

    return { timeline.getX(), y, timeline.getWidth(), laneHeight };
}

juce::Rectangle<float> PhraseWorkspace::getBlockBounds(std::size_t deckIndex, std::size_t blockIndex) const
{
    if (deckIndex >= state.decks.size() || blockIndex >= state.decks[deckIndex].blocks.size())
        return {};

    const auto& deck = state.decks[deckIndex];
    const auto& block = deck.blocks[blockIndex];
    auto lane = getLaneBounds(deckIndex).reduced(0.0f, 12.0f);

    if (deck.loadedTrack.has_value())
    {
        lane = getLoadedTrackBounds(deckIndex).reduced(8.0f, 6.0f);
        lane = lane.removeFromBottom(30.0f);
    }

    const auto x = xForBar(effectiveStartBar(deck, block));
    const auto width = static_cast<float>(std::max(1, block.lengthBars)) * pixelsPerBar;

    return { x, lane.getY(), width, lane.getHeight() };
}

juce::Rectangle<float> PhraseWorkspace::getLoadedTrackBounds(std::size_t deckIndex) const
{
    if (deckIndex >= state.decks.size())
        return {};

    const auto& deck = state.decks[deckIndex];
    if (! deck.loadedTrack.has_value())
        return {};

    const auto lane = getLaneBounds(deckIndex).reduced(4.0f, 14.0f);
    const auto x = xForBar(static_cast<double>(deck.launchOffsetBars));
    const auto width = static_cast<float>(getTrackLengthBars(deck)) * pixelsPerBar;

    return { x, lane.getY(), width, lane.getHeight() };
}

juce::Rectangle<float> PhraseWorkspace::getLoadedTrackTitleBounds(std::size_t deckIndex) const
{
    auto content = getLoadedTrackBounds(deckIndex).reduced(8.0f, 6.0f);
    return content.removeFromTop(loadedTrackTitleHeight);
}

juce::Rectangle<float> PhraseWorkspace::getLoadedTrackWaveformBounds(std::size_t deckIndex) const
{
    auto content = getLoadedTrackBounds(deckIndex).reduced(8.0f, 6.0f);
    content.removeFromTop(loadedTrackTitleHeight + loadedTrackTitleGap);
    return content;
}

float PhraseWorkspace::xForBar(double bar) const
{
    const auto grid = getGridBounds();
    return grid.getX() + static_cast<float>((bar - viewStartBar) * static_cast<double>(pixelsPerBar));
}

double PhraseWorkspace::barForX(float x) const
{
    const auto grid = getGridBounds();
    return viewStartBar + (static_cast<double>(x - grid.getX()) / static_cast<double>(pixelsPerBar));
}

double PhraseWorkspace::beatTimeToBar(const model::DeckTimeline& deck, double beatTimeSeconds) const
{
    if (! deck.beatGrid.has_value() || deck.beatGrid->secondsPerBeat <= 0.0)
        return static_cast<double>(deck.launchOffsetBars);

    const auto beatsPerBar = std::max(1, deck.beatGrid->beatsPerBar);
    const auto beatPosition = (beatTimeSeconds - deck.beatGrid->firstBeatOffsetSeconds) / deck.beatGrid->secondsPerBeat;
    return static_cast<double>(deck.launchOffsetBars) + (beatPosition / static_cast<double>(beatsPerBar));
}

double PhraseWorkspace::getTrackLengthBars(const model::DeckTimeline& deck) const
{
    if (deck.beatGrid.has_value() && deck.beatGrid->secondsPerBeat > 0.0)
    {
        const auto secondsPerBar = deck.beatGrid->secondsPerBeat * static_cast<double>(std::max(1, deck.beatGrid->beatsPerBar));
        return deck.beatGrid->durationSeconds / secondsPerBar;
    }

    return 64.0;
}

double PhraseWorkspace::visibleEndBar() const
{
    const auto grid = getGridBounds();
    return viewStartBar + (static_cast<double>(grid.getWidth()) / static_cast<double>(pixelsPerBar));
}

void PhraseWorkspace::drawHeader(juce::Graphics& g)
{
    const auto header = getHeaderBounds();
    g.setColour(panelColour());
    g.fillRect(header);

    const auto barsPerPhrase = std::max(1, state.barsPerPhrase);
    const auto currentBar = std::max(0.0, state.currentBarPosition);
    const auto currentPhrase = static_cast<int>(std::floor(currentBar / static_cast<double>(barsPerPhrase))) + 1;
    const auto barIntoPhrase = std::fmod(currentBar, static_cast<double>(barsPerPhrase));
    const auto barsUntilBoundary = static_cast<double>(barsPerPhrase) - barIntoPhrase;

    auto content = header.reduced(outerPadding, 0.0f);
    const auto transportButton = getTransportButtonBounds();
    const auto bpmBounds = getBpmBounds();
    const auto bpmTrack = getBpmTrackBounds();
    const auto volumeBounds = getMasterVolumeBounds();
    const auto volumeTrack = getMasterVolumeTrackBounds();
    const auto isPlaying = isWorkspacePlaying();

    g.setColour(isPlaying ? juce::Colour(0xffd6eef5) : juce::Colour(0xff24313a));
    g.fillRoundedRectangle(transportButton, 8.0f);
    g.setColour(isPlaying ? juce::Colour(0xff101318) : textColour());

    if (isPlaying)
    {
        const auto pauseBarWidth = 5.0f;
        const auto pauseHeight = transportButton.getHeight() * 0.44f;
        const auto pauseY = transportButton.getCentreY() - (pauseHeight * 0.5f);
        g.fillRect(juce::Rectangle<float>(transportButton.getCentreX() - 7.0f, pauseY, pauseBarWidth, pauseHeight));
        g.fillRect(juce::Rectangle<float>(transportButton.getCentreX() + 2.0f, pauseY, pauseBarWidth, pauseHeight));
    }
    else
    {
        juce::Path playIcon;
        playIcon.startNewSubPath(transportButton.getCentreX() - 5.0f, transportButton.getCentreY() - 9.0f);
        playIcon.lineTo(transportButton.getCentreX() - 5.0f, transportButton.getCentreY() + 9.0f);
        playIcon.lineTo(transportButton.getCentreX() + 10.0f, transportButton.getCentreY());
        playIcon.closeSubPath();
        g.fillPath(playIcon);
    }

    content.removeFromLeft(transportButton.getWidth() + 14.0f);
    content.removeFromRight(bpmBounds.getWidth() + volumeBounds.getWidth() + 26.0f);
    auto titleArea = content.removeFromLeft(330.0f);

    g.setColour(textColour());
    g.setFont(makeFont(21.0f, juce::Font::bold));
    g.drawText("Phrase Alignment Workspace", titleArea.removeFromTop(36.0f).toNearestInt(),
        juce::Justification::centredLeft, true);

    if (const auto* deckA = model::findDeck(state, model::DeckId::A);
        deckA != nullptr && deckA->loadedTrack.has_value())
    {
        const auto trackText = asJuceString(deckA->loadedTrack->name)
            + (deckA->beatGrid.has_value()
                ? "  |  detected " + juce::String(deckA->beatGrid->bpm) + " BPM"
                : "");

        g.setFont(makeFont(13.0f));
        g.setColour(mutedTextColour());
        g.drawText(trackText, titleArea.toNearestInt(), juce::Justification::centredLeft, true);
    }

    g.setFont(makeFont(16.0f));
    const auto statusText = "BPM " + fixedOneDecimal(state.bpm)
        + "    Phrase " + juce::String(currentPhrase)
        + "    Next boundary in " + fixedOneDecimal(barsUntilBoundary) + " bars";

    g.setColour(mutedTextColour());
    g.drawText(statusText, content.toNearestInt(), juce::Justification::centredRight, true);

    const auto bpm = std::clamp(state.bpm, minGlobalBpm, maxGlobalBpm);
    const auto bpmNormalized = (bpm - minGlobalBpm) / (maxGlobalBpm - minGlobalBpm);
    g.setColour(juce::Colour(0xff101820));
    g.fillRoundedRectangle(bpmBounds, 8.0f);
    g.setColour(juce::Colour(0xff2a333b));
    g.drawRoundedRectangle(bpmBounds, 8.0f, 1.0f);

    const auto bpmLabel = juce::String("BPM ") + juce::String(bpm, 1);
    g.setColour(textColour());
    g.setFont(makeFont(12.5f, juce::Font::bold));
    g.drawFittedText(bpmLabel, bpmBounds.reduced(12.0f, 3.0f).removeFromTop(18.0f).toNearestInt(),
        juce::Justification::centredLeft, 1);

    g.setColour(juce::Colour(0xff0a1014));
    g.fillRoundedRectangle(bpmTrack, 5.0f);
    const auto bpmFillWidth = static_cast<float>(bpmNormalized) * bpmTrack.getWidth();
    g.setColour(juce::Colour(0xff75d1e0));
    g.fillRoundedRectangle(bpmTrack.withWidth(bpmFillWidth), 5.0f);

    const auto bpmThumbX = bpmTrack.getX() + bpmFillWidth;
    const auto bpmThumb = juce::Rectangle<float>(bpmThumbX - 9.0f, bpmTrack.getCentreY() - 13.0f, 18.0f, 26.0f);
    g.setColour(juce::Colour(0xffeef3f5));
    g.fillRoundedRectangle(bpmThumb, 7.0f);
    g.setColour(juce::Colour(0xff101318).withAlpha(0.68f));
    g.drawRoundedRectangle(bpmThumb, 7.0f, 1.0f);

    const auto volume = std::clamp(state.masterVolume, 0.0f, 1.0f);
    g.setColour(juce::Colour(0xff101820));
    g.fillRoundedRectangle(volumeBounds, 8.0f);
    g.setColour(juce::Colour(0xff2a333b));
    g.drawRoundedRectangle(volumeBounds, 8.0f, 1.0f);

    const auto volumeLabel = juce::String("VOL ") + juce::String(static_cast<int>(std::round(volume * 100.0f))) + "%";
    g.setColour(textColour());
    g.setFont(makeFont(12.5f, juce::Font::bold));
    g.drawFittedText(volumeLabel, volumeBounds.reduced(12.0f, 3.0f).removeFromTop(18.0f).toNearestInt(),
        juce::Justification::centredLeft, 1);

    g.setColour(juce::Colour(0xff0a1014));
    g.fillRoundedRectangle(volumeTrack, 5.0f);
    const auto fillWidth = volumeTrack.getWidth() * volume;
    g.setColour(juce::Colour(0xffd6eef5));
    g.fillRoundedRectangle(volumeTrack.withWidth(fillWidth), 5.0f);

    const auto thumbX = volumeTrack.getX() + fillWidth;
    const auto thumb = juce::Rectangle<float>(thumbX - 9.0f, volumeTrack.getCentreY() - 13.0f, 18.0f, 26.0f);
    g.setColour(juce::Colour(0xffeef3f5));
    g.fillRoundedRectangle(thumb, 7.0f);
    g.setColour(juce::Colour(0xff101318).withAlpha(0.68f));
    g.drawRoundedRectangle(thumb, 7.0f, 1.0f);
}

void PhraseWorkspace::drawGrid(juce::Graphics& g)
{
    const auto grid = getGridBounds();
    const auto timeline = getTimelineBounds();

    g.setColour(juce::Colour(0xff12171d));
    g.fillRoundedRectangle(grid, 8.0f);

    const auto firstBar = static_cast<int>(std::floor(viewStartBar));
    const auto lastBar = static_cast<int>(std::ceil(visibleEndBar()));
    const auto phraseInterval = std::max(1, state.barsPerPhrase);
    const auto doublePhraseInterval = std::max(phraseInterval * 2, phraseInterval);

    for (auto bar = firstBar; bar <= lastBar; ++bar)
    {
        const auto x = xForBar(static_cast<double>(bar));
        const auto isDoublePhrase = isMultipleOf(bar, doublePhraseInterval);
        const auto isPhrase = isMultipleOf(bar, phraseInterval);

        g.setColour(isDoublePhrase ? phraseBoundaryColour()
                                   : (isPhrase ? strongGridLineColour() : gridLineColour()));
        const auto thickness = isDoublePhrase ? 2.0f : (isPhrase ? 1.4f : 0.7f);
        g.drawLine(x, timeline.getY(), x, timeline.getBottom(), thickness);

        if (isPhrase)
        {
            g.setFont(makeFont(12.0f));
            g.setColour(isDoublePhrase ? textColour().withAlpha(0.72f) : mutedTextColour().withAlpha(0.58f));
            g.drawText(juce::String(bar), juce::Rectangle<float>(x + 4.0f, grid.getY() + 4.0f, 48.0f, 18.0f).toNearestInt(),
                juce::Justification::centredLeft, true);
        }
    }
}

void PhraseWorkspace::drawLanes(juce::Graphics& g)
{
    for (std::size_t deckIndex = 0; deckIndex < state.decks.size(); ++deckIndex)
    {
        const auto& deck = state.decks[deckIndex];
        const auto lane = getLaneBounds(deckIndex);
        const auto isSelected = deck.id == selectedDeck;
        const auto laneLabel = getDeckLabelBounds(deckIndex);
        const auto hasTrack = deck.loadedTrack.has_value();

        g.setColour(isSelected ? laneSelectedColour() : laneColour());
        g.fillRoundedRectangle(lane, 8.0f);

        g.setColour(isSelected ? juce::Colour(0xff4fbed2) : juce::Colour(0xff2a333b));
        g.drawRoundedRectangle(lane, 8.0f, isSelected ? 2.0f : 1.0f);

        g.setColour(panelColour());
        g.fillRoundedRectangle(laneLabel, 8.0f);

        auto textBounds = laneLabel.reduced(14.0f, 12.0f);
        auto deckHeader = textBounds.removeFromTop(36.0f);
        const auto badge = deckHeader.removeFromLeft(46.0f).reduced(0.0f, 1.0f);

        g.setColour(isSelected ? juce::Colour(0xff4fbed2) : juce::Colour(0xff2a333b));
        g.fillRoundedRectangle(badge, 6.0f);
        g.setColour(textColour());
        g.setFont(makeFont(18.0f, juce::Font::bold));
        g.drawFittedText(asJuceString(model::toString(deck.id)).fromLastOccurrenceOf(" ", false, true),
            badge.toNearestInt(), juce::Justification::centred, 1);

        auto roleArea = deckHeader.reduced(10.0f, 0.0f);
        g.setColour(mutedTextColour());
        g.setFont(makeFont(12.5f, juce::Font::bold));
        g.drawFittedText(asJuceString(model::toString(deck.role)).toUpperCase(),
            roleArea.removeFromTop(16.0f).toNearestInt(), juce::Justification::centredLeft, 1);

        const auto launchText = juce::String("Offset ")
            + juce::String(deck.launchOffsetBars >= 0 ? "+" : "")
            + juce::String(deck.launchOffsetBars)
            + " bars";
        g.setColour(mutedTextColour().withAlpha(0.72f));
        g.setFont(makeFont(11.5f));
        g.drawFittedText(launchText, roleArea.toNearestInt(), juce::Justification::centredLeft, 1);

        textBounds.removeFromTop(10.0f);
        const auto title = hasTrack ? asJuceString(deck.loadedTrack->name) : juce::String("Empty deck");
        g.setColour(hasTrack ? textColour() : mutedTextColour().withAlpha(0.58f));
        g.setFont(makeFont(hasTrack ? 16.0f : 14.0f, hasTrack ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText(title, textBounds.removeFromTop(46.0f).toNearestInt(), juce::Justification::centredLeft, 2);

        const auto key = hasTrack && ! deck.loadedTrack->key.empty() ? asJuceString(deck.loadedTrack->key) : juce::String("--");
        const auto meta = juce::String("Key ") + key
            + "   BPM " + trackBpmText(deck, state.bpm)
            + "   " + formatDuration(trackDurationSeconds(deck));

        g.setColour(hasTrack ? juce::Colour(0xffd6eef5) : mutedTextColour().withAlpha(0.58f));
        g.setFont(makeFont(13.0f, juce::Font::bold));
        g.drawFittedText(meta, textBounds.removeFromTop(24.0f).toNearestInt(), juce::Justification::centredLeft, 1);

        const auto stateLine = juce::String("Vol ") + juce::String(static_cast<int>(std::round(deck.volume * 100.0f))) + "%"
            + (deck.lowCutEnabled ? "   Low cut" : juce::String());
        g.setColour(deck.lowCutEnabled ? juce::Colour(0xff75d1e0) : mutedTextColour().withAlpha(0.72f));
        g.setFont(makeFont(12.0f));
        g.drawFittedText(stateLine, textBounds.withTrimmedBottom(38.0f).toNearestInt(), juce::Justification::centredLeft, 1);

        for (const auto stemType : stemToggleOrder)
        {
            const auto button = getStemToggleBounds(deckIndex, stemType);
            const auto stemEnabled = model::isStemEnabled(deck.stemEnabled, stemType);
            const auto colour = stemColour(stemType);

            g.setColour(stemEnabled ? colour.withAlpha(hasTrack ? 0.90f : 0.54f)
                                    : juce::Colour(0xff0d1317).withAlpha(0.92f));
            g.fillRoundedRectangle(button, 6.0f);

            g.setColour(colour.withAlpha(stemEnabled ? 0.90f : 0.42f));
            g.drawRoundedRectangle(button, 6.0f, stemEnabled ? 1.5f : 1.1f);

            g.setColour(stemEnabled ? juce::Colour(0xff071014) : colour.withAlpha(0.72f));
            g.setFont(makeFont(13.0f, juce::Font::bold));
            g.drawFittedText(stemShortLabel(stemType), button.toNearestInt(), juce::Justification::centred, 1);
        }
    }
}

void PhraseWorkspace::drawLoadedTrackBeds(juce::Graphics& g)
{
    const auto grid = getGridBounds();
    g.saveState();
    g.reduceClipRegion(grid.toNearestInt());

    for (std::size_t deckIndex = 0; deckIndex < state.decks.size(); ++deckIndex)
    {
        const auto& deck = state.decks[deckIndex];
        if (! deck.loadedTrack.has_value())
            continue;

        const auto bed = getLoadedTrackBounds(deckIndex);

        if (bed.getRight() < grid.getX() || bed.getX() > grid.getRight())
            continue;

        g.setColour(juce::Colour(0xff18323a));
        g.fillRoundedRectangle(bed, 7.0f);

        const auto isDragging = activeTrackDrag.has_value() && activeTrackDrag->deckIndex == deckIndex;
        const auto isSelected = deck.id == selectedDeck;
        g.setColour((isDragging || isSelected ? juce::Colour(0xffe7f7ff) : juce::Colour(0xff4fbed2)).withAlpha(isDragging ? 0.95f : 0.80f));
        g.drawRoundedRectangle(bed, 7.0f, isDragging ? 2.4f : 1.5f);

        auto titleArea = getLoadedTrackTitleBounds(deckIndex);
        auto waveformArea = getLoadedTrackWaveformBounds(deckIndex);

        g.setColour(juce::Colour(0xff0f1c22).withAlpha(0.94f));
        g.fillRoundedRectangle(titleArea, 5.0f);
        g.setColour((isDragging ? juce::Colour(0xffe7f7ff) : juce::Colour(0xff4fbed2)).withAlpha(isDragging ? 0.80f : 0.42f));
        g.drawRoundedRectangle(titleArea, 5.0f, isDragging ? 1.8f : 1.0f);

        const auto handle = titleArea.removeFromLeft(24.0f).reduced(7.0f, 9.0f);
        g.setColour(juce::Colour(0xffe7f7ff).withAlpha(isDragging ? 0.55f : 0.28f));
        for (auto handleLine = 0; handleLine < 3; ++handleLine)
        {
            const auto x = handle.getX() + static_cast<float>(handleLine * 4);
            g.drawLine(x, handle.getY(), x, handle.getBottom(), 1.2f);
        }

        auto titleText = titleArea.reduced(8.0f, 0.0f);
        auto metaArea = titleText.removeFromRight(std::min(210.0f, titleText.getWidth() * 0.46f));
        const auto key = ! deck.loadedTrack->key.empty() ? asJuceString(deck.loadedTrack->key) : juce::String("--");
        const auto meta = juce::String("Key ") + key
            + "   " + trackBpmText(deck, state.bpm) + " BPM"
            + "   " + formatDuration(trackDurationSeconds(deck));

        g.setColour(textColour());
        g.setFont(makeFont(14.0f, juce::Font::bold));
        g.drawFittedText(asJuceString(deck.loadedTrack->name), titleText.toNearestInt(),
            juce::Justification::centredLeft, 1);

        g.setColour(mutedTextColour().brighter(0.18f));
        g.setFont(makeFont(12.0f, juce::Font::bold));
        g.drawFittedText(meta, metaArea.toNearestInt(), juce::Justification::centredRight, 1);

        drawStemWaveforms(g, deck, waveformArea);
    }

    g.restoreState();
}

void PhraseWorkspace::drawStemWaveforms(juce::Graphics& g, const model::DeckTimeline& deck, juce::Rectangle<float> bed)
{
    if (deck.stemWaveforms.empty())
        return;

    const auto* beatGrid = deck.beatGrid.has_value() ? &*deck.beatGrid : nullptr;
    const auto visibleStart = viewStartBar;
    const auto visibleEnd = visibleEndBar();
    constexpr std::array drawOrder { model::StemType::Drums, model::StemType::Bass, model::StemType::MusicResidual, model::StemType::Vocals };
    const auto rowGap = 5.0f;
    const auto rowHeight = (bed.getHeight() - (rowGap * static_cast<float>(drawOrder.size() - 1)))
        / static_cast<float>(drawOrder.size());
    auto rowIndex = 0;

    for (const auto stemType : drawOrder)
    {
        const auto iter = std::find_if(deck.stemWaveforms.begin(), deck.stemWaveforms.end(),
            [stemType](const model::StemWaveform& waveform) { return waveform.type == stemType; });

        const auto row = juce::Rectangle<float>(
            bed.getX(),
            bed.getY() + (static_cast<float>(rowIndex) * (rowHeight + rowGap)),
            bed.getWidth(),
            rowHeight).reduced(1.0f, 0.0f);
        ++rowIndex;

        const auto colour = stemColour(stemType);
        const auto stemEnabled = model::isStemEnabled(deck.stemEnabled, stemType);
        g.setColour(juce::Colour(0xff0c161b).withAlpha(0.62f));
        g.fillRoundedRectangle(row, 4.0f);
        g.setColour(colour.withAlpha(stemEnabled ? 0.28f : 0.12f));
        g.drawRoundedRectangle(row, 4.0f, 0.8f);

        const auto labelBounds = juce::Rectangle<float>(row.getX() + 6.0f, row.getY() + 4.0f, 54.0f, 17.0f);
        g.setColour(juce::Colour(0xff0a1014).withAlpha(0.80f));
        g.fillRoundedRectangle(labelBounds, 4.0f);
        g.setColour(colour.withAlpha(stemEnabled ? 1.0f : 0.45f));
        g.setFont(makeFont(11.5f, juce::Font::bold));
        g.drawFittedText(asJuceString(model::toString(stemType)), labelBounds.reduced(6.0f, 0.0f).toNearestInt(),
            juce::Justification::centredLeft, 1);

        g.setColour(juce::Colour(0xffffffff).withAlpha(0.08f));
        g.drawLine(row.getX(), row.getCentreY(), row.getRight(), row.getCentreY(), 0.8f);

        if (iter == deck.stemWaveforms.end() || iter->peaks.empty() || iter->pointsPerSecond <= 0.0)
            continue;

        const auto& waveform = *iter;
        const auto duration = waveform.durationSeconds > 0.0 ? waveform.durationSeconds
            : (deck.loadedTrack.has_value() ? deck.loadedTrack->durationSeconds : 0.0);

        if (duration <= 0.0)
            continue;

        double firstVisibleSecond = 0.0;
        double lastVisibleSecond = duration;

        if (beatGrid != nullptr && beatGrid->secondsPerBeat > 0.0)
        {
            const auto secondsPerBar = beatGrid->secondsPerBeat * static_cast<double>(std::max(1, beatGrid->beatsPerBar));
            firstVisibleSecond = ((visibleStart - static_cast<double>(deck.launchOffsetBars)) * secondsPerBar)
                + beatGrid->firstBeatOffsetSeconds;
            lastVisibleSecond = ((visibleEnd - static_cast<double>(deck.launchOffsetBars)) * secondsPerBar)
                + beatGrid->firstBeatOffsetSeconds;
        }

        firstVisibleSecond = std::clamp(firstVisibleSecond, 0.0, duration);
        lastVisibleSecond = std::clamp(lastVisibleSecond, 0.0, duration);

        if (lastVisibleSecond <= firstVisibleSecond)
            continue;

        const auto startIndex = static_cast<std::size_t>(
            std::max(0.0, std::floor(firstVisibleSecond * waveform.pointsPerSecond) - 2.0));
        const auto endIndex = std::min<std::size_t>(
            waveform.peaks.size() - 1,
            static_cast<std::size_t>(std::ceil(lastVisibleSecond * waveform.pointsPerSecond) + 2.0));

        if (endIndex <= startIndex)
            continue;

        const auto centerY = row.getCentreY();
        const auto halfHeight = row.getHeight() * 0.42f;
        juce::Path waveformPath;
        auto hasPoint = false;

        for (auto index = startIndex; index <= endIndex; ++index)
        {
            const auto timeSeconds = static_cast<double>(index) / waveform.pointsPerSecond;
            const auto markerBar = beatGrid != nullptr ? beatTimeToBar(deck, timeSeconds)
                : static_cast<double>(deck.launchOffsetBars) + ((timeSeconds / duration) * 64.0);
            const auto x = xForBar(markerBar);
            const auto peak = std::sqrt(std::clamp(waveform.peaks[index], 0.0f, 1.0f));
            const auto y = centerY - (peak * halfHeight);

            if (! hasPoint)
            {
                waveformPath.startNewSubPath(x, y);
                hasPoint = true;
            }
            else
            {
                waveformPath.lineTo(x, y);
            }
        }

        for (auto reverseIndex = endIndex + 1; reverseIndex-- > startIndex;)
        {
            const auto timeSeconds = static_cast<double>(reverseIndex) / waveform.pointsPerSecond;
            const auto markerBar = beatGrid != nullptr ? beatTimeToBar(deck, timeSeconds)
                : static_cast<double>(deck.launchOffsetBars) + ((timeSeconds / duration) * 64.0);
            const auto x = xForBar(markerBar);
            const auto peak = std::sqrt(std::clamp(waveform.peaks[reverseIndex], 0.0f, 1.0f));
            const auto y = centerY + (peak * halfHeight);
            waveformPath.lineTo(x, y);

            if (reverseIndex == 0)
                break;
        }

        waveformPath.closeSubPath();

        g.setColour(colour.withAlpha(stemEnabled ? 0.38f : 0.11f));
        g.fillPath(waveformPath);
        g.setColour(colour.withAlpha(stemEnabled ? 0.88f : 0.28f));
        g.strokePath(waveformPath, juce::PathStrokeType(0.75f));
    }
}

void PhraseWorkspace::drawBeatMarkers(juce::Graphics& g)
{
    const auto grid = getGridBounds();
    g.saveState();
    g.reduceClipRegion(grid.toNearestInt());

    for (std::size_t deckIndex = 0; deckIndex < state.decks.size(); ++deckIndex)
    {
        const auto& deck = state.decks[deckIndex];
        if (! deck.beatGrid.has_value())
            continue;

        auto lane = getLaneBounds(deckIndex).reduced(8.0f, deck.loadedTrack.has_value() ? 14.0f : 8.0f);
        if (deck.loadedTrack.has_value())
            lane = getLoadedTrackWaveformBounds(deckIndex);

        const auto beatsPerBar = std::max(1, deck.beatGrid->beatsPerBar);
        const auto showSubBeats = pixelsPerBar >= 40.0f;

        for (std::size_t beatIndex = 0; beatIndex < deck.beatGrid->beatTimesSeconds.size(); ++beatIndex)
        {
            const auto markerBar = beatTimeToBar(deck, deck.beatGrid->beatTimesSeconds[beatIndex]);

            if (markerBar < viewStartBar || markerBar > visibleEndBar())
                continue;

            const auto x = xForBar(markerBar);
            const auto isDownbeat = beatIndex % static_cast<std::size_t>(beatsPerBar) == 0;

            if (! isDownbeat && ! showSubBeats)
                continue;

            if (isDownbeat)
            {
                g.setColour(juce::Colour(0xffe7f7ff).withAlpha(0.26f));
                g.drawLine(x, lane.getY() + 2.0f, x, lane.getBottom() - 2.0f, 1.1f);
                g.setColour(juce::Colour(0xffe7f7ff).withAlpha(0.72f));
                g.drawLine(x, lane.getY() + 2.0f, x, lane.getY() + 14.0f, 1.8f);
                continue;
            }

            g.setColour(juce::Colour(0xffe7f7ff).withAlpha(0.22f));
            g.drawLine(x, lane.getY() + 4.0f, x, lane.getY() + 10.0f, 0.8f);
        }
    }

    g.restoreState();
}

void PhraseWorkspace::drawPhraseMarkers(juce::Graphics& g)
{
    const auto grid = getGridBounds();
    g.saveState();
    g.reduceClipRegion(grid.toNearestInt());

    for (std::size_t deckIndex = 0; deckIndex < state.decks.size(); ++deckIndex)
    {
        const auto& deck = state.decks[deckIndex];
        if (! deck.loadedTrack.has_value() || deck.blocks.empty())
            continue;

        auto lane = getLoadedTrackWaveformBounds(deckIndex);

        const auto top = lane.getY() + 2.0f;
        const auto bottom = lane.getBottom() - 2.0f;
        for (const auto& block : deck.blocks)
        {
            const auto markerBar = effectiveStartBar(deck, block);

            if (markerBar < viewStartBar || markerBar > visibleEndBar())
                continue;

            const auto x = xForBar(markerBar);
            const auto colour = phraseColour(block.type);

            g.setColour(colour.withAlpha(0.88f));
            g.drawLine(x, top, x, bottom, 2.0f);

            g.setColour(colour.withAlpha(0.18f));
            g.fillRect(juce::Rectangle<float>(x, top + 18.0f, std::max(1.0f, pixelsPerBar * 0.08f), bottom - top - 18.0f));
        }
    }

    g.restoreState();
}

void PhraseWorkspace::drawPhraseBlocks(juce::Graphics& g)
{
    // TODO(waveform rendering): draw analyzed waveform/energy detail behind each phrase block.
    const auto grid = getGridBounds();
    g.saveState();
    g.reduceClipRegion(grid.toNearestInt());

    for (std::size_t deckIndex = 0; deckIndex < state.decks.size(); ++deckIndex)
    {
        const auto& deck = state.decks[deckIndex];
        if (deck.loadedTrack.has_value())
            continue;

        for (std::size_t blockIndex = 0; blockIndex < deck.blocks.size(); ++blockIndex)
        {
            const auto& block = deck.blocks[blockIndex];
            const auto bounds = getBlockBounds(deckIndex, blockIndex).reduced(3.0f, 3.0f);

            if (bounds.getRight() < grid.getX() || bounds.getX() > grid.getRight())
                continue;

            const auto conflictFlags = detectConflictForBlock(deckIndex, blockIndex);
            auto colour = phraseColour(block.type).interpolatedWith(juce::Colours::white, 0.10f + (0.12f * block.energy));
            colour = colour.withMultipliedBrightness(0.72f + (0.45f * block.energy));

            g.setColour(deck.loadedTrack.has_value() ? colour.withAlpha(0.72f) : colour);
            g.fillRoundedRectangle(bounds, 7.0f);

            const auto isSelected = deck.id == selectedDeck && selectedBlockIndex.has_value() && *selectedBlockIndex == blockIndex;
            g.setColour(isSelected ? juce::Colours::white.withAlpha(0.92f) : juce::Colours::black.withAlpha(0.25f));
            g.drawRoundedRectangle(bounds, 7.0f, isSelected ? 2.2f : 1.0f);

            if (conflictFlags.bass)
            {
                const auto strip = bounds.withHeight(7.0f);
                g.setColour(juce::Colour(0xffffc247));
                g.fillRoundedRectangle(strip, 4.0f);
            }

            if (conflictFlags.vocal)
            {
                const auto strip = bounds.withY(bounds.getBottom() - 7.0f).withHeight(7.0f);
                g.setColour(juce::Colour(0xffff6fb2));
                g.fillRoundedRectangle(strip, 4.0f);
            }

            auto textArea = bounds.reduced(12.0f, 8.0f);
            g.setColour(juce::Colours::white);
            g.setFont(makeFont(deck.loadedTrack.has_value() ? 13.5f : 16.0f, juce::Font::bold));
            g.drawFittedText(asJuceString(model::toString(block.type)), textArea.removeFromTop(deck.loadedTrack.has_value() ? 18.0f : 24.0f).toNearestInt(),
                juce::Justification::centredLeft, 1);

            g.setFont(makeFont(deck.loadedTrack.has_value() ? 11.0f : 12.5f));
            g.setColour(juce::Colours::white.withAlpha(0.84f));

            auto meta = juce::String(block.lengthBars) + " bars";
            if (conflictFlags.bass)
                meta += "  BASS";
            if (conflictFlags.vocal)
                meta += "  VOCAL";

            if (bounds.getWidth() > 82.0f)
                g.drawFittedText(meta, textArea.toNearestInt(), juce::Justification::centredLeft, 1);
        }
    }

    g.restoreState();
}

void PhraseWorkspace::drawPlayhead(juce::Graphics& g)
{
    const auto grid = getGridBounds();
    const auto x = xForBar(state.currentBarPosition);

    if (x < grid.getX() || x > grid.getRight())
        return;

    g.setColour(juce::Colour(0xfffff4a3).withAlpha(0.22f));
    g.fillRect(juce::Rectangle<float>(x - 5.0f, grid.getY(), 10.0f, grid.getHeight()));

    g.setColour(juce::Colour(0xfffff4a3));
    g.drawLine(x, grid.getY(), x, grid.getBottom(), 2.8f);
}

void PhraseWorkspace::drawControlRail(juce::Graphics& g)
{
    const auto controls = getControlBounds();
    g.setColour(panelColour());
    g.fillRect(controls);

    g.setColour(juce::Colour(0xff2a333b));
    g.drawLine(controls.getX(), controls.getY(), controls.getRight(), controls.getY(), 1.0f);
}

std::optional<PhraseWorkspace::HitBlock> PhraseWorkspace::hitTestBlock(juce::Point<float> position) const
{
    for (std::size_t deckIndex = 0; deckIndex < state.decks.size(); ++deckIndex)
    {
        const auto& deck = state.decks[deckIndex];
        if (deck.loadedTrack.has_value())
            continue;

        for (std::size_t blockIndex = 0; blockIndex < deck.blocks.size(); ++blockIndex)
        {
            if (getBlockBounds(deckIndex, blockIndex).contains(position))
                return HitBlock { deckIndex, blockIndex, deck.id };
        }
    }

    return std::nullopt;
}

std::optional<PhraseWorkspace::HitTrack> PhraseWorkspace::hitTestLoadedTrack(juce::Point<float> position) const
{
    for (std::size_t deckIndex = 0; deckIndex < state.decks.size(); ++deckIndex)
    {
        const auto& deck = state.decks[deckIndex];
        if (deck.loadedTrack.has_value() && getLoadedTrackTitleBounds(deckIndex).contains(position))
            return HitTrack { deckIndex, deck.id };
    }

    return std::nullopt;
}

std::optional<PhraseWorkspace::HitStemToggle> PhraseWorkspace::hitTestStemToggle(juce::Point<float> position) const
{
    for (std::size_t deckIndex = 0; deckIndex < state.decks.size(); ++deckIndex)
    {
        const auto& deck = state.decks[deckIndex];
        for (const auto stemType : stemToggleOrder)
            if (getStemToggleBounds(deckIndex, stemType).contains(position))
                return HitStemToggle { deckIndex, deck.id, stemType };
    }

    return std::nullopt;
}

std::optional<model::DeckId> PhraseWorkspace::hitTestDeck(juce::Point<float> position) const
{
    for (std::size_t deckIndex = 0; deckIndex < state.decks.size(); ++deckIndex)
        if (getLaneBounds(deckIndex).contains(position))
            return state.decks[deckIndex].id;

    return std::nullopt;
}

int PhraseWorkspace::snapStartBar(double rawStartBar) const
{
    if (snapMode == SnapMode::Phrase)
    {
        const auto phrase = std::max(1, state.barsPerPhrase);
        return static_cast<int>(std::round(rawStartBar / static_cast<double>(phrase)) * static_cast<double>(phrase));
    }

    return static_cast<int>(std::round(rawStartBar));
}

void PhraseWorkspace::setSnapMode(SnapMode newMode)
{
    snapMode = newMode;
    updateButtonText();
    repaint();
}

void PhraseWorkspace::nudgeSelectedLaunchOffset(int deltaBars)
{
    if (auto* deck = model::findDeck(state, selectedDeck))
        deck->launchOffsetBars += deltaBars;

    if (onLaunchOffsetNudged)
        onLaunchOffsetNudged(selectedDeck, deltaBars);

    repaint();
}

void PhraseWorkspace::cycleSelectedRole()
{
    auto* deck = model::findDeck(state, selectedDeck);
    if (deck == nullptr)
        return;

    deck->role = model::nextRole(deck->role);

    if (onRoleChangeRequested)
        onRoleChangeRequested(selectedDeck, deck->role);

    repaint();
}

void PhraseWorkspace::updateButtonText()
{
    snapButton.setButtonText(snapMode == SnapMode::Bar ? "Snap: Bar" : "Snap: Phrase");
}

PhraseWorkspace::ConflictFlags PhraseWorkspace::detectConflictForBlock(std::size_t deckIndex, std::size_t blockIndex) const
{
    ConflictFlags conflictFlags;

    if (deckIndex >= state.decks.size() || blockIndex >= state.decks[deckIndex].blocks.size())
        return conflictFlags;

    const auto& deck = state.decks[deckIndex];
    const auto& block = deck.blocks[blockIndex];
    const auto start = effectiveStartBar(deck, block);
    const auto end = effectiveEndBar(deck, block);

    for (std::size_t otherDeckIndex = 0; otherDeckIndex < state.decks.size(); ++otherDeckIndex)
    {
        if (otherDeckIndex == deckIndex)
            continue;

        const auto& otherDeck = state.decks[otherDeckIndex];
        for (const auto& otherBlock : otherDeck.blocks)
        {
            if (! rangesOverlap(start, end, effectiveStartBar(otherDeck, otherBlock), effectiveEndBar(otherDeck, otherBlock)))
                continue;

            conflictFlags.bass = conflictFlags.bass || (activeBass(deck, block) && activeBass(otherDeck, otherBlock));
            conflictFlags.vocal = conflictFlags.vocal || (activeVocal(deck, block) && activeVocal(otherDeck, otherBlock));

            if (conflictFlags.bass && conflictFlags.vocal)
                return conflictFlags;
        }
    }

    return conflictFlags;
}

float PhraseWorkspace::volumeForX(float x) const
{
    const auto track = getMasterVolumeTrackBounds();
    if (track.getWidth() <= 0.0f)
        return state.masterVolume;

    return std::clamp((x - track.getX()) / track.getWidth(), 0.0f, 1.0f);
}

void PhraseWorkspace::setMasterVolumeFromPoint(juce::Point<float> position)
{
    const auto volume = volumeForX(position.x);
    if (std::abs(state.masterVolume - volume) < 0.001f)
        return;

    state.masterVolume = volume;

    if (onMasterVolumeChanged)
        onMasterVolumeChanged(volume);
}

double PhraseWorkspace::bpmForX(float x) const
{
    const auto track = getBpmTrackBounds();
    if (track.getWidth() <= 0.0f)
        return state.bpm;

    const auto normalized = std::clamp((x - track.getX()) / track.getWidth(), 0.0f, 1.0f);
    return minGlobalBpm + (static_cast<double>(normalized) * (maxGlobalBpm - minGlobalBpm));
}

void PhraseWorkspace::setBpmFromPoint(juce::Point<float> position)
{
    const auto bpm = bpmForX(position.x);
    if (std::abs(state.bpm - bpm) < 0.05)
        return;

    state.bpm = bpm;

    if (onBpmChanged)
        onBpmChanged(bpm);
}

bool PhraseWorkspace::isWorkspacePlaying() const
{
    return std::any_of(state.decks.begin(), state.decks.end(),
        [](const model::DeckTimeline& deck) { return deck.isPlaying; });
}

void PhraseWorkspace::zoomAround(float componentX, float scaleFactor)
{
    const auto clampedScale = std::clamp(scaleFactor, 0.7f, 1.4f);
    const auto grid = getGridBounds();
    const auto anchorX = std::clamp(componentX, grid.getX(), grid.getRight());
    const auto anchorBar = barForX(anchorX);

    pixelsPerBar = std::clamp(pixelsPerBar * clampedScale, minPixelsPerBar, maxPixelsPerBar);
    viewStartBar = anchorBar - (static_cast<double>(anchorX - grid.getX()) / static_cast<double>(pixelsPerBar));
    repaint();
}

void PhraseWorkspace::updatePinchZoomFromPointers()
{
    // TODO(multi-touch pinch zoom): keep using tracked MouseInputSource IDs here, then derive
    // a two-finger scale and pan gesture once target touchscreen hardware is available.
    // TODO(GPU rendering path): if dense waveform/analysis overlays arrive, move the timeline
    // fill and block rendering behind an OpenGL/Metal-capable renderer abstraction.
}
} // namespace mixdesk::ui
